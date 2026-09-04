#pragma once

#include <chrono>
#include <memory>

class QTimer;

namespace app::core {
class Bootstrap;
}

namespace app {
class DashboardPresenter;
}

namespace app::view {
class QtMainWindow;
}

namespace app::qt {

/// Composition root for the Qt desktop frontend. The direct counterpart to
/// `app::console::InitConsole`: it builds the same Model + Presenter + View
/// collaborators, wires them, drives the simulation tick, and shows the shell
/// window. The existence of a third composition root over the SAME presenter
/// and model layer is the concrete proof that the MVP View seam is
/// toolkit-independent (ADR-0020, extends ADR-0002).
///
/// The tick runs on a UI-thread QTimer, so presenter callbacks reach the
/// widgets on the UI thread without cross-thread marshalling.
///
/// Ownership: borrows the already-prepared Bootstrap (logger + config).
/// Bootstrap must out-live this object; in practice both are stack objects in
/// `main()`.
class QtInitRoot {
public:
    explicit QtInitRoot(core::Bootstrap& bootstrap);
    ~QtInitRoot();

    QtInitRoot(const QtInitRoot&)            = delete;
    QtInitRoot& operator=(const QtInitRoot&) = delete;
    QtInitRoot(QtInitRoot&&)                 = delete;
    QtInitRoot& operator=(QtInitRoot&&)      = delete;

    /// Build the presenter + shell, attach the observer, start the tick timer
    /// and show the window. Non-blocking: the Qt event loop is run by
    /// `QApplication::exec()` back in `main()`.
    void run();

private:
    core::Bootstrap&                    bootstrap_;
    std::unique_ptr<DashboardPresenter> dashboardPresenter_;
    std::unique_ptr<view::QtMainWindow> window_;
    std::unique_ptr<QTimer>             tickTimer_;

    static constexpr std::chrono::milliseconds kTickPeriod{2000};
};

}  // namespace app::qt
