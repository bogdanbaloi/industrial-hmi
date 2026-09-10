#pragma once

#include <QWidget>

#include <memory>

class QEvent;

namespace app {
class DashboardPresenter;
}

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtMultiStationPage;
}

namespace app::view {

class QtDashboardPage;

/// Multi-station view: two full QtDashboardPage panes side by side, one for the
/// primary station and one for the secondary. The Qt counterpart to the GTK
/// MultiStationDashboardPage -- composition over duplication: each pane is a
/// complete QtDashboardPage over its own DashboardPresenter (the primary over
/// the live model, the secondary over the MirrorModel the PrimaryToSecondary
/// bridge feeds). This shell only lays them out; the model-layer link lives in
/// the integration layer (ADR-0011). Mounted only when a secondary model exists
/// (ui.multistation_enabled).
class QtMultiStationPage : public QWidget {
public:
    QtMultiStationPage(DashboardPresenter& primaryPresenter,
                       DashboardPresenter& secondaryPresenter,
                       QWidget* parent = nullptr);
    ~QtMultiStationPage() override;

    QtMultiStationPage(const QtMultiStationPage&)            = delete;
    QtMultiStationPage& operator=(const QtMultiStationPage&) = delete;
    QtMultiStationPage(QtMultiStationPage&&)                 = delete;
    QtMultiStationPage& operator=(QtMultiStationPage&&)      = delete;

    /// The composition root attaches these to their presenters as observers.
    [[nodiscard]] QtDashboardPage* primaryPane() const { return primaryPane_; }
    [[nodiscard]] QtDashboardPage* secondaryPane() const {
        return secondaryPane_;
    }

protected:
    void changeEvent(QEvent* event) override;

private:
    std::unique_ptr<Ui::QtMultiStationPage> ui_;
    QtDashboardPage*                        primaryPane_{nullptr};
    QtDashboardPage*                        secondaryPane_{nullptr};
};

}  // namespace app::view
