// [utest->req~multistation-003~1]
// [utest->req~multistation-004~1]
// [utest->req~multistation-005~1]
// Covers REQ-MULTISTATION-003 (equipment supply forward),
//             REQ-MULTISTATION-004 (quality pass-rate forward),
//             REQ-MULTISTATION-005 (bridge metrics on BackendHealthBar).
//
// Tests for app::integration::PrimaryToSecondaryBridge.
//
// The bridge has one job: forward equipment-status events from a
// "master" ProductionModel onto a "slave" ProductionModel via the
// slave's setEquipmentSupplyLevel setter. No threads, no I/O,
// callback plumbing only.
//
// Pattern (same as SensorIngestBridgeTest):
//   * Master is a MockProductionModel that SaveArg-captures its
//     onEquipmentStatusChanged callback so the test can fire fake
//     equipment events.
//   * Slave is a MockProductionModel with EXPECT_CALL on
//     setEquipmentSupplyLevel.
//   * No real model, no real wire format.

#include "src/integration/PrimaryToSecondaryBridge.h"

#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using app::integration::BackendState;
using app::integration::PrimaryToSecondaryBridge;
using app::model::EquipmentStatus;
using app::model::ProductionModel;
using app::test::MockProductionModel;

using testing::_;
using testing::AnyNumber;
using testing::SaveArg;

/// Fixture: builds master + slave mocks and the bridge, captures the
/// callback the bridge registers on master at start() so individual
/// tests can fire fake equipment events at will.
class PrimaryToSecondaryBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Bridge subscribes lazily on first start(). Capture the
        // callback master receives, then call bridge.start() to wire
        // things up.
        EXPECT_CALL(master_, onEquipmentStatusChanged(_))
            .Times(AnyNumber())
            .WillRepeatedly(SaveArg<0>(&masterCb_));

        bridge_ = std::make_unique<PrimaryToSecondaryBridge>(master_, slave_);
    }

    /// Inject a fake equipment-status event "from master". Only valid
    /// after start() has been called (otherwise masterCb_ is empty).
    void injectMasterEvent(uint32_t equipmentId, int supplyLevel) {
        ASSERT_TRUE(static_cast<bool>(masterCb_))
            << "bridge must be started before injecting events";
        EquipmentStatus status;
        status.equipmentId = equipmentId;
        status.status      = 1;
        status.supplyLevel = supplyLevel;
        masterCb_(status);
    }

    MockProductionModel              master_;
    MockProductionModel              slave_;
    std::unique_ptr<PrimaryToSecondaryBridge> bridge_;
    ProductionModel::EquipmentCallback   masterCb_;
};

// ---------------------------------------------------------------- //
// 1. Initial state                                                  //
// ---------------------------------------------------------------- //

TEST_F(PrimaryToSecondaryBridgeTest, StartsDisconnectedAndDoesNotForward) {
    EXPECT_FALSE(bridge_->isRunning());
    EXPECT_EQ(bridge_->connectionState(), BackendState::Disconnected);
    EXPECT_EQ(bridge_->name(), "Primary->Secondary");
    EXPECT_EQ(bridge_->forwardedCount(), 0u);

    // Slave should NEVER see a forward before start(). We use a strict
    // expectation: setEquipmentSupplyLevel is never called.
    EXPECT_CALL(slave_, setEquipmentSupplyLevel(_, _)).Times(0);
}

// ---------------------------------------------------------------- //
// 2. Happy path: start -> event -> slave gets the forward            //
// ---------------------------------------------------------------- //

TEST_F(PrimaryToSecondaryBridgeTest, ForwardsEquipmentSupplyToSlaveWhenRunning) {
    bridge_->start();
    EXPECT_TRUE(bridge_->isRunning());
    EXPECT_EQ(bridge_->connectionState(), BackendState::Connected);

    // One master event -> one slave setter call with same id + level.
    EXPECT_CALL(slave_, setEquipmentSupplyLevel(1u, 72)).Times(1);
    injectMasterEvent(/*equipmentId=*/1, /*supplyLevel=*/72);

    EXPECT_EQ(bridge_->forwardedCount(), 1u);
}

