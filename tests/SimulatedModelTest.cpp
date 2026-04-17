// Tests for app::model::SimulatedModel
// Covers init, state transitions, signal delivery, tick simulation,
// and equipment enable/disable. No GTK dependency.

#include "src/model/SimulatedModel.h"
#include "src/model/ProductionTypes.h"

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

    // Initialize demo data ONCE — callbacks persist in the singleton so
    // re-initializing per test would fire stale callbacks from previous
    // tests that captured stack-local variables.
    static void SetUpTestSuite() {
        SimulatedModel::instance().initializeDemoData();
    }
};

// ============================================================================
// Initial state after demo data
// ============================================================================

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

// ============================================================================
// State transitions via commands
// ============================================================================

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

// ============================================================================
// Equipment enable/disable
// ============================================================================

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

// ============================================================================
// Signal delivery (callbacks fire on subscribe)
// ============================================================================

TEST_F(SimulatedModelTest, StateCallbackFiresOnTransition) {
    // shared_ptr so the vector outlives the test — callback persists in singleton
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

// ============================================================================
// Tick simulation advances state
// ============================================================================

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
