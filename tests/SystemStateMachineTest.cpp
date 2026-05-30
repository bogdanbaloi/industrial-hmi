// [utest->req~state-001~1]
// Covers REQ-STATE-001 (formal SystemState transition table).
//
// SystemStateMachine wraps a Boost.SML transition table for the
// production-line lifecycle. These tests pin the Phase 1 contract: the
// transitions currently in use map to the same target states the legacy
// code produced, AND the observer fires only on real transitions (no
// phantom refresh on an idempotent command). Guards / invalid-rejection /
// Fault arrive in later phases and will land alongside their own tests.

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

TEST(SystemStateMachineTest, CalibrateFromRunningEntersCalibration) {
    SystemStateMachine sm;
    sm.start();
    sm.calibrate();
    EXPECT_EQ(sm.state(), SystemState::CALIBRATION);
}
