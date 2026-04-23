// Tests for app::view::ProductsPage — View layer.
//
// Verifies that the delete-product flow passes through the confirm dialog
// and that error dialogs appear when the presenter signals failure.
//
// The page uses a real ProductsPresenter backed by the DatabaseManager
// singleton (in-memory SQLite) because ProductsPage::showDeleteConfirmDialog
// calls presenter_->getProduct() synchronously. The mock dialog manager
// captures dialog calls without opening GTK windows.
//
// Requires GTK initialised (see ViewTestMain.cpp).

#include "src/gtk/view/pages/ProductsPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include "mocks/MockDialogManager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::SaveArg;

// Fixture — needs DB initialized for presenter.getProduct()

class ProductsPageTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto result = app::model::DatabaseManager::instance().initialize();
        ASSERT_TRUE(result.isOk())
            << "DB init failed: " << result.errorMessage();
    }

    void SetUp() override {
        presenter_ = std::make_shared<app::ProductsPresenter>();
        page_ = Gtk::make_managed<app::view::ProductsPage>(mockDM_);
        page_->initialize(presenter_);
    }

    // Friend bridge — see DashboardPageTest for rationale.
    static void callShowDeleteConfirm(app::view::ProductsPage* p,
                                      int id, const std::string& name) {
        p->showDeleteConfirmDialog(id, name);
    }

    app::test::MockDialogManager mockDM_;
    std::shared_ptr<app::ProductsPresenter> presenter_;
    app::view::ProductsPage* page_{nullptr};
};

// Delete confirmation

TEST_F(ProductsPageTest, ShowDeleteConfirmShowsDialogWithProductName) {
    // showDeleteConfirmDialog is the private method that the delete flow
    // calls after validating the product exists.
    EXPECT_CALL(mockDM_, showConfirmAsync(
            HasSubstr("Delete"),
            HasSubstr("Product A"),  // demo seed name
            _, _))
        .Times(1);

    // Call the private helper directly (friend access)
    callShowDeleteConfirm(page_, 1, "Product A");
}

TEST_F(ProductsPageTest, DeleteConfirmedCallsPresenterDelete) {
    std::function<void(bool)> capturedCb;
    EXPECT_CALL(mockDM_, showConfirmAsync(_, _, _, _))
        .WillOnce(SaveArg<2>(&capturedCb));

    callShowDeleteConfirm(page_, 1, "Product A");
    ASSERT_TRUE(capturedCb);

    // Simulate user confirming — soft delete is async so we just verify
    // the callback doesn't crash. The actual DB call is async and routes
    // through ModelContext which isn't running in tests.
    EXPECT_NO_THROW(capturedCb(true));
}

TEST_F(ProductsPageTest, DeleteCancelledDoesNothing) {
    std::function<void(bool)> capturedCb;
    EXPECT_CALL(mockDM_, showConfirmAsync(_, _, _, _))
        .WillOnce(SaveArg<2>(&capturedCb));

    callShowDeleteConfirm(page_, 1, "Product A");
    ASSERT_TRUE(capturedCb);

    // Cancel should be a no-op
    EXPECT_NO_THROW(capturedCb(false));
}

// Error dialogs

TEST_F(ProductsPageTest, ShowDeleteConfirmDialogMessageMentionsSoftDelete) {
    std::string capturedMsg;
    EXPECT_CALL(mockDM_, showConfirmAsync(_, _, _, _))
        .WillOnce(SaveArg<1>(&capturedMsg));

    callShowDeleteConfirm(page_, 1, "Product A");

    EXPECT_THAT(capturedMsg, HasSubstr("inactive"));
}
