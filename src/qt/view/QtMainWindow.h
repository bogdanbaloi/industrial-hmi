#pragma once

#include <QMainWindow>

#include <functional>
#include <string>

class QStackedWidget;
class QEvent;

namespace app {
class DashboardPresenter;
class ProductsPresenter;
}

namespace app::presenter {
class AlertCenter;
class QualityInspectionPresenter;
class UsersPresenter;
}

namespace app::config {
class ConfigManager;
}

namespace app::historian {
class HistoryReader;
}

namespace app::auth {
class Session;
class AuditLogger;
}

namespace app::view {

class QtDashboardPage;
class QtProductsPage;
class QtGoodsReceiptPage;
class QtTrendsPage;
class QtHistoryPage;
class QtUsersPage;
class QtAuditLogPage;
class QtStatusStrip;
class QtPaletteManager;
class QtSidebar;

/// The Qt application shell: a custom QtSidebar rail beside a QStackedWidget,
/// one stacked page per nav entry (the GTK-sidebar feel on Qt). It owns the
/// page widgets and exposes them so the composition root can attach observers.
class QtMainWindow : public QMainWindow {
public:
    /// Optional / cross-cutting shell wiring, grouped so the constructor stays
    /// within the parameter budget as the frontend grows. All fields are
    /// optional: a null historyReader hides the History page, a null session
    /// keeps the default sidebar footer, an empty callback disables the language
    /// picker's effect.
    struct Context {
        historian::HistoryReader*                historyReader{nullptr};
        std::function<void(const std::string&)>  onLanguageChanged;
        std::function<void()>                    onSignOut;
        std::function<void()>                    onChangePassword;
        auth::Session*                           session{nullptr};
        // Admin-only pages: the composition root passes these non-null only for
        // an Admin session, so they mount for admins and stay hidden otherwise.
        presenter::UsersPresenter*               usersPresenter{nullptr};
        auth::AuditLogger*                       auditReader{nullptr};
    };

    QtMainWindow(DashboardPresenter& dashboardPresenter,
                 ProductsPresenter& productsPresenter,
                 presenter::AlertCenter& alertCenter,
                 presenter::QualityInspectionPresenter& inspectionPresenter,
                 const config::ConfigManager& config,
                 QtPaletteManager& paletteManager,
                 Context context);
    ~QtMainWindow() override;

    QtMainWindow(const QtMainWindow&)            = delete;
    QtMainWindow& operator=(const QtMainWindow&) = delete;
    QtMainWindow(QtMainWindow&&)                 = delete;
    QtMainWindow& operator=(QtMainWindow&&)      = delete;

    [[nodiscard]] QtDashboardPage* dashboardPage() const { return dashboardPage_; }
    [[nodiscard]] QtProductsPage* productsPage() const { return productsPage_; }
    [[nodiscard]] QtStatusStrip* statusStrip() const { return statusStrip_; }
    [[nodiscard]] QtGoodsReceiptPage* goodsReceiptPage() const {
        return goodsReceiptPage_;
    }
    [[nodiscard]] QtTrendsPage* trendsPage() const { return trendsPage_; }

    /// Set the active-alert count shown as a badge on the Alerts nav entry.
    void setAlertsBadge(int count);

    /// Replace the sidebar footer with the signed-in user's identity (called by
    /// the composition root after login and on every Session change).
    void setUserIdentity(const QString& text);

protected:
    /// Retranslate the window title and the nav labels (whose source strings
    /// this shell owns) on a live language change.
    void changeEvent(QEvent* event) override;

private:
    QtSidebar*       sidebar_{nullptr};
    QStackedWidget*  stack_{nullptr};
    QtDashboardPage* dashboardPage_{nullptr};
    QtProductsPage*  productsPage_{nullptr};
    QtGoodsReceiptPage* goodsReceiptPage_{nullptr};
    QtTrendsPage*    trendsPage_{nullptr};
    QtHistoryPage*   historyPage_{nullptr};
    QtUsersPage*     usersPage_{nullptr};
    QtAuditLogPage*  auditLogPage_{nullptr};
    QtStatusStrip*   statusStrip_{nullptr};
};

}  // namespace app::view
