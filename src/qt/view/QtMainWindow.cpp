#include "src/qt/view/QtMainWindow.h"

#include "src/config/ConfigManager.h"
#include "src/qt/view/QtAlertsPage.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtGoodsReceiptPage.h"
#include "src/qt/view/QtHistoryPage.h"
#include "src/qt/view/QtIcons.h"
#include "src/qt/view/QtLogPanel.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtSettingsPage.h"
#include "src/qt/view/QtSidebar.h"
#include "src/qt/view/QtStatusStrip.h"
#include "src/qt/view/QtTrendsPage.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace app::view {

namespace {
// Match the GTK frontend's kiosk scale (1920x1080 target). The window manager
// clamps this to the screen when it is smaller.
constexpr int kWindowWidth  = 1920;
constexpr int kWindowHeight = 1080;
}  // namespace

QtMainWindow::QtMainWindow(DashboardPresenter& dashboardPresenter,
                           ProductsPresenter& productsPresenter,
                           presenter::AlertCenter& alertCenter,
                           presenter::QualityInspectionPresenter& inspectionPresenter,
                           const config::ConfigManager& config,
                           QtPaletteManager& paletteManager,
                           historian::HistoryReader* historyReader,
                           std::function<void(const std::string&)>
                               onLanguageChanged)
    : QMainWindow(nullptr) {
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
    auto* alerts      = new QtAlertsPage(alertCenter);
    goodsReceiptPage_ = new QtGoodsReceiptPage(inspectionPresenter);
    trendsPage_       = new QtTrendsPage();
    // Persisted-historian page: mounted only when the composition root opened
    // the store (degraded historian -> no History tab, same policy as GTK).
    if (historyReader != nullptr) {
        historyPage_ = new QtHistoryPage(*historyReader);
    }
    auto* settings    = new QtSettingsPage(
        config, paletteManager, [this](bool fullscreen) {
            if (fullscreen) {
                showFullScreen();
            } else {
                showNormal();
            }
        },
        std::move(onLanguageChanged));

    stack_->addWidget(dashboardPage_);    // index 0 -> Overview
    stack_->addWidget(alerts);            // index 1 -> Alerts
    stack_->addWidget(productsPage_);     // index 2 -> Inventory
    stack_->addWidget(goodsReceiptPage_); // index 3 -> Goods receipt
    stack_->addWidget(trendsPage_);       // index 4 -> Trends
    if (historyPage_ != nullptr) {
        stack_->addWidget(historyPage_);  // index 5 -> History (when enabled)
    }
    stack_->addWidget(settings);          // Settings (last)

    sidebar_ = new QtSidebar(
        [this](int index) { stack_->setCurrentIndex(index); }, content);
    sidebar_->addItem(tr("Overview"), icons::overview());
    sidebar_->addItem(tr("Alerts"), icons::alerts());
    sidebar_->addItem(tr("Inventory"), icons::inventory());
    sidebar_->addItem(tr("Goods receipt"), icons::goodsReceipt());
    sidebar_->addItem(tr("Trends"), icons::trends());
    if (historyPage_ != nullptr) {
        sidebar_->addItem(tr("History"), icons::history());
    }
    sidebar_->addItem(tr("Settings"), icons::settings());

    row->addWidget(sidebar_);
    row->addWidget(stack_, 1);

    outer->addWidget(content, 1);

    // Always-visible status strip (system state + backend health + clock), the
    // Qt home for glanceable connectivity instead of a nav page.
    statusStrip_ = new QtStatusStrip(central);
    outer->addWidget(statusStrip_);

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

void QtMainWindow::setAlertsBadge(int count) {
    constexpr int kNavAlerts = 1;  // matches the addItem order above
    sidebar_->setBadge(kNavAlerts, count);
}

void QtMainWindow::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        setWindowTitle(tr("Industrial HMI (Qt)"));
        // Re-apply the nav labels in the exact order they were added, skipping
        // History when it is not mounted so the indices stay aligned.
        int index = 0;
        sidebar_->setItemLabel(index++, tr("Overview"));
        sidebar_->setItemLabel(index++, tr("Alerts"));
        sidebar_->setItemLabel(index++, tr("Inventory"));
        sidebar_->setItemLabel(index++, tr("Goods receipt"));
        sidebar_->setItemLabel(index++, tr("Trends"));
        if (historyPage_ != nullptr) {
            sidebar_->setItemLabel(index++, tr("History"));
        }
        sidebar_->setItemLabel(index++, tr("Settings"));
    }
    QMainWindow::changeEvent(event);
}

}  // namespace app::view
