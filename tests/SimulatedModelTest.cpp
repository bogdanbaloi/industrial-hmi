// [utest->req~products-003~1]
// Covers REQ-PRODUCTS-003 (load product recipe onto the line).
//
// Tests for app::model::SimulatedModel
// Covers init, state transitions, signal delivery, tick simulation,
// and equipment enable/disable. No GTK dependency.

#include "src/model/SimulatedModel.h"
#include "src/model/ProductionTypes.h"
#include "src/model/Product.h"
#include "src/model/Recipe.h"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <vector>

using app::model::SimulatedModel;
using app::model::SystemState;
using app::model::EquipmentStatus;
using app::model::QualityCheckpoint;
using app::model::WorkUnit;

class SimulatedModelTest : public ::testing::Test {
protected:
    SimulatedModel& model() { return SimulatedModel::instance(); }

    // Initialize demo data ONCE -- callbacks persist in the singleton so
    // re-initializing per test would fire stale callbacks from previous
    // tests that captured stack-local variables.
    static void SetUpTestSuite() {
        SimulatedModel::instance().initializeDemoData();
    }
};

// Initial state after demo data

TEST_F(SimulatedModelTest, InitialStateIsIdle) {
    EXPECT_EQ(model().getState(), SystemState::IDLE);
}

TEST_F(SimulatedModelTest, InitialWorkUnitHasExpectedFields) {
    auto wu = model().getWorkUnit();
    EXPECT_EQ(wu.workUnitId, "WU-2024-001234");
    EXPECT_EQ(wu.productId, "TAB-200");
    EXPECT_EQ(wu.totalOperations, 5);
}

TEST_F(SimulatedModelTest, InitialQualityCheckpointsExist) {
    auto cp0 = model().getQualityCheckpoint(0);
    EXPECT_EQ(cp0.name, "Weight Check");
    EXPECT_GT(cp0.unitsInspected, 0);

    auto cp1 = model().getQualityCheckpoint(1);
    EXPECT_EQ(cp1.name, "Hardness Test");

    auto cp2 = model().getQualityCheckpoint(2);
    EXPECT_EQ(cp2.name, "Final Inspection");
}

// State transitions via commands

TEST_F(SimulatedModelTest, StartProductionSetsRunning) {
    model().startProduction();
    EXPECT_EQ(model().getState(), SystemState::RUNNING);
}

TEST_F(SimulatedModelTest, StopProductionSetsIdle) {
    model().startProduction();
    model().stopProduction();
    EXPECT_EQ(model().getState(), SystemState::IDLE);
}

TEST_F(SimulatedModelTest, ResetSystemSetsIdleAndClearsProgress) {
    model().startProduction();
    model().resetSystem();
    EXPECT_EQ(model().getState(), SystemState::IDLE);
    EXPECT_EQ(model().getWorkUnit().completedOperations, 0);
}

TEST_F(SimulatedModelTest, StartCalibrationSetsCalibration) {
    model().startCalibration();
    EXPECT_EQ(model().getState(), SystemState::CALIBRATION);
}

// Equipment enable/disable

TEST_F(SimulatedModelTest, SetEquipmentEnabledChangesStatus) {
    auto captured = std::make_shared<EquipmentStatus>();
    model().onEquipmentStatusChanged([captured](const EquipmentStatus& es) {
        if (es.equipmentId == 0) *captured = es;
    });
    model().setEquipmentEnabled(0, true);
    EXPECT_EQ(captured->status, 1);  // online
}

TEST_F(SimulatedModelTest, SetEquipmentDisabledSetsOffline) {
    auto captured = std::make_shared<EquipmentStatus>();
    model().onEquipmentStatusChanged([captured](const EquipmentStatus& es) {
        if (es.equipmentId == 1) *captured = es;
    });
    model().setEquipmentEnabled(1, false);
    EXPECT_EQ(captured->status, 0);  // offline
}

// Signal delivery (callbacks fire on subscribe)

