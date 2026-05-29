// [utest->req~quality-002~1]
// Covers REQ-QUALITY-002 (persistent alerts raised + auto-cleared from
// production state) at the real model->presenter->AlertCenter seam.
//
// DashboardPresenterTest exercises the AlertCenter path but drives it by
// hand-invoking a captured MockProductionModel callback. This wires a
// REAL MirrorModel + real DashboardPresenter (DI ctor) + real
// AlertCenter and lets a real state change flow through the presenter's
// own observer dispatch: disabling equipment must raise an alert, and
// re-enabling it must clear the alert automatically.
//
// MirrorModel (non-singleton) keeps the test isolated; presenter is
// declared after the model + AlertCenter so it tears down first.

#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/AlertCenter.h"
#include "src/model/MirrorModel.h"

#include <gtest/gtest.h>

namespace {

using app::DashboardPresenter;
using app::model::MirrorModel;
using app::presenter::AlarmState;
using app::presenter::AlertCenter;

TEST(AlertCenterModelIntegrationTest, EquipmentOfflineRaisesThenRecoveryThenAck) {
    MirrorModel     model;
    AlertCenter     alerts;
    DashboardPresenter presenter(model);
    presenter.setAlertCenter(alerts);
    presenter.initialize();  // subscribes to the model's real signals

    ASSERT_TRUE(alerts.snapshot().empty()) << "no alerts before any event";

    // Real state change: take equipment 0 offline -> presenter's real
    // dispatch raises a keyed alarm (unacknowledged).
    model.setEquipmentEnabled(0, false);
    auto raised = alerts.snapshot();
    ASSERT_EQ(raised.size(), 1u) << "offline equipment must raise an alarm";
    EXPECT_EQ(raised[0].state, AlarmState::UnackActive);

    // Recovery: bring it back online. ISA-18.2 -- an UNACKNOWLEDGED alarm
    // returning to normal does NOT auto-disappear; it becomes RtnUnack and
    // stays visible so the operator can't miss the transient fault.
    model.setEquipmentEnabled(0, true);
    auto recovered = alerts.snapshot();
    ASSERT_EQ(recovered.size(), 1u)
        << "recovered-but-unacked alarm must stay visible";
    EXPECT_EQ(recovered[0].state, AlarmState::RtnUnack);

    // Operator acknowledges the returned alarm -> fully resolved.
    alerts.acknowledge(recovered[0].key);
    EXPECT_TRUE(alerts.snapshot().empty())
        << "acknowledging a returned alarm resolves it";
}

}  // namespace
