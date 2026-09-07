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
}

namespace app::config {
class ConfigManager;
}

namespace app::historian {
class HistoryReader;
}

namespace app::view {

class QtDashboardPage;
class QtProductsPage;
class QtGoodsReceiptPage;
class QtTrendsPage;
class QtHistoryPage;
class QtStatusStrip;
class QtPaletteManager;
class QtSidebar;

/// The Qt application shell: a custom QtSidebar rail beside a QStackedWidget,
/// one stacked page per nav entry (the GTK-sidebar feel on Qt). It owns the
/// page widgets and exposes them so the composition root can attach observers.
class QtMainWindow : public QMainWindow {
public:
    explicit QtMainWindow(DashboardPresenter& dashboardPresenter,
                          ProductsPresenter& productsPresenter,
                          presenter::AlertCenter& alertCenter,
                          presenter::QualityInspectionPresenter& inspectionPresenter,
                          const config::ConfigManager& config,
                          QtPaletteManager& paletteManager,
                          historian::HistoryReader* historyReader = nullptr,
                          std::function<void(const std::string&)>
                              onLanguageChanged = {});
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
    QtStatusStrip*   statusStrip_{nullptr};
};

}  // namespace app::view
