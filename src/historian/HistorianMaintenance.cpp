#include "src/historian/HistorianMaintenance.h"

#include "src/core/LoggerBase.h"

#include <chrono>

namespace app::historian {

namespace {

/// Wall-clock ms since the Unix epoch. Same source the bridge uses
/// when writing rows, so "older than X" comparisons line up.
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

constexpr std::int64_t kMinuteBucketMs = 60'000;
constexpr std::int64_t kHourBucketMs   = 3'600'000;

}  // namespace

HistorianMaintenance::HistorianMaintenance(SqliteHistoryStore& store)
    : HistorianMaintenance(store, Config{}) {}

HistorianMaintenance::HistorianMaintenance(SqliteHistoryStore& store,
                                           Config config)
    : store_(store), config_(config) {}

HistorianMaintenance::~HistorianMaintenance() {
    stop();
}

void HistorianMaintenance::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::jthread([this](std::stop_token st) { loop(st); });
}

void HistorianMaintenance::stop() noexcept {
    if (!running_) return;
    running_ = false;
    // Request stop on the jthread + wake the cv so wait_for returns.
    if (thread_.joinable()) {
        thread_.request_stop();
        {
            const std::scoped_lock lock(cvMutex_);
            cv_.notify_all();
        }
        // jthread destructor would join anyway, but doing it here
        // makes the stop() <-> joined sequence explicit during
        // shutdown ordering (the SqliteHistoryStore& must outlive
        // the worker, so we want join to complete before this
        // function returns).
        thread_.join();
    }
}

void HistorianMaintenance::runOnce() {
    const std::int64_t now = nowMs();

    // Raw -> Minute. olderThanMs = now - rawRetention. Anything that
    // crossed the 1h horizon since last sweep gets folded.
    const std::int64_t rawCutoff = now - config_.rawRetention.count();
    const std::size_t demotedRaw = store_.demoteOlderThan(
        Tier::Raw, Tier::Minute, rawCutoff, kMinuteBucketMs);

    // Minute -> Hour. Same idea; uses the minute-tier retention.
    const std::int64_t minCutoff = now - config_.minuteRetention.count();
    const std::size_t demotedMin = store_.demoteOlderThan(
        Tier::Minute, Tier::Hour, minCutoff, kHourBucketMs);

    if (logger_ != nullptr && (demotedRaw + demotedMin) > 0) {
        logger_->info(
            "Historian maintenance: demoted {} raw -> 1m, {} 1m -> 1h",
            demotedRaw, demotedMin);
    }
}

void HistorianMaintenance::loop(std::stop_token st) {
    // First sweep runs immediately so a long-uptime process that
    // restarts mid-day doesn't wait a full interval before trimming
    // accumulated raw rows.
    runOnce();

    std::unique_lock lk(cvMutex_);
    while (!st.stop_requested()) {
        // wait_for with stop_token returns as soon as request_stop()
        // is called -- C++20 jthread/cv_any integration. The
        // predicate handles spurious wakes.
        cv_.wait_for(lk, st, config_.sweepInterval,
                     [&]{ return st.stop_requested(); });
        if (st.stop_requested()) break;
        // Drop the lock while we hit SQLite so the next stop() doesn't
        // block on a slow sweep -- the cv_ exists only to interrupt
        // the wait, not to serialise the work.
        lk.unlock();
        runOnce();
        lk.lock();
    }
}

}  // namespace app::historian
