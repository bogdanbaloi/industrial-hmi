#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace app::core {
class Bootstrap;
}

namespace app {
class DashboardPresenter;
}

namespace app::view {
class QtDashboardWindow;
}

namespace app::qt {

/// Composition root for the Qt desktop frontend. The direct counterpart to
/// `app::console::InitConsole`: it builds the same Model + Presenter + View
/// collaborators, wires them, drives the simulation tick, and shows the
/// window. The existence of a third composition root over the SAME presenter
/// and model layer is the concrete proof that the MVP View seam is
/// toolkit-independent (ADR-0020, extends ADR-0002).
///
/// Ownership: borrows the already-prepared Bootstrap (logger + config).
/// Bootstrap must out-live this object, which in practice means both are
/// stack objects in `main()`.
class QtInitRoot {
public:
    explicit QtInitRoot(core::Bootstrap& bootstrap);
    ~QtInitRoot();

    QtInitRoot(const QtInitRoot&)            = delete;
    QtInitRoot& operator=(const QtInitRoot&) = delete;
    QtInitRoot(QtInitRoot&&)                 = delete;
    QtInitRoot& operator=(QtInitRoot&&)      = delete;

    /// Build the presenter + window, attach the observer, start the tick
    /// thread and show the window. Non-blocking: the Qt event loop is run by
    /// `QApplication::exec()` back in `main()`.
    void run();

private:
    void tickLoop(std::stop_token stop);

    core::Bootstrap&                         bootstrap_;
    std::unique_ptr<DashboardPresenter>      dashboardPresenter_;
    std::unique_ptr<view::QtDashboardWindow> window_;
    std::jthread                             tickThread_;
    std::atomic<bool>                        tickEnabled_{true};

    static constexpr std::chrono::milliseconds kDefaultTickPeriod{2000};
};

}  // namespace app::qt
