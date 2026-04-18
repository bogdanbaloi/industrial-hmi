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
#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"
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

// ============================================================================
// AlertCenter integration
//
// The presenter raises a keyed alert on equipment Offline(0)/Error(3) and
// clears it once the equipment reports any non-fault state. Quality works
// the same way but keyed on checkpointId, with Critical/Warning mapping to
// the AlertSeverity enum and Passing clearing the alert.
//
// These tests inject a real AlertCenter (header-only, no deps) and
// inspect its snapshot after firing the captured model callbacks.
// ============================================================================

class DashboardPresenterAlertsTest : public DashboardPresenterTest {
protected:
    void SetUp() override {
        DashboardPresenterTest::SetUp();
        presenter->setAlertCenter(alerts);
    }

    // Fire all five initialize subscriptions, capturing the equipment and
    // quality callbacks for the test bodies to invoke.
    void initializeAndCaptureCallbacks() {
        EXPECT_CALL(model, onEquipmentStatusChanged(_))
            .WillOnce(SaveArg<0>(&equipmentCb_));
        EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
        EXPECT_CALL(model, onQualityCheckpointChanged(_))
            .WillOnce(SaveArg<0>(&qualityCb_));
        EXPECT_CALL(model, onWorkUnitChanged(_)).Times(1);
        EXPECT_CALL(model, onSystemStateChanged(_)).Times(1);
        presenter->initialize();
        ASSERT_TRUE(equipmentCb_);
        ASSERT_TRUE(qualityCb_);
    }

    app::presenter::AlertCenter alerts;
    MockProductionModel::EquipmentCallback equipmentCb_;
    MockProductionModel::QualityCheckpointCallback qualityCb_;
};

TEST_F(DashboardPresenterAlertsTest, EquipmentOfflineRaisesWarningAlert) {
    initializeAndCaptureCallbacks();
    equipmentCb_(EquipmentStatus{7, 0, 0, ""});  // Offline

    const auto snap = alerts.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "equipment-7");
    EXPECT_EQ(snap[0].severity, app::presenter::AlertSeverity::Warning);
    EXPECT_THAT(snap[0].title, ::testing::HasSubstr("7"));
    EXPECT_FALSE(snap[0].message.empty());
}

TEST_F(DashboardPresenterAlertsTest, EquipmentErrorRaisesCriticalAlert) {
    initializeAndCaptureCallbacks();
    equipmentCb_(EquipmentStatus{3, 3, 5, ""});  // Error

    const auto snap = alerts.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "equipment-3");
    EXPECT_EQ(snap[0].severity, app::presenter::AlertSeverity::Critical);
}

TEST_F(DashboardPresenterAlertsTest, EquipmentRecoveryClearsAlert) {
    initializeAndCaptureCallbacks();
    equipmentCb_(EquipmentStatus{1, 0, 0, ""});    // Offline — raise
    ASSERT_EQ(alerts.snapshot().size(), 1u);

    equipmentCb_(EquipmentStatus{1, 1, 85, ""});   // Online — clear
    EXPECT_TRUE(alerts.snapshot().empty());
}

TEST_F(DashboardPresenterAlertsTest, EquipmentProcessingClearsAlert) {
    // Status 2 (Processing) is also a non-fault state — must clear.
    initializeAndCaptureCallbacks();
    equipmentCb_(EquipmentStatus{2, 3, 12, ""});   // Error — raise
    ASSERT_EQ(alerts.snapshot().size(), 1u);

    equipmentCb_(EquipmentStatus{2, 2, 60, ""});   // Processing — clear
    EXPECT_TRUE(alerts.snapshot().empty());
}

TEST_F(DashboardPresenterAlertsTest, EquipmentAlertDedupesByKey) {
    initializeAndCaptureCallbacks();
    equipmentCb_(EquipmentStatus{4, 0, 0, ""});
    equipmentCb_(EquipmentStatus{4, 0, 0, ""});
    equipmentCb_(EquipmentStatus{4, 3, 0, ""});    // severity upgrade, same key

    const auto snap = alerts.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "equipment-4");
    EXPECT_EQ(snap[0].severity, app::presenter::AlertSeverity::Critical);
}