TEST_F(SimulatedModelTest, StateCallbackFiresOnTransition) {
    // shared_ptr so the vector outlives the test -- callback persists in singleton
    auto states = std::make_shared<std::vector<SystemState>>();
    model().onSystemStateChanged([states](SystemState s) { states->push_back(s); });

    model().startProduction();
    model().stopProduction();

    ASSERT_GE(states->size(), 2u);
    // Last two entries (other callbacks from earlier tests may have fired too)
    auto sz = states->size();
    EXPECT_EQ((*states)[sz - 2], SystemState::RUNNING);
    EXPECT_EQ((*states)[sz - 1], SystemState::IDLE);
}

TEST_F(SimulatedModelTest, WorkUnitCallbackFiresOnReset) {
    auto callCount = std::make_shared<int>(0);
    model().onWorkUnitChanged([callCount](const WorkUnit&) { ++(*callCount); });

    model().resetSystem();
    EXPECT_GE(*callCount, 1);
}

TEST_F(SimulatedModelTest, QualityCallbackFiresOnTick) {
    auto callCount = std::make_shared<int>(0);
    model().onQualityCheckpointChanged([callCount](const QualityCheckpoint&) { ++(*callCount); });

    model().tickSimulation();
    EXPECT_GE(*callCount, 3);
}

// Tick simulation advances state

TEST_F(SimulatedModelTest, TickAdvancesWorkUnitProgress) {
    auto before = model().getWorkUnit().completedOperations;
    model().tickSimulation();
    auto after = model().getWorkUnit().completedOperations;
    // Should advance (or wrap to 0 if was at max)
    EXPECT_NE(before, after);
}

TEST_F(SimulatedModelTest, TickKeepsPassRateInValidRange) {
    for (int i = 0; i < 10; ++i) model().tickSimulation();
    auto rate = model().getQualityCheckpoint(0).passRate;
    EXPECT_GE(rate, 85.0f);
    EXPECT_LE(rate, 100.0f);
}

TEST_F(SimulatedModelTest, TickIncreasesUnitsInspected) {
    auto before = model().getQualityCheckpoint(0).unitsInspected;
    model().tickSimulation();
    auto after = model().getQualityCheckpoint(0).unitsInspected;
    EXPECT_GT(after, before);
}

// ===== Analog inbound setters (A3) ===============================
//
// Callback captures use shared_ptr to outlive the test scope. The
// model singleton keeps every registered callback for the rest of
// the suite -- a stack-captured value would dangle as soon as the
// test returns and corrupt the next test. Same pattern as the
// pre-A3 SetEquipmentEnabled* tests above.

TEST_F(SimulatedModelTest, SetEquipmentSupplyLevelUpdatesField) {
    auto captured = std::make_shared<EquipmentStatus>();
    model().onEquipmentStatusChanged(
        [captured](const EquipmentStatus& es) {
            if (es.equipmentId == 0U) *captured = es;
        });
    model().setEquipmentSupplyLevel(0, 73);
    EXPECT_EQ(captured->equipmentId, 0U);
    EXPECT_EQ(captured->supplyLevel, 73);
}

TEST_F(SimulatedModelTest, SetEquipmentSupplyLevelClampsAbove100) {
    auto captured = std::make_shared<EquipmentStatus>();
    model().onEquipmentStatusChanged(
        [captured](const EquipmentStatus& es) {
            if (es.equipmentId == 1U) *captured = es;
        });
    model().setEquipmentSupplyLevel(1, 500);
    EXPECT_EQ(captured->supplyLevel, 100);
}

TEST_F(SimulatedModelTest, SetEquipmentSupplyLevelClampsBelowZero) {
    auto captured = std::make_shared<EquipmentStatus>();
    model().onEquipmentStatusChanged(
        [captured](const EquipmentStatus& es) {
            if (es.equipmentId == 1U) *captured = es;
        });
    model().setEquipmentSupplyLevel(1, -42);
    EXPECT_EQ(captured->supplyLevel, 0);
}

