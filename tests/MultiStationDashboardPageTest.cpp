// [utest->req~multistation-002~1]
// Covers REQ-MULTISTATION-002 (Dashboard tab hosts two DashboardPage
// instances side by side, one per station, each with its own presenter).
//
// Smoke + structure test for app::view::MultiStationDashboardPage.
//
// Previously verified only by manual inspection. This asserts the dual-
// pane composition concretely: the page builds a horizontal split with
// exactly two station columns and initialises with two independent
// presenters without crashing. Requires GTK initialised (ViewTestMain).

#include "src/gtk/view/pages/MultiStationDashboardPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/model/MirrorModel.h"
#include "mocks/MockDialogManager.h"

#include <memory>

#include <gtkmm.h>
#include <gtest/gtest.h>

namespace {

using app::DashboardPresenter;
using app::model::MirrorModel;
using app::test::MockDialogManager;
using app::view::MultiStationDashboardPage;

TEST(MultiStationDashboardPageTest, HostsTwoStationColumnsAndTitlesDashboard) {
    MockDialogManager dm;
    MirrorModel primaryModel;
    MirrorModel secondaryModel;
    auto primary   = std::make_shared<DashboardPresenter>(primaryModel);
    auto secondary = std::make_shared<DashboardPresenter>(secondaryModel);

    auto* page = Gtk::make_managed<MultiStationDashboardPage>(dm);
    page->initialize(primary, secondary);

    // The multi-station tab still reads as the single "Dashboard" tab.
    EXPECT_EQ(page->pageTitle(), Glib::ustring("Dashboard"));

    // The page's child is the horizontal split; it must hold exactly two
    // station columns (primary + secondary). This is the structural
    // guarantee REQ-MULTISTATION-002 makes.
    Gtk::Widget* split = page->get_first_child();
    ASSERT_NE(split, nullptr) << "page built no content";
    int columns = 0;
    for (Gtk::Widget* c = split->get_first_child(); c != nullptr;
         c = c->get_next_sibling()) {
        ++columns;
    }
    EXPECT_EQ(columns, 2) << "expected two side-by-side station columns";
}

}  // namespace
