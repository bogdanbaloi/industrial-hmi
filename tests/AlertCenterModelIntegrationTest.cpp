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
using app::presenter::AlertCenter;

TEST(AlertCenterModelIntegrationTest, EquipmentOfflineRaisesThenRecoveryClears) {
    MirrorModel     model;
    AlertCenter     alerts;
    DashboardPresenter presenter(model);
    presenter.setAlertCenter(alerts);
    presenter.initialize();  // subscribes to the model's real signals

    ASSERT_TRUE(alerts.snapshot().empty()) << "no alerts before any event";

    // Real state change: take equipment 0 offline -> presenter's real
    // dispatch raises a keyed alert.
    model.setEquipmentEnabled(0, false);
    EXPECT_FALSE(alerts.snapshot().empty())
        << "offline equipment must raise an alert";

    // Recovery: bring it back online -> the alert auto-clears.
    model.setEquipmentEnabled(0, true);
    EXPECT_TRUE(alerts.snapshot().empty())
        << "recovered equipment must clear its alert";
}

}  // namespace
