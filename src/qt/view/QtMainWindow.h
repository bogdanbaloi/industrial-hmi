#pragma once

#include <QMainWindow>

class QStackedWidget;

namespace app {
class DashboardPresenter;
class ProductsPresenter;
}

namespace app::config {
class ConfigManager;
}

namespace app::view {

class QtDashboardPage;
class QtProductsPage;
class QtPaletteManager;
class QtSidebar;

/// The Qt application shell: a custom QtSidebar rail beside a QStackedWidget,
/// one stacked page per nav entry (the GTK-sidebar feel on Qt). It owns the
/// page widgets and exposes them so the composition root can attach observers.
class QtMainWindow : public QMainWindow {
public:
    explicit QtMainWindow(DashboardPresenter& dashboardPresenter,
                          ProductsPresenter& productsPresenter,
                          const config::ConfigManager& config,
                          QtPaletteManager& paletteManager,
                          QWidget* parent = nullptr);
    ~QtMainWindow() override;

    QtMainWindow(const QtMainWindow&)            = delete;
    QtMainWindow& operator=(const QtMainWindow&) = delete;
    QtMainWindow(QtMainWindow&&)                 = delete;
    QtMainWindow& operator=(QtMainWindow&&)      = delete;

    [[nodiscard]] QtDashboardPage* dashboardPage() const { return dashboardPage_; }
    [[nodiscard]] QtProductsPage* productsPage() const { return productsPage_; }

private:
    QtSidebar*       sidebar_{nullptr};
    QStackedWidget*  stack_{nullptr};
    QtDashboardPage* dashboardPage_{nullptr};
    QtProductsPage*  productsPage_{nullptr};
};

}  // namespace app::view
