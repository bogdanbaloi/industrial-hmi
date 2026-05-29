// [utest->req~dashboard-007~1]
// Covers REQ-DASHBOARD-007 (the THROUGHPUT KPI reflects a live measured
// production rate, not a static value) at the computation seam.
//
// app::model::ThroughputMeter is the pure, clock-injected core that backs
// the dashboard's units/hour figure: the caller passes explicit time
// points, so the rate math is fully deterministic here -- no sleeps, no
// wall clock. SimulatedModel wires it to a real steady_clock (verified
// indirectly by the presenter/model tests); this file pins the math.

#include "src/model/ThroughputMeter.h"

#include <chrono>

#include <gtest/gtest.h>

namespace {

using app::model::ThroughputMeter;
using Clock     = ThroughputMeter::Clock;
using TimePoint = ThroughputMeter::TimePoint;

// Anchor every test on a fixed base so timestamps are pure arithmetic.
TimePoint at(int seconds) {
    return TimePoint{} + std::chrono::seconds{seconds};
}

TEST(ThroughputMeterTest, ZeroWithFewerThanTwoCompletions) {
    ThroughputMeter meter;
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(0)), 0.0) << "no completions";

    meter.recordCompletion(at(0));
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(10)), 0.0)
        << "one completion gives no measurable interval";
}

TEST(ThroughputMeterTest, TwoCompletionsOneMinuteApartIsSixtyPerHour) {
    ThroughputMeter meter;
    meter.recordCompletion(at(0));
    meter.recordCompletion(at(60));
    // (2-1) intervals over 60s -> 3600/60 = 60 u/h.
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(60)), 60.0);
}

TEST(ThroughputMeterTest, AveragesAcrossMultipleCompletions) {
    ThroughputMeter meter;
    meter.recordCompletion(at(0));
    meter.recordCompletion(at(30));
    meter.recordCompletion(at(60));
    meter.recordCompletion(at(90));
    // (4-1) intervals over the 90s span -> 3*3600/90 = 120 u/h.
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(90)), 120.0);
}

TEST(ThroughputMeterTest, DecaysToZeroWhenWindowEmpties) {
    ThroughputMeter meter;  // default 120s window
    meter.recordCompletion(at(0));
    meter.recordCompletion(at(10));
    // Query far in the future: both completions fall outside the trailing
    // 120s window (cutoff = 200-120 = 80), so they are evicted and the
    // stalled line reads 0 instead of freezing on its last rate.
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(200)), 0.0);
}

TEST(ThroughputMeterTest, RespectsCustomWindow) {
    ThroughputMeter meter{std::chrono::seconds{60}};
    meter.recordCompletion(at(0));
    meter.recordCompletion(at(30));
    meter.recordCompletion(at(90));
    // Querying at 90 with a 60s window evicts the t=0 sample (cutoff 30),
    // leaving {30, 90}: (2-1)*3600/60 = 60 u/h.
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(90)), 60.0);
}

TEST(ThroughputMeterTest, ClearForgetsHistory) {
    ThroughputMeter meter;
    meter.recordCompletion(at(0));
    meter.recordCompletion(at(60));
    ASSERT_DOUBLE_EQ(meter.unitsPerHour(at(60)), 60.0);

    meter.clear();
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(60)), 0.0)
        << "clear() drops all completion history";
}

TEST(ThroughputMeterTest, SameInstantCompletionsGiveNoSpuriousRate) {
    ThroughputMeter meter;
    meter.recordCompletion(at(5));
    meter.recordCompletion(at(5));
    // Zero span must not divide-by-zero into infinity.
    EXPECT_DOUBLE_EQ(meter.unitsPerHour(at(5)), 0.0);
}

}  // namespace
