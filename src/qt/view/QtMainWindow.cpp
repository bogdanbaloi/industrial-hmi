#include "src/qt/view/QtMainWindow.h"

#include "src/config/ConfigManager.h"
#include "src/qt/view/QtAlertsPage.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtAuditLogPage.h"
#include "src/qt/view/QtGoodsReceiptPage.h"
#include "src/qt/view/QtHistoryPage.h"
#include "src/qt/view/QtIcons.h"
#include "src/qt/view/QtUsersPage.h"
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

#include <utility>

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
                           Context context)
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
    if (context.historyReader != nullptr) {
        historyPage_ = new QtHistoryPage(*context.historyReader);
    }
    // Admin-only pages, mounted only when the composition root supplies their
    // collaborators (Admin session). RBAC lives in UsersPresenter too, so this
    // is the visible half of a defence-in-depth gate.
    if (context.usersPresenter != nullptr) {
        usersPage_ = new QtUsersPage(*context.usersPresenter);
    }
    if (context.auditReader != nullptr) {
        auditLogPage_ = new QtAuditLogPage(*context.auditReader);
    }
    auto* settings    = new QtSettingsPage(
        config, paletteManager, [this](bool fullscreen) {
            if (fullscreen) {
                showFullScreen();
            } else {
                showNormal();
            }
        },
        std::move(context.onLanguageChanged));

    stack_->addWidget(dashboardPage_);    // index 0 -> Overview
    stack_->addWidget(alerts);            // index 1 -> Alerts
    stack_->addWidget(productsPage_);     // index 2 -> Inventory
    stack_->addWidget(goodsReceiptPage_); // index 3 -> Goods receipt
    stack_->addWidget(trendsPage_);       // index 4 -> Trends
    if (historyPage_ != nullptr) {
        stack_->addWidget(historyPage_);  // History (when enabled)
    }
    if (usersPage_ != nullptr) {
        stack_->addWidget(usersPage_);    // Users (admin)
    }
    if (auditLogPage_ != nullptr) {
        stack_->addWidget(auditLogPage_); // Audit log (admin)
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
    if (usersPage_ != nullptr) {
        sidebar_->addItem(tr("Users"), icons::users());
    }
    if (auditLogPage_ != nullptr) {
        sidebar_->addItem(tr("Audit log"), icons::auditLog());
    }
    sidebar_->addItem(tr("Settings"), icons::settings());

    // Wire the sign-out control only for an auth session (callback supplied).
    if (context.onSignOut) {
        sidebar_->enableSignOut(std::move(context.onSignOut));
    }

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

void QtMainWindow::setUserIdentity(const QString& text) {
    sidebar_->setUserText(text);
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
        if (usersPage_ != nullptr) {
            sidebar_->setItemLabel(index++, tr("Users"));
        }
        if (auditLogPage_ != nullptr) {
            sidebar_->setItemLabel(index++, tr("Audit log"));
        }
        sidebar_->setItemLabel(index++, tr("Settings"));
    }
    QMainWindow::changeEvent(event);
}

}  // namespace app::view
