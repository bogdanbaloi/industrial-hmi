// Model + presenter headers first so their ProductionError::ERROR and
// StatusZoneViewModel::Severity::ERROR enumerators are parsed before any Qt or
// Boost.Asio header pulls in wingdi.h (ERROR=0 macro).
#include "src/model/SimulatedModel.h"
#include "src/presenter/AlertCenter.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"

#include "src/qt/QtInitRoot.h"

#include "src/config/ConfigManager.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtPaletteManager.h"
#include "src/qt/view/QtMainWindow.h"
#include "src/model/ModelContext.h"

#include <QTimer>

namespace app::qt {

QtInitRoot::QtInitRoot(core::Bootstrap& bootstrap) : bootstrap_{bootstrap} {}

QtInitRoot::~QtInitRoot() {
    // Teardown in reverse order of run(). Stop the tick first so no further
    // callbacks reach the window, then detach and drop.
    tickTimer_.reset();

    if (window_) {
        if (dashboardPresenter_) {
            dashboardPresenter_->removeObserver(window_->dashboardPage());
        }
        if (productsPresenter_) {
            productsPresenter_->removeObserver(window_->productsPage());
        }
    }
    window_.reset();
    paletteManager_.reset();
    productsPresenter_.reset();
    dashboardPresenter_.reset();
    // AlertCenter last: the presenter holds a bare reference to it, so it must
    // out-live the presenter that raises alarms into it.
    alertCenter_.reset();

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

    // Presenter: DI construction, identical to the console and test wiring.
    dashboardPresenter_ = std::make_unique<DashboardPresenter>(model);
    productsPresenter_  = std::make_unique<ProductsPresenter>();
    paletteManager_     = std::make_unique<view::QtPaletteManager>(
        app::config::ConfigManager::instance());

    // Alarm store: the same ISA-18.2 AlertCenter the console and GTK frontends
    // wire (InitConsole / MainWindow). The presenter raises / clears alarms
    // into it; the Qt alerts page renders it. Injected before the first tick.
    alertCenter_ = std::make_unique<presenter::AlertCenter>();
    dashboardPresenter_->setAlertCenter(*alertCenter_);

    // Shell owns the page widgets; the presenters never learn they are talking
    // to Qt widgets rather than GTK pages or a terminal.
    window_ = std::make_unique<view::QtMainWindow>(
        *dashboardPresenter_, *productsPresenter_, *alertCenter_,
        app::config::ConfigManager::instance(), *paletteManager_);

    dashboardPresenter_->addObserver(window_->dashboardPage());
    productsPresenter_->addObserver(window_->productsPage());
    dashboardPresenter_->initialize();
    productsPresenter_->initialize();
    model.initializeDemoData();

    // Populate the products table once (onProductsLoaded fires synchronously).
    productsPresenter_->loadProducts();

    // Drive the simulation from a UI-thread timer. Every tick runs on the Qt
    // event loop, so the presenter callbacks reach the widgets on the UI thread
    // with no cross-thread marshalling. A production build with a background
    // producer would marshal via a queued signal, the Qt analog of the GTK
    // frontend's Glib::signal_idle hop.
    tickTimer_ = std::make_unique<QTimer>();
    QObject::connect(tickTimer_.get(), &QTimer::timeout, window_.get(), [this] {
        app::model::SimulatedModel::instance().tickSimulation();
        // Drive alarm shelf auto-expiry on the same UI-thread cadence the GTK
        // frontend uses (no separate timer needed).
        alertCenter_->tick();
    });
    tickTimer_->start(kTickPeriod);

    // Apply the stored palette (or the default) before showing.
    paletteManager_->applyInitial();

    window_->show();
}

}  // namespace app::qt