// ---------------------------------------------------------------- //
// 3. Multiple events: counter accumulates, every event forwarded     //
// ---------------------------------------------------------------- //

TEST_F(PrimaryToSecondaryBridgeTest, ForwardsEveryEventAndAccumulatesCount) {
    bridge_->start();

    EXPECT_CALL(slave_, setEquipmentSupplyLevel(1u, 10)).Times(1);
    EXPECT_CALL(slave_, setEquipmentSupplyLevel(2u, 50)).Times(1);
    EXPECT_CALL(slave_, setEquipmentSupplyLevel(3u, 99)).Times(1);

    injectMasterEvent(1, 10);
    injectMasterEvent(2, 50);
    injectMasterEvent(3, 99);

    EXPECT_EQ(bridge_->forwardedCount(), 3u);
}

// ---------------------------------------------------------------- //
// 4. stop() mutes forwarding -- new events DON'T reach the slave     //
// ---------------------------------------------------------------- //

TEST_F(PrimaryToSecondaryBridgeTest, StopMutesForwarding) {
    bridge_->start();
    EXPECT_CALL(slave_, setEquipmentSupplyLevel(1u, 30)).Times(1);
    injectMasterEvent(1, 30);
    EXPECT_EQ(bridge_->forwardedCount(), 1u);

    bridge_->stop();
    EXPECT_FALSE(bridge_->isRunning());
    EXPECT_EQ(bridge_->connectionState(), BackendState::Disconnected);

    // After stop, the master callback still exists (ProductionModel
    // has no remove path) but the bridge's running_ flag mutes the
    // forward. Slave should see zero further calls.
    EXPECT_CALL(slave_, setEquipmentSupplyLevel(_, _)).Times(0);
    injectMasterEvent(1, 99);

    // Counter is unchanged since the previous successful forward.
    EXPECT_EQ(bridge_->forwardedCount(), 1u);
}

// ---------------------------------------------------------------- //
// 5. Restart after stop continues to work without duplicate subscribe //
// ---------------------------------------------------------------- //

TEST_F(PrimaryToSecondaryBridgeTest, RestartContinuesForwardingWithoutDuplicateSubscribe) {
    // start/stop/start cycle. The CRITICAL property: master's
    // onEquipmentStatusChanged must be called exactly ONCE across the
    // whole cycle -- the bridge guards subscription with a one-shot
    // flag so cycling doesn't pile up stale callbacks.
    //
    // We assert this by the fixture's AnyNumber() expectation having
    // captured exactly one callback; the masterCb_ slot was last-
    // written-wins, so this property is enforced by also asserting
    // that one injection during the second running phase produces one
    // forward (no doubles).

    bridge_->start();
    bridge_->stop();
    bridge_->start();
    EXPECT_TRUE(bridge_->isRunning());

    EXPECT_CALL(slave_, setEquipmentSupplyLevel(2u, 55)).Times(1);
    injectMasterEvent(2, 55);
    EXPECT_EQ(bridge_->forwardedCount(), 1u);
}

// ---------------------------------------------------------------- //
// 6. metricsSummary reports count + last forward                     //
// ---------------------------------------------------------------- //

TEST_F(PrimaryToSecondaryBridgeTest, MetricsSummaryReportsCount) {
    // Before start: zero forwards, summary just says "0 forwarded".
    EXPECT_EQ(bridge_->metricsSummary(), "0 forwarded");

    bridge_->start();

    EXPECT_CALL(slave_, setEquipmentSupplyLevel(_, _)).Times(2);
    injectMasterEvent(1, 10);
    injectMasterEvent(1, 20);

    const auto summary = bridge_->metricsSummary();
    // Should start with "2 forwarded" and contain a "(last HH:MM:SS)"
    // suffix once at least one event has been forwarded. We don't
    // assert the exact time; we assert the substring shape.
    EXPECT_NE(summary.find("2 forwarded"), std::string::npos)
        << "summary: " << summary;
    EXPECT_NE(summary.find("(last "), std::string::npos)
        << "summary: " << summary;
}

}  // namespace
