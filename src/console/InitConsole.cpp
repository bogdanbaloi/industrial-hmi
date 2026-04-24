// SimulatedModel.h (and therefore ProductionTypes.h with its
// `enum class ProductionError { ... ERROR ... }`) must come before
// ModelContext.h -- the latter pulls Boost.Asio which on Windows
// transitively includes wingdi.h, which `#define ERROR 0` and
// poisons the enum identifier. Same reason main.cpp keeps
// SimulatedModel include near the top.
#include "src/model/SimulatedModel.h"

#include "src/console/InitConsole.h"

#include "src/console/ConsoleView.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/model/ModelContext.h"
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

    // Model -- reuse the Simulated singleton that GTK also binds to.
    // Logger injection is the same pattern MainWindow uses.
    auto& model = model::SimulatedModel::instance();
    model.setLogger(logger);

    // Presenters -- DI construction so we don't rely on any singleton
    // shortcut from inside the presenter. Makes the dependency graph
    // explicit at the composition root, identical to test wiring.
    alertCenter_        = std::make_unique<presenter::AlertCenter>();
    dashboardPresenter_ = std::make_unique<DashboardPresenter>(model);
    dashboardPresenter_->setAlertCenter(*alertCenter_);
    productsPresenter_  = std::make_unique<ProductsPresenter>();  // DB singleton path

    // View -- injected with std::cout / std::cin so tests can plug in
    // string streams and run scenarios without touching terminal state.
    view_ = std::make_unique<ConsoleView>(std::cout, std::cin);

    wireActions();
    attachObservers();

    dashboardPresenter_->initialize();
    productsPresenter_->initialize();
    model.initializeDemoData();

    // Simulation tick -- drives the SimulatedModel the same way the
    // GTK path does via Glib::signal_timeout, except without GLib.
    tickThread_ = std::jthread([this](std::stop_token st) { tickLoop(st); });

    // Event loop -- ConsoleView owns the stdin reader thread; we block
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

    // Stop the Asio io_context worker used by DatabaseManager async paths.
    // Without this, its std::jthread joins after the singleton teardown
    // order gets ambiguous at static destruction time and can crash
    // (observed on Windows: segfault after the final log line).
    model::ModelContext::instance().stop();

    logger.info("Application shutting down (console frontend)");
    return 0;
}

// Wiring

void InitConsole::wireActions() {
    // Presenter methods are captured by raw pointer on the stable
    // unique_ptr owned by this composition root, so the lambdas stay
    // valid for as long as the ConsoleView lives. AlertCenter is
    // likewise captured by pointer -- all three objects tear down
    // together in InitConsole::run()'s cleanup block.
    auto* dp = dashboardPresenter_.get();
    auto* pp = productsPresenter_.get();
    auto* ac = alertCenter_.get();

    view_->onStart     ([dp] { if (dp) dp->onStartClicked(); });
    view_->onStop      ([dp] { if (dp) dp->onStopClicked(); });
    view_->onReset     ([dp] { if (dp) dp->onResetRestartClicked(); });
    view_->onCalibrate ([dp] { if (dp) dp->onCalibrationClicked(); });
    view_->onToggleEquipment(
        [dp](std::uint32_t id, bool enabled) {
            if (dp) dp->onEquipmentToggled(id, enabled);
        });

    // Products: loadProducts() re-emits onProductsLoaded synchronously
    // (the sync repository path, not the async DatabaseManager CRUD
    // path), so console scenarios can exercise the full Model ->
    // Presenter -> View chain without a GLib main loop.
    view_->onListProducts([pp] { if (pp) pp->loadProducts(); });
    view_->onViewProduct(
        [pp](int id) { if (pp) pp->viewProduct(id); });

    // Alerts: ConsoleView asks for a snapshot on demand rather than
    // receiving a push-stream, because the terminal rendering cadence
    // is the user's keystroke, not a real-time refresh timer.
    view_->onAlertsSnapshot(
        [ac]() -> std::vector<presenter::AlertViewModel> {
            return ac ? ac->snapshot() : std::vector<presenter::AlertViewModel>{};
        });
    view_->onDismissAlert(
        [ac](std::string_view key) { if (ac) ac->clear(key); });

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