TEST_F(SimulatedModelTest, SetEquipmentSupplyLevelDropsOutOfRangeId) {
    // Out-of-range ids are silent no-ops; just confirm no crash.
    model().setEquipmentSupplyLevel(99, 50);
    SUCCEED();
}

TEST_F(SimulatedModelTest, SetQualityPassRateUpdatesAndIsSticky) {
    // After the setter, repeated tickSimulation() must NOT drift the
    // value -- the bridge has taken ownership.
    model().setQualityPassRate(0, 91.5f);
    EXPECT_FLOAT_EQ(model().getQualityCheckpoint(0).passRate, 91.5f);

    for (int i = 0; i < 20; ++i) model().tickSimulation();
    EXPECT_FLOAT_EQ(model().getQualityCheckpoint(0).passRate, 91.5f)
        << "tickSimulation drifted an externally-set passRate";
}

TEST_F(SimulatedModelTest, SetQualityPassRateClampsAbove100) {
    model().setQualityPassRate(1, 150.0f);
    EXPECT_FLOAT_EQ(model().getQualityCheckpoint(1).passRate, 100.0f);
}

TEST_F(SimulatedModelTest, SetQualityPassRateClampsBelowMin) {
    model().setQualityPassRate(2, 10.0f);  // below kQualityRateMin = 85
    EXPECT_FLOAT_EQ(model().getQualityCheckpoint(2).passRate, 85.0f);
}

TEST_F(SimulatedModelTest, SetQualityPassRateDropsUnknownId) {
    // Unknown ids must not crash and must not poison the override
    // set; subsequent legitimate tick must still drift other ids.
    model().setQualityPassRate(999, 50.0f);
    SUCCEED();
}

// Note: a "non-overridden checkpoint still drifts" assertion isn't
// included here because SimulatedModel is a singleton -- preceding
// tests in this suite set passRate on every checkpoint id, marking
// all of them as externally driven. The sticky-after-setter test
// above proves the dual contract (overridden ids stay frozen);
// SensorIngestBridgeTest / ModbusIngestBridgeTest cover the drift
// path on fresh per-test instances.

// loadProduct (REQ-PRODUCTS-003) -- recipe drives the active work unit
// and per-checkpoint targets. Self-contained: loadProduct sets every
// asserted field explicitly, so it does not depend on test ordering
// against the shared singleton.

TEST_F(SimulatedModelTest, LoadProductSetsWorkUnitAndCheckpointTargets) {
    app::model::Product product;
    product.id = 7;
    product.productCode = "PROD-007";
    product.name = "Tablet X";

    app::model::Recipe recipe;
    recipe.productCode = "PROD-007";
    recipe.totalOperations = 6;
    recipe.checkpointTargets = {
        {"Weight Check", 99.0f},
        {"Hardness Test", 96.5f},
    };

    model().loadProduct(product, recipe);

    auto wu = model().getWorkUnit();
    EXPECT_EQ(wu.productId, "PROD-007");
    EXPECT_EQ(wu.totalOperations, 6);
    EXPECT_EQ(wu.completedOperations, 0);          // fresh batch
    EXPECT_EQ(wu.workUnitId.rfind("WU-", 0), 0u);  // generated id

    // Targets applied by name (not position).
    EXPECT_FLOAT_EQ(model().getQualityCheckpoint(0).passRateTarget, 99.0f);
    EXPECT_FLOAT_EQ(model().getQualityCheckpoint(1).passRateTarget, 96.5f);
}

TEST_F(SimulatedModelTest, LoadProductGeneratesDistinctWorkUnitIds) {
    app::model::Product product;
    product.productCode = "PROD-008";
    product.name = "Tablet Y";
    app::model::Recipe recipe;
    recipe.totalOperations = 5;

    model().loadProduct(product, recipe);
    const auto first = model().getWorkUnit().workUnitId;
    model().loadProduct(product, recipe);
    const auto second = model().getWorkUnit().workUnitId;

    EXPECT_NE(first, second);
}
