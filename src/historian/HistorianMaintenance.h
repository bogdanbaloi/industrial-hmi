#pragma once

#include "src/historian/SqliteHistoryStore.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace app::core {
class Logger;
}

namespace app::historian {

/// Background worker that drives tier demotion on the
/// SqliteHistoryStore.
///
/// Owns one `std::jthread` that sleeps on a `condition_variable_any`
/// keyed to its stop token, so `stop()` (and the destructor) return
/// within ms rather than the full sweep interval. Each tick the
/// worker calls `store_.demoteOlderThan(...)` twice -- raw -> minute,
/// then minute -> hour -- with retention thresholds taken from the
/// Config. No work is done at the store level beyond what those two
/// calls accomplish; the maintenance class is pure cadence + policy.
///
/// SOLID:
///   * S -- one job: time the demotion calls. No SQL, no schema,
///     no UI. The store does the actual work.
///   * O -- new tier (e.g. Hour -> Day) = a third demote call in the
///     tick body. No signature change, no consumer breakage.
///   * D -- depends on the concrete store today (the demotion API is
///     instance-specific); a future interface split would let
///     alternative backends plug in.
///
/// Why a dedicated thread (vs a Glib timer): the worker must run in
/// both front-ends. Glib timers run on the GTK main loop, which the
/// console binary doesn't have. A `std::jthread` works identically
/// in both binaries -- zero front-end coupling.
class HistorianMaintenance {
public:
    /// Default tier retention thresholds. Named constants instead of
    /// inline literals because clang-tidy flags `24` in the member
    /// initialiser as a magic number, and a single point of truth
    /// also keeps the worker / tests / config defaults aligned.
    static constexpr int kDefaultMinuteRetentionHours = 24;

    struct Config {
        /// How often the worker sweeps. 60 s matches the minute bucket
        /// granularity -- there's no point demoting raw rows more often
        /// than a single bucket can fill. Longer than 60 s starves
        /// the rolling retention guarantee.
        std::chrono::milliseconds sweepInterval{std::chrono::minutes{1}};

        /// Raw rows older than this age get demoted into the minute
        /// tier. Default 1 h = the documented "live data" window the
        /// History page's "Last hour" range covers.
        std::chrono::milliseconds rawRetention{std::chrono::hours{1}};

        /// Minute rows older than this age get demoted into the hour
        /// tier. Default 24 h = the "Last 24h" range.
        std::chrono::milliseconds minuteRetention{
            std::chrono::hours{kDefaultMinuteRetentionHours}};
    };

    explicit HistorianMaintenance(SqliteHistoryStore& store);
    HistorianMaintenance(SqliteHistoryStore& store, Config config);
    ~HistorianMaintenance();

    HistorianMaintenance(const HistorianMaintenance&)            = delete;
    HistorianMaintenance& operator=(const HistorianMaintenance&) = delete;
    HistorianMaintenance(HistorianMaintenance&&)                 = delete;
    HistorianMaintenance& operator=(HistorianMaintenance&&)      = delete;

    void setLogger(app::core::Logger& logger) { logger_ = &logger; }

    /// Spawn the worker thread. Returns immediately. Safe to call
    /// once per instance; subsequent calls are no-ops.
    void start();

    /// Stop the worker. Idempotent. Safe from the destructor.
    void stop() noexcept;

    /// Run one sweep on the caller's thread. Exposed so the
    /// composition root can force a tick at shutdown (drains
    /// whatever was buffered between the last periodic tick and
    /// process exit), and so unit tests can exercise the policy
    /// without spinning a thread.
    void runOnce();

private:
    void loop(std::stop_token stop);

    SqliteHistoryStore&       store_;
    Config                    config_;
    app::core::Logger*        logger_{nullptr};

    std::jthread              thread_;
    std::mutex                cvMutex_;
    std::condition_variable_any cv_;
    bool                      running_{false};
};

}  // namespace app::historian
