// [utest->req~multistation-002~1]
// Covers REQ-MULTISTATION-002 (the secondary's model holds state +
// fans out observer callbacks; it is the passive mirror behind the
// second dashboard).
//
// Unit tests for app::model::MirrorModel in isolation.
//
// MirrorModel is exercised as a real fixture by the bridge / historian /
// dashboard integration tests, but those drive only the happy path. This
// pins the contract those tests rely on but never assert: setter
// clamping, out-of-range id drop, observer dispatch, command-local-only
// behaviour, and system-state de-duplication.

#include "src/model/MirrorModel.h"
#include "src/model/ProductionTypes.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using app::model::EquipmentStatus;
using app::model::MirrorModel;
using app::model::Product;
using app::model::QualityCheckpoint;
using app::model::Recipe;
using app::model::SystemState;

TEST(MirrorModelTest, DefaultSeedPopulatesNamedCheckpointsAndIdleState) {
    MirrorModel model;  // defaults: 3 equipment + 3 quality
    EXPECT_EQ(model.getState(), SystemState::IDLE);
    // Seeded checkpoints carry names before any bridge event arrives,
    // so the secondary dashboard renders cards from the first frame.
    EXPECT_FALSE(model.getQualityCheckpoint(0).name.empty());
    EXPECT_FALSE(model.getQualityCheckpoint(2).name.empty());
}

TEST(MirrorModelTest, EquipmentSupplyLevelIsClampedToZeroHundred) {
    MirrorModel model;
    EquipmentStatus last;
    model.onEquipmentStatusChanged([&](const EquipmentStatus& s) { last = s; });

    model.setEquipmentSupplyLevel(0, -50);
    EXPECT_EQ(last.supplyLevel, 0) << "below-range supply must clamp to 0";

    model.setEquipmentSupplyLevel(0, 250);
    EXPECT_EQ(last.supplyLevel, 100) << "above-range supply must clamp to 100";
}

TEST(MirrorModelTest, OutOfRangeEquipmentIdIsSilentlyDropped) {
    MirrorModel model;  // ids 0..2 exist
    int calls = 0;
    model.onEquipmentStatusChanged([&](const EquipmentStatus&) { ++calls; });

    model.setEquipmentSupplyLevel(999, 50);
    EXPECT_EQ(calls, 0) << "an unknown id must not fire an observer";
}

TEST(MirrorModelTest, QualityPassRateClampsAndUnknownIdDropped) {
    MirrorModel model;
    int calls = 0;
    model.onQualityCheckpointChanged(
        [&](const QualityCheckpoint&) { ++calls; });

    model.setQualityPassRate(1, 130.0F);
    EXPECT_FLOAT_EQ(model.getQualityCheckpoint(1).passRate, 100.0F);
    EXPECT_EQ(calls, 1);

    model.setQualityPassRate(999, 50.0F);  // unknown id
    EXPECT_EQ(calls, 1) << "unknown checkpoint id must not fire";
}

TEST(MirrorModelTest, CommandsUpdateStateAndFireObserver) {
    MirrorModel model;
    SystemState last = SystemState::IDLE;
    model.onSystemStateChanged([&](SystemState s) { last = s; });

    model.startProduction();
    EXPECT_EQ(model.getState(), SystemState::RUNNING);
    EXPECT_EQ(last, SystemState::RUNNING);

    model.startCalibration();
    EXPECT_EQ(model.getState(), SystemState::CALIBRATION);
    EXPECT_EQ(last, SystemState::CALIBRATION);
}

TEST(MirrorModelTest, SystemStateChangeIsDeduplicated) {
    MirrorModel model;
    int calls = 0;
    model.onSystemStateChanged([&](SystemState) { ++calls; });

    model.startProduction();  // IDLE -> RUNNING : fires
    model.startProduction();  // RUNNING -> RUNNING : no-op, must not fire
    EXPECT_EQ(calls, 1) << "redundant state set must not re-notify";
}

TEST(MirrorModelTest, LoadProductIsNoOpOnTheSecondary) {
    MirrorModel model;
    const auto before = model.getWorkUnit().productId;

    Product product;
    product.productCode = "PROD-001";
    Recipe recipe;
    recipe.productCode    = "PROD-001";
    recipe.totalOperations = 9;
    model.loadProduct(product, recipe);

    // Secondary mirrors the primary; it must not adopt a recipe locally.
    EXPECT_EQ(model.getWorkUnit().productId, before);
}

}  // namespace
