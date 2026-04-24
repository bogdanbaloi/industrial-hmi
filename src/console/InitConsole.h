#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace app::core { class Bootstrap; }

namespace app {
class DashboardPresenter;
class ProductsPresenter;
namespace presenter { class AlertCenter; }
}

namespace app::console {

class ConsoleView;

/// Composition root for the headless console front-end. Owns and wires
/// together the Model-Presenter-View collaborators that otherwise live
/// in MainWindow on the GTK side.
///
/// @design Symmetric counterpart to the lifecycle that
/// `core::Application` runs for the GTK binary -- the existence of two
/// composition roots sharing the same presenter + model layer is the
/// concrete proof that the MVP interfaces hold. Anything the
/// DashboardPresenter emits through `ViewObserver` is consumed here
/// without a single GTK dependency on the link line.
///
/// @lifecycle
///   1. `run()` builds presenters and the ConsoleView.
///   2. Presenter signals are wired to view callbacks; view command
///      callbacks are wired back to presenter methods (DI at compose
///      time, not at construction time -- presenters stay unaware of
///      whether their audience is a GTK widget or a terminal).
///   3. The simulation tick runs on a background thread, producing
///      ViewModel callbacks that the view renders as lines.
///   4. `run()` blocks in `ConsoleView::waitForExit()` until the user
///      types `quit` (or stdin hits EOF).
///   5. On exit, the tick thread is stopped, observers are detached,
///      presenters are released. Logger is owned by `Bootstrap` and
///      flushes on its own destruction in `main()`.
class InitConsole {
public:
    /// Borrow the already-prepared Bootstrap (logger + config + i18n).
    /// Bootstrap must out-live InitConsole.
    explicit InitConsole(core::Bootstrap& bootstrap);
    ~InitConsole();

    InitConsole(const InitConsole&)            = delete;
    InitConsole& operator=(const InitConsole&) = delete;
    InitConsole(InitConsole&&)                 = delete;
    InitConsole& operator=(InitConsole&&)      = delete;

    /// Build collaborators, run the event loop, tear everything down.
    /// Returns 0 on clean exit (user typed `quit` / stdin EOF).
    int run();

private:
    void wireActions();
    void attachObservers();
    void detachObservers();
    void tickLoop(std::stop_token stop);

    core::Bootstrap&                         bootstrap_;
    std::unique_ptr<presenter::AlertCenter>  alertCenter_;
    std::unique_ptr<DashboardPresenter>      dashboardPresenter_;
    std::unique_ptr<ProductsPresenter>       productsPresenter_;
    std::unique_ptr<ConsoleView>             view_;
    std::jthread                             tickThread_;
    std::atomic<bool>                        tickEnabled_{true};

    static constexpr std::chrono::milliseconds kDefaultTickPeriod{2000};
};

}  // namespace app::console
