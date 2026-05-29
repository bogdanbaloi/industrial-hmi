// [utest->req~dashboard-001~1]
// [utest->req~dashboard-002~1]
// [utest->req~dashboard-005~1]
// [utest->req~dashboard-006~1]
// Covers REQ-DASHBOARD-001 (equipment cards reflect live state),
//             REQ-DASHBOARD-002 (quality checkpoint cards),
//             REQ-DASHBOARD-005 (control panel role gating).
//
// Tests for app::view::DashboardPage -- View layer.
//
// Verifies that button click handlers forward to the right presenter
// method and display the expected confirmation dialogs via the injected
// DialogManager. The page is constructed with a MockDialogManager and a
// real DashboardPresenter backed by a MockProductionModel.
//
// Requires GTK initialised (see ViewTestMain.cpp).

#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/presenter/DashboardPresenter.h"
#include "mocks/MockDialogManager.h"
#include "mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::SaveArg;
using ::testing::Return;

// Fixture

class DashboardPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        presenter_ = std::make_shared<app::DashboardPresenter>(mockModel_);

        // DashboardPage buildUI() loads CSS from assets/styles/*.css relative
        // to CWD. CTest's WORKING_DIRECTORY is set to the source root.
        page_ = Gtk::make_managed<app::view::DashboardPage>(mockDM_);
        page_->initialize(presenter_);
    }

    // C++ friendship is not inherited by TEST_F subclasses. These static
    // helpers live on the friend class itself so they CAN access privates.
    static void callReset(app::view::DashboardPage* p) { p->onResetButtonClicked(); }
    static void callCalibration(app::view::DashboardPage* p) { p->onCalibrationButtonClicked(); }
    static void callStart(app::view::DashboardPage* p) { p->onStartButtonClicked(); }
    static void callStop(app::view::DashboardPage* p) { p->onStopButtonClicked(); }

    app::test::MockProductionModel mockModel_;
    app::test::MockDialogManager mockDM_;
    std::shared_ptr<app::DashboardPresenter> presenter_;
    app::view::DashboardPage* page_{nullptr};
};

// Reset button -> confirm dialog -> presenter

TEST_F(DashboardPageTest, ResetButtonShowsConfirmDialog) {
    std::function<void(bool)> capturedCb;

    EXPECT_CALL(mockDM_, showConfirmAsync(
            HasSubstr("Reset"), _, _, _))
        .WillOnce(SaveArg<2>(&capturedCb));

    callReset(page_);

    ASSERT_TRUE(capturedCb) << "showConfirmAsync callback not captured";
}

TEST_F(DashboardPageTest, ResetConfirmedCallsPresenterReset) {
    std::function<void(bool)> capturedCb;
    EXPECT_CALL(mockDM_, showConfirmAsync(_, _, _, _))
        .WillOnce(SaveArg<2>(&capturedCb));

    callReset(page_);
    ASSERT_TRUE(capturedCb);

    // Simulate user clicking OK
    EXPECT_CALL(mockModel_, resetSystem()).Times(1);
    capturedCb(true);
}

TEST_F(DashboardPageTest, ResetCancelledDoesNotCallPresenter) {
    std::function<void(bool)> capturedCb;
    EXPECT_CALL(mockDM_, showConfirmAsync(_, _, _, _))
        .WillOnce(SaveArg<2>(&capturedCb));

    callReset(page_);
    ASSERT_TRUE(capturedCb);

    // Simulate user clicking Cancel
    EXPECT_CALL(mockModel_, resetSystem()).Times(0);
    capturedCb(false);
}

// Calibration button -> confirm dialog -> presenter

TEST_F(DashboardPageTest, CalibrationButtonShowsConfirmDialog) {
    EXPECT_CALL(mockDM_, showConfirmAsync(
            HasSubstr("Calibration"), _, _, _))
        .Times(1);

    callCalibration(page_);
}

TEST_F(DashboardPageTest, CalibrationConfirmedCallsPresenterCalibration) {
    std::function<void(bool)> capturedCb;
    EXPECT_CALL(mockDM_, showConfirmAsync(_, _, _, _))
        .WillOnce(SaveArg<2>(&capturedCb));

    callCalibration(page_);
    ASSERT_TRUE(capturedCb);

    EXPECT_CALL(mockModel_, startCalibration()).Times(1);
    capturedCb(true);
}

// Direct button handlers forward to presenter without dialog

TEST_F(DashboardPageTest, StartButtonCallsPresenterStart) {
    EXPECT_CALL(mockModel_, startProduction()).Times(1);
    callStart(page_);
}

TEST_F(DashboardPageTest, StopButtonCallsPresenterStop) {
    EXPECT_CALL(mockModel_, stopProduction()).Times(1);
    callStop(page_);
}

// Layout-budget regression guard (REQ-DASHBOARD-006).
//
// In multi-station mode two DashboardPage panes sit side by side
// beside the sidebar. On the tightest supported viewport -- 1536
// logical px (a 1920 panel at 125% OS scale) -- the sidebar needs
// ~340px for its content (the "Primary->Secondary" I/O row + status
// pill), leaving (1536 - 340) / 2 = 598px per pane. If a later change
// (a new KPI card, a wider control button, a roomier gauge) pushes the
// compact pane's MINIMUM width past this budget, the two panes plus the
// sidebar overflow the window and the sidebar clips on the right.
//
// This test makes that failure loud and automatic instead of relying
// on someone spotting a clipped sidebar in a screenshot.
TEST_F(DashboardPageTest, CompactPaneFitsMultiStationWidthBudget) {
    // Multi-station applies both the CSS class (drives .dashboard-compact
    // rules) and the C++ compaction in setCompact().
    page_->add_css_class("dashboard-compact");
    page_->setCompact(true);

    int minW = 0;
    int natW = 0;
    int ignoreBaselineMin = 0;
    int ignoreBaselineNat = 0;
    page_->measure(Gtk::Orientation::HORIZONTAL, -1,
                   minW, natW, ignoreBaselineMin, ignoreBaselineNat);

    constexpr int kCompactPaneWidthBudgetPx = 600;
    RecordProperty("compact_pane_min_width_px", minW);
    EXPECT_LE(minW, kCompactPaneWidthBudgetPx)
        << "Compact dashboard pane minimum width " << minW
        << "px exceeds the multi-station budget of "
        << kCompactPaneWidthBudgetPx << "px. Two panes + the sidebar "
        << "overflow a 1536 logical-px viewport (1920 @ 125% scale) "
        << "and the sidebar clips.";

    // Sanity floor: guard against a false pass where the stylesheet
    // failed to load (CWD wrong), which would drop every CSS min-width
    // and report an unrealistically small pane. A real compact pane is
    // a few hundred px wide.
    EXPECT_GE(minW, 300)
        << "Compact pane min width " << minW << "px is implausibly small "
        << "-- the dashboard stylesheet probably did not load (check the "
        << "test working directory).";
}
