// Model + presenter headers first so their ProductionError::ERROR and
// StatusZoneViewModel::Severity::ERROR enumerators are parsed before any Qt
// or Boost.Asio header pulls in wingdi.h (ERROR=0 macro). Same rule the
// console composition root relies on.
#include "src/model/SimulatedModel.h"
#include "src/presenter/DashboardPresenter.h"

#include "src/qt/QtInitRoot.h"

#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/qt/view/QtDashboardWindow.h"
#include "src/model/ModelContext.h"

#include <chrono>
#include <thread>

namespace app::qt {

QtInitRoot::QtInitRoot(core::Bootstrap& bootstrap) : bootstrap_{bootstrap} {}

QtInitRoot::~QtInitRoot() {
    // Teardown in reverse order of run(). Stop the tick first so no further
    // ViewObserver callbacks are queued at the window, then detach and drop.
    tickEnabled_.store(false, std::memory_order_release);
    tickThread_.request_stop();
    if (tickThread_.joinable()) {
        tickThread_.join();
    }

    if (dashboardPresenter_ && window_) {
        dashboardPresenter_->removeObserver(window_.get());
    }
    window_.reset();
    dashboardPresenter_.reset();

    // Mirror InitConsole's shutdown: drop model callbacks and stop the Asio
    // io_context worker before static teardown gets ambiguous.
    app::model::SimulatedModel::instance().clearCallbacks();
    app::model::ModelContext::instance().stop();
}

void QtInitRoot::run() {
    auto& logger = bootstrap_.logger();
    logger.info("Application starting (Qt frontend)");

    // Model: reuse the same SimulatedModel singleton the GTK and console
    // frontends bind to. Logger injection follows the same pattern.
    auto& model = app::model::SimulatedModel::instance();
    model.setLogger(logger);

    // Presenter: DI construction so the dependency graph is explicit at the
    // composition root, identical to the console and test wiring.
    dashboardPresenter_ = std::make_unique<DashboardPresenter>(model);

    // View: implements app::ViewObserver. The presenter never learns it is
    // talking to a Qt widget rather than a GTK page or a terminal.
    window_ = std::make_unique<view::QtDashboardWindow>(*dashboardPresenter_);

    dashboardPresenter_->addObserver(window_.get());
    dashboardPresenter_->initialize();
    model.initializeDemoData();

    // Simulation tick on a background thread, exactly like InitConsole. The
    // ViewObserver callbacks it triggers are re-marshalled onto the Qt UI
    // thread inside QtDashboardWindow.
    tickThread_ = std::jthread([this](std::stop_token st) { tickLoop(st); });

    window_->show();
}

void QtInitRoot::tickLoop(std::stop_token stop) {
    // Sleep in small slices so stop is responsive even mid-tick-period.
    constexpr auto kSlice = std::chrono::milliseconds{50};
    auto next = std::chrono::steady_clock::now() + kDefaultTickPeriod;
    while (!stop.stop_requested()
           && tickEnabled_.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next) {
            app::model::SimulatedModel::instance().tickSimulation();
            next = now + kDefaultTickPeriod;
        }
        std::this_thread::sleep_for(kSlice);
    }
}

}  // namespace app::qt
