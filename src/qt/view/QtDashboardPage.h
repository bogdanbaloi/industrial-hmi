#pragma once

// ViewObserver.h (and the view-model headers it pulls) MUST precede any Qt
// header: Qt on Windows includes wingdi.h, whose ERROR=0 macro would corrupt
// StatusZoneViewModel::Severity::ERROR. Same class of gotcha the GTK side
// documents.
#include "src/presenter/ViewObserver.h"

#include <QWidget>

#include <cstdint>
#include <memory>
#include <unordered_map>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtDashboardPage;
}

namespace app {
class DashboardPresenter;
}

namespace app::view {

class QtEquipmentCard;
class QtActuatorCard;
class QtQualityCard;

/// Qt dashboard page. A QWidget (hosted in a QtMainWindow tab) that is also an
/// app::ViewObserver, so the real DashboardPresenter drives it through the exact
/// same seam the GTK DashboardPage uses. The static layout lives in
/// QtDashboardPage.ui (loaded by uic), the analog of the GTK builder .ui. The
/// page holds no business logic: it renders the view-models it is handed and
/// forwards button clicks to the presenter.
///
/// Threading: the simulation tick runs on the Qt UI thread (QtInitRoot drives a
/// QTimer), so ViewObserver callbacks already arrive on the UI thread and touch
/// widgets directly. A production build with a background producer would marshal
/// via a queued signal, the Qt analog of the GTK Glib::signal_idle hop.
class QtDashboardPage : public QWidget, public app::ViewObserver {
public:
    explicit QtDashboardPage(DashboardPresenter& presenter,
                             QWidget* parent = nullptr);
    ~QtDashboardPage() override;

    QtDashboardPage(const QtDashboardPage&)            = delete;
    QtDashboardPage& operator=(const QtDashboardPage&) = delete;
    QtDashboardPage(QtDashboardPage&&)                 = delete;
    QtDashboardPage& operator=(QtDashboardPage&&)      = delete;

    // ViewObserver overrides. Called on the UI thread (see class note).
    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onControlPanelChanged(const presenter::ControlPanelViewModel& vm) override;
    void onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) override;
    void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) override;
    void onActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) override;
    void onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) override;

private:
    DashboardPresenter&                  presenter_;
    std::unique_ptr<Ui::QtDashboardPage> ui_;

    // Cards created lazily as the presenter first reports each id, then updated
    // in place on later notifications.
    std::unordered_map<std::uint32_t, QtEquipmentCard*> equipmentCards_;
    std::unordered_map<std::uint32_t, QtActuatorCard*>  actuatorCards_;
    std::unordered_map<std::uint32_t, QtQualityCard*>   qualityCards_;
};

}  // namespace app::view
