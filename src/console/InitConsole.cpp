#include "src/console/InitConsole.h"

#include "src/console/ConsoleView.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/model/SimulatedModel.h"
#include "src/presenter/AlertCenter.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"

#include <chrono>
#include <iostream>
#include <utility>

namespace app::console {

InitConsole::InitConsole(core::Bootstrap& bootstrap)
    : bootstrap_{bootstrap} {}

InitConsole::~InitConsole() = default;

int InitConsole::run() {
    auto& logger = bootstrap_.logger();
    logger.info("Application starting (console frontend)");

    // Model — reuse the Simulated singleton that GTK also binds to.
    // Logger injection is the same pattern MainWindow uses.
    auto& model = model::SimulatedModel::instance();
    model.setLogger(logger);

    // Presenters — DI construction so we don't rely on any singleton
    // shortcut from inside the presenter. Makes the dependency graph
    // explicit at the composition root, identical to test wiring.
    alertCenter_        = std::make_unique<presenter::AlertCenter>();
    dashboardPresenter_ = std::make_unique<DashboardPresenter>(model);
    dashboardPresenter_->setAlertCenter(*alertCenter_);
    productsPresenter_  = std::make_unique<ProductsPresenter>();  // DB singleton path

    // View — injected with std::cout / std::cin so tests can plug in
    // string streams and run scenarios without touching terminal state.
    view_ = std::make_unique<ConsoleView>(std::cout, std::cin);

    wireActions();
    attachObservers();

    dashboardPresenter_->initialize();
    productsPresenter_->initialize();
    model.initializeDemoData();

    // Simulation tick — drives the SimulatedModel the same way the
    // GTK path does via Glib::signal_timeout, except without GLib.
    tickThread_ = std::jthread([this](std::stop_token st) { tickLoop(st); });

    // Event loop — ConsoleView owns the stdin reader thread; we block
    // here until the user signals exit.
    view_->start();
    view_->waitForExit();

    // Teardown (reverse order of construction).
    tickEnabled_.store(false, std::memory_order_release);
    tickThread_.request_stop();
    if (tickThread_.joinable()) tickThread_.join();

    detachObservers();

    view_.reset();
    productsPresenter_.reset();
    dashboardPresenter_.reset();
    alertCenter_.reset();

    model.clearCallbacks();

    logger.info("Application shutting down (console frontend)");
    return 0;
}

// Wiring

void InitConsole::wireActions() {
    // Phase-1 command set: only shutdown is actionable; the rest become
    // real actions in phase 2 (start/stop/reset/calibrate/equipment
    // toggle). Presenter methods are captured by reference on the
    // stable unique_ptr so lambdas outlive nothing suspicious.
    auto* dp = dashboardPresenter_.get();

    view_->onStart     ([dp] { if (dp) dp->onStartClicked(); });
    view_->onStop      ([dp] { if (dp) dp->onStopClicked(); });
    view_->onReset     ([dp] { if (dp) dp->onResetRestartClicked(); });
    view_->onCalibrate ([dp] { if (dp) dp->onCalibrationClicked(); });
    view_->onToggleEquipment(
        [dp](std::uint32_t id, bool enabled) {
            if (dp) dp->onEquipmentToggled(id, enabled);
        });

    // Shutdown hook fires on `quit` so the tick thread can stop cleanly
    // before run() unblocks from waitForExit().
    view_->onShutdown([this] {
        tickEnabled_.store(false, std::memory_order_release);
    });
}

void InitConsole::attachObservers() {
    if (dashboardPresenter_) dashboardPresenter_->addObserver(view_.get());
    if (productsPresenter_)  productsPresenter_->addObserver(view_.get());
}

void InitConsole::detachObservers() {
    if (dashboardPresenter_) dashboardPresenter_->removeObserver(view_.get());
    if (productsPresenter_)  productsPresenter_->removeObserver(view_.get());
}

// Simulation tick

void InitConsole::tickLoop(std::stop_token stop) {
    // Sleep in small slices so stop is responsive even mid-tick-period.
    constexpr auto kSlice = std::chrono::milliseconds{50};
    auto next = std::chrono::steady_clock::now() + kDefaultTickPeriod;
    while (!stop.stop_requested()
           && tickEnabled_.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next) {
            model::SimulatedModel::instance().tickSimulation();
            next = now + kDefaultTickPeriod;
        }
        std::this_thread::sleep_for(kSlice);
    }
}

}  // namespace app::console
