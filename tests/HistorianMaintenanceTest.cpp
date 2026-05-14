// Tests for HistorianMaintenance.
//
// We exercise the policy directly via `runOnce()` so the assertions
// stay deterministic -- no `sleep` for the periodic loop, no race on
// the sweep interval, no flake on slow CI runners. The thread-spawning
// path (`start()` / `stop()`) is smoke-tested separately so we still
// verify the jthread / stop-token plumbing doesn't deadlock at
// shutdown.

#include "src/historian/HistorianMaintenance.h"

#include "src/historian/HistoryRecord.h"
#include "src/historian/SqliteHistoryStore.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

namespace {

using app::historian::FieldKind;
using app::historian::HistorianMaintenance;
using app::historian::HistoryRecord;
using app::historian::SqliteHistoryStore;
using app::historian::Tier;

std::unique_ptr<SqliteHistoryStore> makeStore() {
    auto store = std::make_unique<SqliteHistoryStore>(
        SqliteHistoryStore::Config{.dbPath = ":memory:"});
    EXPECT_TRUE(store->initialize());
    return store;
}

}  // namespace

TEST(HistorianMaintenanceTest, RunOnceDemotesAgedRawIntoMinute) {
    auto store = makeStore();

    // Two raw rows with timestamps far in the past (epoch ms ~ year
    // 1970) so they're guaranteed older than the default 1 h retention.
    store->write(std::array<HistoryRecord, 2>{
        HistoryRecord{1000, FieldKind::QualityPassRate, 0, 50.0F},
        HistoryRecord{2000, FieldKind::QualityPassRate, 0, 60.0F}});

    // Set minute retention to "effectively forever" so the second
    // demote pass (1m -> 1h) finds nothing eligible and the 1m row
    // we expect to see actually stays in the minute tier. Without
    // this guard the row would cascade straight through to the hour
    // tier because its synthetic ts is decades old.
    HistorianMaintenance::Config cfg;
    cfg.minuteRetention = std::chrono::hours{24 * 365 * 100};
    HistorianMaintenance worker(*store, cfg);
    worker.runOnce();

    EXPECT_EQ(store->rowCount(Tier::Raw),    0U)
        << "rows older than retention should drain out of raw";
    EXPECT_EQ(store->rowCount(Tier::Minute), 1U)
        << "two raw rows in same bucket fold into one minute row";
}

TEST(HistorianMaintenanceTest, FreshRowsAreNotDemoted) {
    auto store = makeStore();

    // Row "now" -- well inside the 1 h retention window. Worker must
    // leave it alone so the dashboard's "last hour" view stays
    // fine-grained.
    const std::int64_t now = std::chrono::duration_cast<
        std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    store->write(std::array<HistoryRecord, 1>{
        HistoryRecord{now, FieldKind::QualityPassRate, 0, 99.0F}});

    HistorianMaintenance worker(*store);
    worker.runOnce();

    EXPECT_EQ(store->rowCount(Tier::Raw),    1U);
    EXPECT_EQ(store->rowCount(Tier::Minute), 0U);
}

TEST(HistorianMaintenanceTest, ChainedDemotionRawToMinuteToHour) {
    auto store = makeStore();

    // Stage a row VERY old (>24 h ago) so it goes raw->1m on the
    // first runOnce, then 1m->1h on the next.
    constexpr std::int64_t kAncientMs = 1'000;
    store->write(std::array<HistoryRecord, 1>{
        HistoryRecord{kAncientMs, FieldKind::QualityPassRate, 0, 42.0F}});

    HistorianMaintenance worker(*store);
    worker.runOnce();
    worker.runOnce();   // second sweep promotes the 1m row to 1h

    EXPECT_EQ(store->rowCount(Tier::Raw),    0U);
    EXPECT_EQ(store->rowCount(Tier::Minute), 0U);
    EXPECT_EQ(store->rowCount(Tier::Hour),   1U);
}

TEST(HistorianMaintenanceTest, StartStopJoinsThreadWithoutDeadlock) {
    auto store = makeStore();
    HistorianMaintenance::Config cfg;
    cfg.sweepInterval = std::chrono::milliseconds{100};
    HistorianMaintenance worker(*store, cfg);

    worker.start();
    // Give the loop a chance to run runOnce() at least twice. The
    // assertion below is on stop() returning promptly, not on the
    // demotion side effect (the store is empty).
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    const auto before = std::chrono::steady_clock::now();
    worker.stop();
    const auto elapsed = std::chrono::steady_clock::now() - before;
    EXPECT_LT(elapsed, std::chrono::milliseconds{500})
        << "stop() must wake the cv promptly, not wait for the sweep";
}

TEST(HistorianMaintenanceTest, RunOnceOnEmptyStoreIsNoop) {
    auto store = makeStore();
    HistorianMaintenance worker(*store);
    worker.runOnce();
    EXPECT_EQ(store->rowCount(Tier::Raw),    0U);
    EXPECT_EQ(store->rowCount(Tier::Minute), 0U);
    EXPECT_EQ(store->rowCount(Tier::Hour),   0U);
}