TEST_F(DashboardPresenterAlertsTest, DifferentEquipmentIdsCoexist) {
    initializeAndCaptureCallbacks();
    equipmentCb_(EquipmentStatus{1, 0, 0, ""});
    equipmentCb_(EquipmentStatus{2, 3, 0, ""});
    equipmentCb_(EquipmentStatus{5, 0, 0, ""});
    EXPECT_EQ(alerts.snapshot().size(), 3u);
}

TEST_F(DashboardPresenterAlertsTest, QualityCriticalRaisesCriticalAlert) {
    initializeAndCaptureCallbacks();

    QualityCheckpoint cp{2, "Final", 0, 100, 30, 60.0f, "Coating"};
    EXPECT_CALL(model, getQualityCheckpoint(2u)).WillOnce(Return(cp));

    qualityCb_(cp);

    const auto snap = alerts.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "quality-2");
    EXPECT_EQ(snap[0].severity, app::presenter::AlertSeverity::Critical);
    EXPECT_THAT(snap[0].title, ::testing::HasSubstr("Final"));
    EXPECT_THAT(snap[0].message, ::testing::HasSubstr("60"));  // pass rate
}

TEST_F(DashboardPresenterAlertsTest, QualityWarningRaisesWarningAlert) {
    initializeAndCaptureCallbacks();

    QualityCheckpoint cp{1, "Hardness", 0, 100, 5, 92.0f, "Soft"};
    EXPECT_CALL(model, getQualityCheckpoint(1u)).WillOnce(Return(cp));

    qualityCb_(cp);

    const auto snap = alerts.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "quality-1");
    EXPECT_EQ(snap[0].severity, app::presenter::AlertSeverity::Warning);
}

TEST_F(DashboardPresenterAlertsTest, QualityPassingClearsAlert) {
    initializeAndCaptureCallbacks();

    // First drop below threshold — raise.
    QualityCheckpoint low{0, "Weight Check", 0, 100, 40, 60.0f, "Over"};
    EXPECT_CALL(model, getQualityCheckpoint(0u))
        .WillOnce(Return(low))
        .WillOnce(Return(QualityCheckpoint{0, "Weight Check", 0, 100, 2, 98.0f, ""}));

    qualityCb_(low);
    ASSERT_EQ(alerts.snapshot().size(), 1u);

    // Now recover — clear.
    QualityCheckpoint ok{0, "Weight Check", 0, 100, 2, 98.0f, ""};
    qualityCb_(ok);
    EXPECT_TRUE(alerts.snapshot().empty());
}

TEST_F(DashboardPresenterAlertsTest, QualityAndEquipmentAlertsCoexist) {
    initializeAndCaptureCallbacks();

    equipmentCb_(EquipmentStatus{0, 3, 0, ""});

    QualityCheckpoint cp{1, "Hardness", 0, 100, 5, 92.0f, "Soft"};
    EXPECT_CALL(model, getQualityCheckpoint(1u)).WillOnce(Return(cp));
    qualityCb_(cp);

    EXPECT_EQ(alerts.snapshot().size(), 2u);
}

// When no AlertCenter is injected the presenter must silently skip alert
// bookkeeping — this is the production vs test wiring difference.
TEST_F(DashboardPresenterTest, NoAlertCenterMeansNoCrashOnEquipmentSignal) {
    MockProductionModel::EquipmentCallback cb;
    EXPECT_CALL(model, onEquipmentStatusChanged(_)).WillOnce(SaveArg<0>(&cb));
    EXPECT_CALL(model, onActuatorStatusChanged(_)).Times(1);
    EXPECT_CALL(model, onQualityCheckpointChanged(_)).Times(1);
    EXPECT_CALL(model, onWorkUnitChanged(_)).Times(1);
    EXPECT_CALL(model, onSystemStateChanged(_)).Times(1);
    presenter->initialize();

    // Must not segfault or throw.
    cb(EquipmentStatus{0, 3, 0, ""});
    cb(EquipmentStatus{0, 1, 85, ""});
    SUCCEED();
}
