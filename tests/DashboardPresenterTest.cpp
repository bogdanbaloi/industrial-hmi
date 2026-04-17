// Tests for app::DashboardPresenter
//
// Drives the presenter against a MockProductionModel so we can verify:
//   - User-facing button handlers forward to the right model command
//   - initialize() subscribes to all five model signals
//   - Triggering a captured signal leads to a notification on observers
//     with the correct ViewModel
//   - View-model builders translate raw model state into the right
//     enum/string/numeric output
//
// Each subscription EXPECT_CALL captures the registered callback via
// SaveArg, so the test body can later invoke it as if the model fired.

#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ViewObserver.h"
#include "src/model/ProductionTypes.h"
#include "src/config/config_defaults.h"
#include "mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using app::test::MockProductionModel;
using app::DashboardPresenter;
using app::model::EquipmentStatus;
using app::model::ActuatorStatus;
using app::model::QualityCheckpoint;
using app::model::WorkUnit;
using app::model::SystemState;

namespace {

// Captures the most recent ViewModel emitted on each onXxxChanged hook.
class CapturingObserver : public app::ViewObserver {
public:
    std::optional<app::presenter::WorkUnitViewModel> workUnit;
    std::optional<app::presenter::EquipmentCardViewModel> equipment;
    std::optional<app::presenter::ActuatorCardViewModel> actuator;
    std::optional<app::presenter::QualityCheckpointViewModel> quality;
    std::optional<app::presenter::ControlPanelViewModel> control;

    void onWorkUnitChanged(const app::presenter::WorkUnitViewModel& vm) override {
        workUnit = vm;
    }
    void onEquipmentCardChanged(const app::presenter::EquipmentCardViewModel& vm) override {
        equipment = vm;
    }
    void onActuatorCardChanged(const app::presenter::ActuatorCardViewModel& vm) override {
        actuator = vm;
    }
    void onQualityCheckpointChanged(const app::presenter::QualityCheckpointViewModel& vm) override {
        quality = vm;
    }
    void onControlPanelChanged(const app::presenter::ControlPanelViewModel& vm) override {
        control = vm;
    }
};

}  // namespace

// ============================================================================
// Fixture
// ============================================================================

class DashboardPresenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        presenter = std::make_unique<DashboardPresenter>(model);
        presenter->addObserver(&observer);
    }

    void TearDown() override {
        presenter->removeObserver(&observer);
    }

    MockProductionModel model;
    CapturingObserver observer;
    std::unique_ptr<DashboardPresenter> presenter;
};

// ============================================================================
// User-action handlers - thin command forwards
// ============================================================================

TEST_F(DashboardPresenterTest, OnStartClickedCallsStartProduction) {
    EXPECT_CALL(model, startProduction()).Times(1);
    presenter->onStartClicked();
}

TEST_F(DashboardPresenterTest, OnStopClickedCallsStopProduction) {
    EXPECT_CALL(model, stopProduction()).Times(1);
    presenter->onStopClicked();
}

TEST_F(DashboardPresenterTest, OnResetRestartClickedCallsResetSystem) {
    EXPECT_CALL(model, resetSystem()).Times(1);
    presenter->onResetRestartClicked();
}

TEST_F(DashboardPresenterTest, OnCalibrationClickedCallsStartCalibration) {
    EXPECT_CALL(model, startCalibration()).Times(1);
    presenter->onCalibrationClicked();
}

TEST_F(DashboardPresenterTest, OnEquipmentToggledForwardsIdAndState) {
    EXPECT_CALL(model, setEquipmentEnabled(2u, true)).Times(1);
    presenter->onEquipmentToggled(2, true);

    EXPECT_CALL(model, setEquipmentEnabled(3u, false)).Times(1);
    presenter->onEquipmentToggled(3, false);
}

// ============================================================================
// initialize() - subscribes to every model signal
// ============================================================================

TEST_F(DashboardPresenterTest, InitializeSubscribesToAllFiveModelSignals) {
    EXPECT_CALL(model, onEquipmentStatusChanged(_)).Times(1);
    EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
    EXPECT_CALL(model, onQualityCheckpointChanged(_)).Times(1);
    EXPECT_CALL(model, onWorkUnitChanged(_)).Times(1);
    EXPECT_CALL(model, onSystemStateChanged(_)).Times(1);

    presenter->initialize();
}

// ============================================================================
// Equipment signal -> EquipmentCardViewModel
//
// status mapping the Presenter implements:
//   0 -> Offline / "Not connected" / disabled
//   1 -> Online / "Supply level: 85%" / enabled
//   2 -> Processing / "Supply level: 60%" / enabled
//   3 -> Error / "Low supply (12%)" / disabled
// ============================================================================

class DashboardPresenterEquipmentTest : public DashboardPresenterTest {
protected:
    // Capture the equipment callback registered during initialize() so each
    // test can fire a fake hardware signal.
    void initializeAndCaptureEquipmentCallback() {
        EXPECT_CALL(model, onEquipmentStatusChanged(_))
            .WillOnce(SaveArg<0>(&equipmentCb_));
        EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onQualityCheckpointChanged(_)).Times(1);
        EXPECT_CALL(model, onWorkUnitChanged(_)).Times(1);
        EXPECT_CALL(model, onSystemStateChanged(_)).Times(1);
        presenter->initialize();
        ASSERT_TRUE(equipmentCb_) << "Presenter did not subscribe to equipment signal";
    }

    MockProductionModel::EquipmentCallback equipmentCb_;
};

