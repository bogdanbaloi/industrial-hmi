// [utest->req~perf-001~1]
// Covers REQ-PERF-001 (reproducible microbenchmarks on hot paths).
//
// Benchmark: AlertCenter lifecycle operations under ISA-18.2 alarm-storm
// conditions.
//
// Why this matters: in a fault cascade, a HMI can receive hundreds of
// raise() calls per second from the producer thread while the operator
// is acknowledging the highest-priority entries from the UI thread.
// Both paths take AlertCenter's internal mutex; both paths are on the
// critical path for "did the operator see the safety event within the
// regulatory budget?".
//
// We report p50 / p90 / p99 instead of means -- a tail latency that
// breaches the ISA-18.2 operator-response window is what causes the
// incident, not the average.
//
// Reference numbers (see benchmarks/README.md) are captured on an
// AMD Ryzen 7 5800X under WSL Ubuntu 24.04 in release mode. Treat
// the absolute values as an order-of-magnitude check; the SHAPE
// (constant vs O(N log N) for snapshot) is the contract.

#include <benchmark/benchmark.h>

#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"

#include <string>

using app::presenter::AlertCenter;
using app::presenter::AlertSeverity;
using app::presenter::AlertViewModel;
using app::presenter::kAlarmPriorityHigh;
using app::presenter::kAlarmPriorityLow;
using app::presenter::kAlarmPriorityMedium;

namespace {

AlertViewModel makeAlarm(int id, int priority) {
    AlertViewModel vm;
    vm.key       = "equip-" + std::to_string(id);
    vm.severity  = AlertSeverity::Warning;
    vm.title     = "Equipment fault";
    vm.message   = "Pass rate below threshold";
    vm.timestamp = "12:34:56";
    vm.priority  = priority;
    return vm;
}

/// Pre-fill an AlertCenter with N distinct active alarms so the
/// benchmarked operation sees a realistic working set, not an empty
/// container. Done in SetUp (outside the timed loop).
void fill(AlertCenter& ac, int n) {
    for (int i = 0; i < n; ++i) {
        // Rotate priority across the three operator-meaningful tiers
        // so snapshot()'s stable_sort actually has work to do.
        const int prio = (i % 3 == 0)   ? kAlarmPriorityHigh
                       : (i % 3 == 1)   ? kAlarmPriorityMedium
                                        : kAlarmPriorityLow;
        ac.raise(makeAlarm(i, prio));
    }
}

}  // namespace

// ---------------------------------------------------------------------
// raise() on an EMPTY center -- the cheap case (new entry, no scan).
// ---------------------------------------------------------------------
static void BM_AlertCenter_RaiseNew(benchmark::State& state) {
    AlertCenter ac;
    int id = 0;
    for (auto _ : state) {
        ac.raise(makeAlarm(id++, kAlarmPriorityHigh));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AlertCenter_RaiseNew)
    ->Unit(benchmark::kNanosecond)
    ->ComputeStatistics("p50", [](const std::vector<double>& v) {
        auto s = v; std::sort(s.begin(), s.end());
        return s[s.size() / 2];
    })
    ->ComputeStatistics("p90", [](const std::vector<double>& v) {
        auto s = v; std::sort(s.begin(), s.end());
        return s[(s.size() * 9) / 10];
    })
    ->ComputeStatistics("p99", [](const std::vector<double>& v) {
        auto s = v; std::sort(s.begin(), s.end());
        return s[(s.size() * 99) / 100];
    })
    ->Repetitions(10);

// ---------------------------------------------------------------------
// raise() that REFRESHES an existing alarm. This is the more common
// case during a storm: the producer re-reports the same condition on
// every poll tick, so AlertCenter must dedupe by key. The internal
// linear scan over active alarms is the cost we want to expose.
// ---------------------------------------------------------------------
static void BM_AlertCenter_RaiseRefresh(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    AlertCenter ac;
    fill(ac, n);
    // Refresh the LAST alarm: worst case for a linear scan (O(N)).
    const auto vm = makeAlarm(n - 1, kAlarmPriorityHigh);
    for (auto _ : state) {
        ac.raise(vm);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("N=" + std::to_string(n));
}
BENCHMARK(BM_AlertCenter_RaiseRefresh)
    ->Arg(10)->Arg(100)->Arg(1000)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5);

// ---------------------------------------------------------------------
// snapshot() -- the UI-thread path. Cost grows with active-set size
// because the implementation copies + stable_sort by priority. This is
// where a tail-latency budget for the UI thread shows up.
// ---------------------------------------------------------------------
static void BM_AlertCenter_Snapshot(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    AlertCenter ac;
    fill(ac, n);
    for (auto _ : state) {
        auto snap = ac.snapshot();
        benchmark::DoNotOptimize(snap);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("N=" + std::to_string(n));
}
BENCHMARK(BM_AlertCenter_Snapshot)
    ->Arg(10)->Arg(100)->Arg(1000)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5);

// ---------------------------------------------------------------------
// acknowledge() -- pure mutator, no copy. Like RaiseRefresh, the
// hot path is the linear scan. Validates the scaling story for
// operator interaction during a storm.
// ---------------------------------------------------------------------
static void BM_AlertCenter_Acknowledge(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    AlertCenter ac;
    fill(ac, n);
    // Toggle ack/refresh -- ack changes state to AckActive, then
    // raise() refresh keeps it there so subsequent ack() calls stay
    // no-ops on the same key but with realistic scan cost.
    const std::string key = "equip-" + std::to_string(n - 1);
    for (auto _ : state) {
        ac.acknowledge(key);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("N=" + std::to_string(n));
}
BENCHMARK(BM_AlertCenter_Acknowledge)
    ->Arg(10)->Arg(100)->Arg(1000)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5);

BENCHMARK_MAIN();
