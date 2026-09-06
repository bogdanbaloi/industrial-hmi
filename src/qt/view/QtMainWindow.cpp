#include "src/qt/view/QtMainWindow.h"

#include "src/config/ConfigManager.h"
#include "src/qt/view/QtAlertsPage.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtLogPanel.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtSettingsPage.h"
#include "src/qt/view/QtSidebar.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace app::view {

namespace {
constexpr int kWindowWidth  = 940;
constexpr int kWindowHeight = 660;
}  // namespace

QtMainWindow::QtMainWindow(DashboardPresenter& dashboardPresenter,
                           ProductsPresenter& productsPresenter,
                           presenter::AlertCenter& alertCenter,
                           const config::ConfigManager& config,
                           QtPaletteManager& paletteManager, QWidget* parent)
    : QMainWindow(parent) {
    auto* central = new QWidget(this);
    auto* outer   = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Top: sidebar rail beside the stacked pages.
    auto* content = new QWidget(central);
    auto* row     = new QHBoxLayout(content);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    stack_         = new QStackedWidget(content);
    dashboardPage_ = new QtDashboardPage(dashboardPresenter);
    productsPage_  = new QtProductsPage(productsPresenter);
    auto* alerts   = new QtAlertsPage(alertCenter);
    auto* settings = new QtSettingsPage(config, paletteManager);

    stack_->addWidget(dashboardPage_);  // index 0 -> Overview
    stack_->addWidget(alerts);          // index 1 -> Alerts
    stack_->addWidget(productsPage_);   // index 2 -> Inventory
    stack_->addWidget(settings);        // index 3 -> Settings

    sidebar_ = new QtSidebar(
        [this](int index) { stack_->setCurrentIndex(index); }, content);
    sidebar_->addItem(tr("Overview"));
    sidebar_->addItem(tr("Alerts"));
    sidebar_->addItem(tr("Inventory"));
    sidebar_->addItem(tr("Settings"));

    row->addWidget(sidebar_);
    row->addWidget(stack_, 1);

    outer->addWidget(content, 1);

    // Bottom: live log panel tailing the log file (the GTK log-panel analog).
    auto* logPanel = new QtLogPanel(
        QString::fromStdString(config.getLogFilePath()), central);
    outer->addWidget(logPanel);

    setCentralWidget(central);
    sidebar_->select(0);

    setWindowTitle(tr("Industrial HMI (Qt)"));
    resize(kWindowWidth, kWindowHeight);
}

QtMainWindow::~QtMainWindow() = default;

}  // namespace app::view