TEST_F(DashboardPresenterEquipmentTest, OnlineStatusProducesOnlineViewModel) {
    initializeAndCaptureEquipmentCallback();
    equipmentCb_(EquipmentStatus{1, 1, 85, ""});

    ASSERT_TRUE(observer.equipment.has_value());
    EXPECT_EQ(observer.equipment->equipmentId, 1u);
    EXPECT_EQ(observer.equipment->status, app::presenter::EquipmentCardStatus::Online);
    EXPECT_TRUE(observer.equipment->enabled);
    EXPECT_THAT(observer.equipment->consumables, ::testing::HasSubstr("85%"));
}

TEST_F(DashboardPresenterEquipmentTest, OfflineStatusProducesDisabledViewModel) {
    initializeAndCaptureEquipmentCallback();
    equipmentCb_(EquipmentStatus{0, 0, 0, ""});

    ASSERT_TRUE(observer.equipment.has_value());
    EXPECT_EQ(observer.equipment->status, app::presenter::EquipmentCardStatus::Offline);
    EXPECT_FALSE(observer.equipment->enabled);
    EXPECT_EQ(observer.equipment->consumables, "Not connected");
}

TEST_F(DashboardPresenterEquipmentTest, ErrorStatusProducesErrorViewModel) {
    initializeAndCaptureEquipmentCallback();
    equipmentCb_(EquipmentStatus{2, 3, 12, ""});

    ASSERT_TRUE(observer.equipment.has_value());
    EXPECT_EQ(observer.equipment->status, app::presenter::EquipmentCardStatus::Error);
    EXPECT_FALSE(observer.equipment->enabled);
    EXPECT_THAT(observer.equipment->consumables, ::testing::HasSubstr("Low supply"));
}

// ============================================================================
// Quality checkpoint signal -> QualityCheckpointViewModel
//
// Pass-rate threshold mapping (from config_defaults):
//   >= kQualityPassThreshold (95.0)    -> Passing
//   >= kQualityWarningThreshold (90.0) -> Warning
//   <  kQualityWarningThreshold        -> Critical
// ============================================================================

class DashboardPresenterQualityTest : public DashboardPresenterTest {
protected:
    void initializeAndCaptureQualityCallback() {
        EXPECT_CALL(model, onEquipmentStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onQualityCheckpointChanged(_))
            .WillOnce(SaveArg<0>(&qualityCb_));
        EXPECT_CALL(model, onWorkUnitChanged(_)).Times(1);
        EXPECT_CALL(model, onSystemStateChanged(_)).Times(1);
        presenter->initialize();
        ASSERT_TRUE(qualityCb_);
    }

    MockProductionModel::QualityCheckpointCallback qualityCb_;
};

TEST_F(DashboardPresenterQualityTest, HighPassRateMapsToPassing) {
    initializeAndCaptureQualityCallback();

    QualityCheckpoint cp{0, "Weight Check", 0, 100, 5, 98.5f, "n/a"};
    EXPECT_CALL(model, getQualityCheckpoint(0u)).WillOnce(Return(cp));

    qualityCb_(cp);

    ASSERT_TRUE(observer.quality.has_value());
    EXPECT_EQ(observer.quality->status, app::presenter::QualityCheckpointStatus::Passing);
    EXPECT_FLOAT_EQ(observer.quality->passRate, 98.5f);
    EXPECT_EQ(observer.quality->checkpointName, "Weight Check");
}

TEST_F(DashboardPresenterQualityTest, MidPassRateMapsToWarning) {
    initializeAndCaptureQualityCallback();

    QualityCheckpoint cp{1, "Hardness", 0, 100, 5, 92.0f, "Soft"};
    EXPECT_CALL(model, getQualityCheckpoint(1u)).WillOnce(Return(cp));

    qualityCb_(cp);

    ASSERT_TRUE(observer.quality.has_value());
    EXPECT_EQ(observer.quality->status, app::presenter::QualityCheckpointStatus::Warning);
}

TEST_F(DashboardPresenterQualityTest, LowPassRateMapsToCritical) {
    initializeAndCaptureQualityCallback();

    QualityCheckpoint cp{2, "Final", 0, 100, 30, 60.0f, "Coating"};
    EXPECT_CALL(model, getQualityCheckpoint(2u)).WillOnce(Return(cp));

    qualityCb_(cp);

    ASSERT_TRUE(observer.quality.has_value());
    EXPECT_EQ(observer.quality->status, app::presenter::QualityCheckpointStatus::Critical);
}

// ============================================================================
// Work unit signal -> WorkUnitViewModel (progress + status message)
// ============================================================================

