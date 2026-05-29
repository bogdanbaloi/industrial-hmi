// [utest->req~multistation-003~1]
// [utest->req~multistation-004~1]
// Covers REQ-MULTISTATION-003 (equipment supply forward),
//        REQ-MULTISTATION-004 (quality pass-rate forward).
//
// INTEGRATION test for app::integration::PrimaryToSecondaryBridge.
//
// Unlike PrimaryToSecondaryBridgeTest (which drives the bridge with two
// MockProductionModels and EXPECT_CALLs), this wires the bridge between
// two REAL ProductionModel instances and asserts on their real state.
// No mocks: a change on the primary must travel through the bridge's
// real observer plumbing and land on the secondary's real state.
//
// This is the test that would have caught the original 0-based vs
// 1-based equipment-id mismatch -- a bug invisible to the mock test
// because mocks just record the call, they don't model the secondary's
// id-keyed storage.
//
// Both sides use MirrorModel rather than the SimulatedModel singleton:
//   * MirrorModel is a real production ProductionModel (it IS the
//     secondary in real deployments), so its setters fire real observer
//     callbacks -- exactly what the bridge consumes.
//   * It is NOT a singleton, so each test gets fresh, isolated models.
//     Using SimulatedModel::instance() as the primary would leave the
//     bridge's callback registered on the process-wide singleton after
//     the bridge is destroyed (ProductionModel has no unsubscribe), and
//     that dangling callback would crash unrelated tests.

#include "src/integration/PrimaryToSecondaryBridge.h"
#include "src/model/MirrorModel.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using app::integration::BackendState;
using app::integration::PrimaryToSecondaryBridge;
using app::model::EquipmentStatus;
using app::model::MirrorModel;

class BridgeIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge_ =
            std::make_unique<PrimaryToSecondaryBridge>(primary_, secondary_);
        bridge_->start();
    }

    // Declaration order matters for safe teardown: bridge_ is declared
    // last so it is destroyed FIRST (members tear down in reverse). The
    // bridge's callback stays registered on primary_ (no unsubscribe
    // path by design), but primary_ outlives the bridge and never
    // notifies during its own destruction -- so the dangling callback
    // is never invoked.
    MirrorModel                               primary_;
    MirrorModel                               secondary_;
    std::unique_ptr<PrimaryToSecondaryBridge> bridge_;
};

TEST_F(BridgeIntegrationTest, EquipmentSupplyForwardsAtMatchingZeroBasedId) {
    EquipmentStatus received;
    int calls = 0;
    secondary_.onEquipmentStatusChanged(
        [&](const EquipmentStatus& s) { received = s; ++calls; });

    // Drive the primary the way an ingest bridge / sim tick would.
    primary_.setEquipmentSupplyLevel(/*equipmentId=*/0, /*level=*/77);

    EXPECT_GE(calls, 1) << "secondary observer never fired -- forward lost";
    EXPECT_EQ(received.equipmentId, 0U) << "0-based id must be preserved";
    EXPECT_EQ(received.supplyLevel, 77);
    EXPECT_GE(bridge_->forwardedCount(), 1U);
}

TEST_F(BridgeIntegrationTest, QualityPassRateLandsOnSecondaryRealState) {
    primary_.setQualityPassRate(/*checkpointId=*/1, /*rate=*/88.5F);

    // Assert on the secondary's REAL stored state, not a mock call.
    EXPECT_FLOAT_EQ(secondary_.getQualityCheckpoint(1).passRate, 88.5F);
}

TEST_F(BridgeIntegrationTest, StoppedBridgeStopsForwarding) {
    bridge_->stop();
    EXPECT_FALSE(bridge_->isRunning());
    EXPECT_EQ(bridge_->connectionState(), BackendState::Disconnected);

    int calls = 0;
    secondary_.onEquipmentStatusChanged(
        [&](const EquipmentStatus&) { ++calls; });

    const auto before = bridge_->forwardedCount();
    primary_.setEquipmentSupplyLevel(2, 50);

    EXPECT_EQ(bridge_->forwardedCount(), before)
        << "a stopped bridge must not forward";
    EXPECT_EQ(calls, 0);
}

}  // namespace
