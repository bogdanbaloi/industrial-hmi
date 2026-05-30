// [utest->req~state-001~1]
// [utest->req~state-002~1]
// [utest->req~state-003~1]
// Covers REQ-STATE-001 (formal SystemState transition table),
//        REQ-STATE-002 (Phase 2: invalid transitions are dropped),
//        REQ-STATE-003 (Phase 3: safe-state Fault -> ERROR, only Reset
//        leaves it, lastFaultReason exposed + cleared on Reset).
//
// SystemStateMachine wraps a Boost.SML transition table for the
// production-line lifecycle. These tests pin the contract end-to-end:
// the observer fires only on real transitions (no phantom refresh),
// invalid combinations are silently dropped, and the SM locks out
// every command except Reset once it enters ERROR.

#include "src/model/SystemStateMachine.h"

#include <gtest/gtest.h>

using app::model::SystemState;
using app::model::SystemStateMachine;

TEST(SystemStateMachineTest, StartsInIdle) {
    SystemStateMachine sm;
    EXPECT_EQ(sm.state(), SystemState::IDLE);
}

TEST(SystemStateMachineTest, IdleToRunningOnStart) {
    SystemStateMachine sm;
    sm.start();
    EXPECT_EQ(sm.state(), SystemState::RUNNING);
}

TEST(SystemStateMachineTest, RunningToIdleOnStop) {
    SystemStateMachine sm;
    sm.start();
    sm.stop();
    EXPECT_EQ(sm.state(), SystemState::IDLE);
}

TEST(SystemStateMachineTest, IdleToCalibrationOnCalibrate) {
    SystemStateMachine sm;
    sm.calibrate();
    EXPECT_EQ(sm.state(), SystemState::CALIBRATION);
}

TEST(SystemStateMachineTest, CalibrationToIdleOnCalibrationDone) {
    SystemStateMachine sm;
    sm.calibrate();
    sm.calibrationDone();
    EXPECT_EQ(sm.state(), SystemState::IDLE);
}

TEST(SystemStateMachineTest, ResetReturnsToIdleFromEveryActiveState) {
    SystemStateMachine sm;

    sm.start();
    sm.reset();
    EXPECT_EQ(sm.state(), SystemState::IDLE);

    sm.calibrate();
    sm.reset();
    EXPECT_EQ(sm.state(), SystemState::IDLE);
}

TEST(SystemStateMachineTest, RunningPlusStartStaysRunningWithoutPhantomNotify) {
    // Idempotent commands must not spam observers -- the FSM observer is
    // a per-transition signal, not a per-dispatch one.
    SystemStateMachine sm;
    int          notifyCount = 0;
    SystemState  last        = SystemState::IDLE;
    sm.onStateChanged([&](SystemState s) {
        ++notifyCount;
        last = s;
    });

    sm.start();  // IDLE -> RUNNING: one notify
    EXPECT_EQ(notifyCount, 1);
    EXPECT_EQ(last, SystemState::RUNNING);

    sm.start();  // RUNNING + Start = RUNNING (no transition): no notify
    sm.start();
    EXPECT_EQ(notifyCount, 1);

    sm.stop();   // RUNNING -> IDLE: another notify
    EXPECT_EQ(notifyCount, 2);
    EXPECT_EQ(last, SystemState::IDLE);
}

// Phase 2 -- invalid-transition drops (REQ-STATE-002)

TEST(SystemStateMachineTest, CalibrateFromRunningIsDropped) {
    // Phase 1 permissively allowed Calibrate from RUNNING; Phase 2
    // requires Stop first. The SM must silently stay in RUNNING.
    SystemStateMachine sm;
    sm.start();
    sm.calibrate();
    EXPECT_EQ(sm.state(), SystemState::RUNNING);
}

TEST(SystemStateMachineTest, StartFromCalibrationIsDropped) {
    SystemStateMachine sm;
    sm.calibrate();
    sm.start();  // Phase 2: must Reset / CalibrationDone first
    EXPECT_EQ(sm.state(), SystemState::CALIBRATION);
}

TEST(SystemStateMachineTest, StartFromErrorIsDropped) {
    SystemStateMachine sm;
    sm.fault("test fault");
    sm.start();
    EXPECT_EQ(sm.state(), SystemState::ERROR);
}

// Phase 3 -- safe-state on Fault (REQ-STATE-003)

TEST(SystemStateMachineTest, FaultFromIdleEntersErrorAndExposesReason) {
    SystemStateMachine sm;
    sm.fault("equipment-0 over-temperature");
    EXPECT_EQ(sm.state(), SystemState::ERROR);
    EXPECT_EQ(sm.lastFaultReason(), "equipment-0 over-temperature");
}

TEST(SystemStateMachineTest, FaultFromRunningEntersError) {
    SystemStateMachine sm;
    sm.start();
    sm.fault("disk full");
    EXPECT_EQ(sm.state(), SystemState::ERROR);
    EXPECT_EQ(sm.lastFaultReason(), "disk full");
}

TEST(SystemStateMachineTest, FaultFromCalibrationEntersError) {
    SystemStateMachine sm;
    sm.calibrate();
    sm.fault("calibration sensor offline");
    EXPECT_EQ(sm.state(), SystemState::ERROR);
}

TEST(SystemStateMachineTest, ErrorLocksOutAllCommandsExceptReset) {
    SystemStateMachine sm;
    sm.fault("disk full");

    sm.start();             EXPECT_EQ(sm.state(), SystemState::ERROR);
    sm.stop();              EXPECT_EQ(sm.state(), SystemState::ERROR);
    sm.calibrate();         EXPECT_EQ(sm.state(), SystemState::ERROR);
    sm.calibrationDone();   EXPECT_EQ(sm.state(), SystemState::ERROR);

    sm.reset();
    EXPECT_EQ(sm.state(), SystemState::IDLE);
}

TEST(SystemStateMachineTest, ResetClearsLastFaultReason) {
    SystemStateMachine sm;
    sm.fault("brownout");
    ASSERT_FALSE(sm.lastFaultReason().empty());
    sm.reset();
    EXPECT_TRUE(sm.lastFaultReason().empty());
}
