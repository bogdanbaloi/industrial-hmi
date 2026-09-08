#pragma once

// ViewObserver.h (and the view-model headers it pulls) MUST precede any Qt
// header: Qt on Windows includes wingdi.h whose ERROR=0 macro would corrupt
// unrelated enumerators. Same gotcha the other Qt views document.
#include "src/presenter/ViewObserver.h"

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QTimer;

namespace app::view {

/// Always-visible status strip along the bottom of the shell: the system-state
/// pill (Idle / Running / Error / Calibration), a compact backend-health row
/// (one coloured dot per integration backend) and a live clock. The Qt analog
/// of the GTK sidebar's SystemStatusBadge + BackendHealthBar (compact) +
/// LiveClock, gathered into one strip. Backend health is glanceable status, so
/// it lives here rather than on a page of its own.
///
/// It is an app::ViewObserver for backend health (driven by
/// BackendHealthPresenter through the same seam the GTK bar uses); the
/// system-state pill is fed via setSystemState, wired to the presenter's
/// state signal by the composition root.
class QtStatusStrip : public QWidget, public app::ViewObserver {
public:
    explicit QtStatusStrip(QWidget* parent = nullptr);
    ~QtStatusStrip() override;

    QtStatusStrip(const QtStatusStrip&)            = delete;
    QtStatusStrip& operator=(const QtStatusStrip&) = delete;
    QtStatusStrip(QtStatusStrip&&)                 = delete;
    QtStatusStrip& operator=(QtStatusStrip&&)      = delete;

    /// Update the system-state pill. `state` is `static_cast<int>(SystemState)`
    /// (0 Idle, 1 Running, 2 Error, 3 Calibration) -- an int, not the enum, so
    /// this header stays clear of the wingdi ERROR macro.
    void setSystemState(int state);

    void onBackendHealthChanged(
        const presenter::BackendHealthViewModel& viewModel) override;

private:
    void applySystemState(int state);
    void applyBackendHealth(const presenter::BackendHealthViewModel& viewModel);
    void updateClock();

    QLabel*      stateBadge_{nullptr};
    QHBoxLayout* backendsLayout_{nullptr};
    QLabel*      summaryLabel_{nullptr};
    QLabel*      clock_{nullptr};
    QTimer*      clockTimer_{nullptr};
};

}  // namespace app::view
