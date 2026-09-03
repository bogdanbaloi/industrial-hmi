#pragma once

// ViewObserver.h (and the view-model headers it pulls) MUST precede any Qt
// header: Qt on Windows includes wingdi.h, whose ERROR=0 macro would corrupt
// StatusZoneViewModel::Severity::ERROR. Same class of gotcha the GTK side
// documents.
#include "src/presenter/ViewObserver.h"

#include <QMainWindow>

#include <cstdint>
#include <unordered_map>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;

namespace app {
class DashboardPresenter;
}

namespace app::view {

class QtEquipmentCard;
class QtActuatorCard;

/// Qt implementation of the dashboard View. It is both a QMainWindow and an
/// app::ViewObserver, so the real DashboardPresenter drives it through the
/// exact same seam the GTK DashboardPage uses. It holds no business logic: it
/// renders the view-models it is handed and forwards button clicks to the
/// presenter.
///
/// Threading: ViewObserver callbacks arrive on the presenter/tick thread.
/// Each override re-marshals onto the Qt UI thread with
/// QMetaObject::invokeMethod(..., Qt::QueuedConnection) before touching any
/// widget -- the Qt-native analogue of GTK's Glib::signal_idle.
class QtDashboardWindow : public QMainWindow, public app::ViewObserver {
public:
    explicit QtDashboardWindow(DashboardPresenter& presenter,
                               QWidget* parent = nullptr);
    ~QtDashboardWindow() override;

    QtDashboardWindow(const QtDashboardWindow&)            = delete;
    QtDashboardWindow& operator=(const QtDashboardWindow&) = delete;
    QtDashboardWindow(QtDashboardWindow&&)                 = delete;
    QtDashboardWindow& operator=(QtDashboardWindow&&)      = delete;

    // ViewObserver overrides (Slice 1). Called on the presenter thread; each
    // one hops to the UI thread before rendering.
    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onControlPanelChanged(const presenter::ControlPanelViewModel& vm) override;
    void onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) override;
    void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) override;
    void onActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) override;

private:
    void buildUi();

    // Actual widget mutation, always on the UI thread.
    void renderWorkUnit(const presenter::WorkUnitViewModel& vm);
    void renderControlPanel(const presenter::ControlPanelViewModel& vm);
    void renderStatusZone(const presenter::StatusZoneViewModel& vm);
    void renderEquipmentCard(const presenter::EquipmentCardViewModel& vm);
    void renderActuatorCard(const presenter::ActuatorCardViewModel& vm);

    DashboardPresenter& presenter_;

    QLabel*       statusBanner_{nullptr};
    QLabel*       workUnitIdLabel_{nullptr};
    QLabel*       productLabel_{nullptr};
    QLabel*       statusMessageLabel_{nullptr};
    QProgressBar* progressBar_{nullptr};
    QPushButton*  startButton_{nullptr};
    QPushButton*  stopButton_{nullptr};
    QPushButton*  resetButton_{nullptr};

    // Equipment and actuator cards, created lazily as the presenter first
    // reports each id, then updated in place on later notifications.
    QHBoxLayout* equipmentLayout_{nullptr};
    QHBoxLayout* actuatorLayout_{nullptr};
    std::unordered_map<std::uint32_t, QtEquipmentCard*> equipmentCards_;
    std::unordered_map<std::uint32_t, QtActuatorCard*>  actuatorCards_;
};

}  // namespace app::view
