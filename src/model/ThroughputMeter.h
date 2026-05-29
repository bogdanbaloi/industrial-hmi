#pragma once

#include <chrono>
#include <deque>

namespace app::model {

/// Computes a production throughput rate -- completed work units per hour
/// -- from a trailing time-window of completion timestamps.
///
/// Pure + clock-injected: the caller passes the current time point on
/// every call (`recordCompletion` / `unitsPerHour`), so the meter is fully
/// deterministic and unit-testable with synthetic timestamps -- no real
/// sleeps and no internal clock. The model wires it to a real
/// `std::chrono::steady_clock`; tests pass fabricated time points.
///
/// Rate model: with N completion timestamps inside the trailing window,
/// the rate is the (N-1) intervals they span, scaled to an hour:
///   unitsPerHour = (N - 1) * 3600 / secondsBetween(oldest, newest).
/// Fewer than two in-window completions yields 0 (no measurable rate
/// yet). Because out-of-window timestamps are evicted on every query, a
/// stalled line decays toward 0 instead of freezing on its last rate.
class ThroughputMeter {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ThroughputMeter(std::chrono::seconds window = kDefaultWindow)
        : window_(window) {}

    /// Record one completed work unit observed at time `now`.
    void recordCompletion(TimePoint now) { completions_.push_back(now); }

    /// Units-per-hour over the trailing window ending at `now`. Evicts
    /// out-of-window timestamps first, so the rate decays on a stall.
    /// Returns 0 with fewer than two in-window completions.
    [[nodiscard]] double unitsPerHour(TimePoint now) {
        evictOlderThan(now - window_);
        if (completions_.size() < 2) {
            return 0.0;
        }
        const double seconds = std::chrono::duration<double>(
                                   completions_.back() - completions_.front())
                                   .count();
        if (seconds <= 0.0) {
            return 0.0;  // all in the same instant -- no measurable span
        }
        const auto intervals = static_cast<double>(completions_.size() - 1);
        return intervals * kSecondsPerHour / seconds;
    }

    /// Forget all history (new batch loaded / system reset).
    void clear() { completions_.clear(); }

private:
    static constexpr double               kSecondsPerHour = 3600.0;
    static constexpr std::chrono::seconds kDefaultWindow{120};

    void evictOlderThan(TimePoint cutoff) {
        while (!completions_.empty() && completions_.front() < cutoff) {
            completions_.pop_front();
        }
    }

    std::chrono::seconds  window_;
    std::deque<TimePoint> completions_;
};

}  // namespace app::model
