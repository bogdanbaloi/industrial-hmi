#include "src/qt/view/QtMainWindow.h"

#include "src/qt/view/QtDashboardPage.h"

#include <QTabWidget>

namespace app::view {

namespace {
constexpr int kWindowWidth  = 900;
constexpr int kWindowHeight = 640;
}  // namespace

QtMainWindow::QtMainWindow(DashboardPresenter& dashboardPresenter,
                           QWidget* parent)
    : QMainWindow(parent) {
    tabs_ = new QTabWidget(this);

    dashboardPage_ = new QtDashboardPage(dashboardPresenter);
    tabs_->addTab(dashboardPage_, tr("Dashboard"));

    setCentralWidget(tabs_);
    setWindowTitle(tr("Industrial HMI (Qt)"));
    resize(kWindowWidth, kWindowHeight);
}

QtMainWindow::~QtMainWindow() = default;

}  // namespace app::view
