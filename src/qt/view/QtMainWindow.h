#pragma once

#include <QMainWindow>

class QTabWidget;

namespace app {
class DashboardPresenter;
}

namespace app::view {

class QtDashboardPage;

/// The Qt application shell: a QMainWindow hosting a QTabWidget, one tab per
/// page. Slice 4 holds the dashboard; later slices add Settings and Products as
/// further tabs. It owns the page widgets through the tab widget (Qt parent
/// ownership) and exposes them so the composition root can attach observers.
class QtMainWindow : public QMainWindow {
public:
    explicit QtMainWindow(DashboardPresenter& dashboardPresenter,
                          QWidget* parent = nullptr);
    ~QtMainWindow() override;

    QtMainWindow(const QtMainWindow&)            = delete;
    QtMainWindow& operator=(const QtMainWindow&) = delete;
    QtMainWindow(QtMainWindow&&)                 = delete;
    QtMainWindow& operator=(QtMainWindow&&)      = delete;

    [[nodiscard]] QtDashboardPage* dashboardPage() const { return dashboardPage_; }

private:
    QTabWidget*      tabs_{nullptr};
    QtDashboardPage* dashboardPage_{nullptr};
};

}  // namespace app::view
