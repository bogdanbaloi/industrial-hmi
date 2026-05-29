// [utest->req~historian-001~1]
// [utest->req~historian-002~1]
// Covers REQ-HISTORIAN-001 (SQLite store),
//        REQ-HISTORIAN-002 (batched writes via the bridge).
//
// INTEGRATION test: HistorianBridge writing into a REAL SqliteHistoryStore
// (in-memory), driven by a real ProductionModel. No FakeHistoryWriter.
//
// HistorianBridgeTest covers the batching logic against a fake writer;
// SqliteHistoryStoreTest covers the store's own write/query. This joins
// the two: a telemetry change on the model must travel through the
// bridge's batch + flush and end up queryable in the real SQLite store.
//
// The model is a MirrorModel (a real ProductionModel whose setters fire
// real observer callbacks) rather than the SimulatedModel singleton, so
// the test is isolated and the bridge's callback dies with the fixture
// instead of dangling on a process-wide singleton.

#include "src/historian/HistorianBridge.h"
#include "src/historian/SqliteHistoryStore.h"
#include "src/historian/HistoryReader.h"
#include "src/model/MirrorModel.h"

#include <chrono>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

namespace {

using app::historian::FieldKind;
using app::historian::HistorianBridge;
using app::historian::QueryRange;
using app::historian::SqliteHistoryStore;
using app::model::MirrorModel;

class HistorianRoundTripIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_ = std::make_unique<SqliteHistoryStore>(
            SqliteHistoryStore::Config{});  // defaults to :memory:
        ASSERT_TRUE(store_->initialize());

        HistorianBridge::Config cfg;
        cfg.maxBatchSize = 1;  // flush every row inline -- no timer needed
        bridge_ = std::make_unique<HistorianBridge>(*store_, model_, cfg);
        bridge_->wire();
    }

    // A window around "now" whose DURATION is under one hour so the
    // store routes the query to the Raw tier -- where the bridge's
    // just-flushed sample lives. A multi-hour/all-time range would
    // route to the coarse 1-minute / 1-hour aggregate tiers, which are
    // empty until the maintenance demotion loop runs.
    static QueryRange recentWindow() {
        const auto nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        constexpr std::int64_t kHalfHourMs = 30 * 60 * 1000;
        return QueryRange{.fromMs = nowMs - kHalfHourMs,
                          .toMs   = nowMs + kHalfHourMs,
                          .limit  = QueryRange::kDefaultLimit};
    }

    // Teardown order: bridge_ declared last -> destroyed first, before
    // the model it subscribed to.
    MirrorModel                       model_;
    std::unique_ptr<SqliteHistoryStore> store_;
    std::unique_ptr<HistorianBridge>    bridge_;
};

TEST_F(HistorianRoundTripIntegrationTest, QualityPassRateIsQueryableAfterFlush) {
    model_.setQualityPassRate(/*checkpointId=*/0, /*rate=*/88.5F);
    bridge_->flush();  // defensive: maxBatchSize 1 already flushed inline

    const auto rows =
        store_->query(FieldKind::QualityPassRate, /*entityId=*/0, recentWindow());

    ASSERT_FALSE(rows.empty()) << "no sample persisted through the bridge";
    EXPECT_EQ(rows.back().field, FieldKind::QualityPassRate);
    EXPECT_EQ(rows.back().entityId, 0U);
    EXPECT_FLOAT_EQ(rows.back().value, 88.5F);
    EXPECT_GE(store_->totalSamples(), 1U);
}

TEST_F(HistorianRoundTripIntegrationTest, EquipmentSupplyLevelRoundTrips) {
    model_.setEquipmentSupplyLevel(/*equipmentId=*/2, /*level=*/63);
    bridge_->flush();

    const auto rows =
        store_->query(FieldKind::EquipmentSupplyLevel, /*entityId=*/2, recentWindow());

    ASSERT_FALSE(rows.empty());
    EXPECT_FLOAT_EQ(rows.back().value, 63.0F);
}

}  // namespace
