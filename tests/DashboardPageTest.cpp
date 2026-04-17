// Tests for app::view::DashboardPage — View layer.
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

// ============================================================================
// Fixture
// ============================================================================

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

// ============================================================================
// Reset button -> confirm dialog -> presenter
// ============================================================================

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

// ============================================================================
// Calibration button -> confirm dialog -> presenter
// ============================================================================

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

// ============================================================================
// Direct button handlers forward to presenter without dialog
// ============================================================================

TEST_F(DashboardPageTest, StartButtonCallsPresenterStart) {
    EXPECT_CALL(mockModel_, startProduction()).Times(1);
    callStart(page_);
}

TEST_F(DashboardPageTest, StopButtonCallsPresenterStop) {
    EXPECT_CALL(mockModel_, stopProduction()).Times(1);
    callStop(page_);
}
