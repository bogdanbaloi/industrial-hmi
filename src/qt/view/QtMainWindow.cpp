#include "src/qt/view/QtMainWindow.h"

#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtSettingsPage.h"
#include "src/qt/view/QtSidebar.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QWidget>

namespace app::view {

namespace {
constexpr int kWindowWidth  = 940;
constexpr int kWindowHeight = 660;
}  // namespace

QtMainWindow::QtMainWindow(DashboardPresenter& dashboardPresenter,
                           ProductsPresenter& productsPresenter,
                           const config::ConfigManager& config,
                           QtPaletteManager& paletteManager, QWidget* parent)
    : QMainWindow(parent) {
    auto* central = new QWidget(this);
    auto* row     = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // Pages, stacked. The sidebar selects which one is shown.
    stack_         = new QStackedWidget(central);
    dashboardPage_ = new QtDashboardPage(dashboardPresenter);
    productsPage_  = new QtProductsPage(productsPresenter);
    auto* settings = new QtSettingsPage(config, paletteManager);

    stack_->addWidget(dashboardPage_);  // index 0 -> Overview
    stack_->addWidget(productsPage_);   // index 1 -> Inventory
    stack_->addWidget(settings);        // index 2 -> Settings

    sidebar_ = new QtSidebar(
        [this](int index) { stack_->setCurrentIndex(index); }, central);
    sidebar_->addItem(tr("Overview"));
    sidebar_->addItem(tr("Inventory"));
    sidebar_->addItem(tr("Settings"));

    row->addWidget(sidebar_);
    row->addWidget(stack_, 1);

    setCentralWidget(central);
    sidebar_->select(0);

    setWindowTitle(tr("Industrial HMI (Qt)"));
    resize(kWindowWidth, kWindowHeight);
}

QtMainWindow::~QtMainWindow() = default;

}  // namespace app::view
