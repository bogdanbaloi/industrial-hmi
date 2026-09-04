#include "src/qt/view/QtMainWindow.h"

#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtSettingsPage.h"

#include <QTabWidget>

namespace app::view {

namespace {
constexpr int kWindowWidth  = 900;
constexpr int kWindowHeight = 640;
}  // namespace

QtMainWindow::QtMainWindow(DashboardPresenter& dashboardPresenter,
                           ProductsPresenter& productsPresenter,
                           const config::ConfigManager& config, QWidget* parent)
    : QMainWindow(parent) {
    tabs_ = new QTabWidget(this);

    dashboardPage_ = new QtDashboardPage(dashboardPresenter);
    tabs_->addTab(dashboardPage_, tr("Dashboard"));

    productsPage_ = new QtProductsPage(productsPresenter);
    tabs_->addTab(productsPage_, tr("Products"));

    tabs_->addTab(new QtSettingsPage(config), tr("Settings"));

    setCentralWidget(tabs_);
    setWindowTitle(tr("Industrial HMI (Qt)"));
    resize(kWindowWidth, kWindowHeight);
}

QtMainWindow::~QtMainWindow() = default;

}  // namespace app::view