class DashboardPresenterWorkUnitTest : public DashboardPresenterTest {
protected:
    void initializeAndCaptureWorkUnitCallback() {
        EXPECT_CALL(model, onEquipmentStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onQualityCheckpointChanged(_)).Times(1);
        EXPECT_CALL(model, onWorkUnitChanged(_)).WillOnce(SaveArg<0>(&workUnitCb_));
        EXPECT_CALL(model, onSystemStateChanged(_)).Times(1);
        presenter->initialize();
        ASSERT_TRUE(workUnitCb_);
    }

    MockProductionModel::WorkUnitCallback workUnitCb_;
};

TEST_F(DashboardPresenterWorkUnitTest, MidProgressShowsProcessingMessage) {
    initializeAndCaptureWorkUnitCallback();

    WorkUnit wu{"WU-1", "PROD-001", "demo batch", 2, 5};
    EXPECT_CALL(model, getWorkUnit()).WillOnce(Return(wu));

    workUnitCb_(wu);

    ASSERT_TRUE(observer.workUnit.has_value());
    EXPECT_EQ(observer.workUnit->workUnitId, "WU-1");
    EXPECT_EQ(observer.workUnit->completedOperations, 2);
    EXPECT_EQ(observer.workUnit->totalOperations, 5);
    EXPECT_FLOAT_EQ(observer.workUnit->progress, 2.0f / 5.0f);
    EXPECT_THAT(observer.workUnit->statusMessage, ::testing::HasSubstr("Processing"));
}

TEST_F(DashboardPresenterWorkUnitTest, FullProgressShowsCompleteMessage) {
    initializeAndCaptureWorkUnitCallback();

    WorkUnit wu{"WU-1", "PROD-001", "demo batch", 5, 5};
    EXPECT_CALL(model, getWorkUnit()).WillOnce(Return(wu));

    workUnitCb_(wu);

    ASSERT_TRUE(observer.workUnit.has_value());
    EXPECT_FLOAT_EQ(observer.workUnit->progress, 1.0f);
    EXPECT_EQ(observer.workUnit->statusMessage, "Complete");
}

// ============================================================================
// System state signal -> ControlPanelViewModel button availability
// ============================================================================

class DashboardPresenterStateTest : public DashboardPresenterTest {
protected:
    void initializeAndCaptureStateCallback() {
        EXPECT_CALL(model, onEquipmentStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onQualityCheckpointChanged(_)).Times(1);
        EXPECT_CALL(model, onWorkUnitChanged(_)).Times(1);
        EXPECT_CALL(model, onSystemStateChanged(_)).WillOnce(SaveArg<0>(&stateCb_));
        presenter->initialize();
        ASSERT_TRUE(stateCb_);
    }

    MockProductionModel::StateCallback stateCb_;
};

TEST_F(DashboardPresenterStateTest, IdleStateAllowsStartAndCalibration) {
    initializeAndCaptureStateCallback();
    EXPECT_CALL(model, getState()).WillOnce(Return(SystemState::IDLE));

    stateCb_(SystemState::IDLE);

    ASSERT_TRUE(observer.control.has_value());
    EXPECT_TRUE(observer.control->startEnabled);
    EXPECT_FALSE(observer.control->stopEnabled);
    EXPECT_TRUE(observer.control->resetRestartEnabled);
    EXPECT_TRUE(observer.control->calibrationEnabled);
    EXPECT_EQ(observer.control->activeButton, app::presenter::ActiveControl::None);
}

TEST_F(DashboardPresenterStateTest, RunningStateOnlyAllowsStop) {
    initializeAndCaptureStateCallback();
    EXPECT_CALL(model, getState()).WillOnce(Return(SystemState::RUNNING));

    stateCb_(SystemState::RUNNING);

    ASSERT_TRUE(observer.control.has_value());
    EXPECT_FALSE(observer.control->startEnabled);
    EXPECT_TRUE(observer.control->stopEnabled);
    EXPECT_FALSE(observer.control->resetRestartEnabled);
    EXPECT_FALSE(observer.control->calibrationEnabled);
    EXPECT_EQ(observer.control->activeButton, app::presenter::ActiveControl::Start);
}

TEST_F(DashboardPresenterStateTest, ErrorStateAllowsOnlyReset) {
    initializeAndCaptureStateCallback();
    EXPECT_CALL(model, getState()).WillOnce(Return(SystemState::ERROR));

    stateCb_(SystemState::ERROR);

    ASSERT_TRUE(observer.control.has_value());
    EXPECT_FALSE(observer.control->startEnabled);
    EXPECT_FALSE(observer.control->stopEnabled);
    EXPECT_TRUE(observer.control->resetRestartEnabled);
    EXPECT_FALSE(observer.control->calibrationEnabled);
}

TEST_F(DashboardPresenterStateTest, CalibrationStateMarksCalibrationActive) {
    initializeAndCaptureStateCallback();
    EXPECT_CALL(model, getState()).WillOnce(Return(SystemState::CALIBRATION));

    stateCb_(SystemState::CALIBRATION);

    ASSERT_TRUE(observer.control.has_value());
    EXPECT_EQ(observer.control->activeButton, app::presenter::ActiveControl::Calibration);
    EXPECT_TRUE(observer.control->stopEnabled);
}
