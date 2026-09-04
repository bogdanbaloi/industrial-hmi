// Model + presenter headers first so their ProductionError::ERROR and
// StatusZoneViewModel::Severity::ERROR enumerators are parsed before any Qt or
// Boost.Asio header pulls in wingdi.h (ERROR=0 macro).
#include "src/model/SimulatedModel.h"
#include "src/presenter/DashboardPresenter.h"

#include "src/qt/QtInitRoot.h"

#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtMainWindow.h"
#include "src/model/ModelContext.h"

#include <QTimer>

namespace app::qt {

QtInitRoot::QtInitRoot(core::Bootstrap& bootstrap) : bootstrap_{bootstrap} {}

QtInitRoot::~QtInitRoot() {
    // Teardown in reverse order of run(). Stop the tick first so no further
    // callbacks reach the window, then detach and drop.
    tickTimer_.reset();

    if (dashboardPresenter_ && window_) {
        dashboardPresenter_->removeObserver(window_->dashboardPage());
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

    // Presenter: DI construction, identical to the console and test wiring.
    dashboardPresenter_ = std::make_unique<DashboardPresenter>(model);

    // Shell owns the page widgets; the presenter never learns it is talking to
    // a Qt widget rather than a GTK page or a terminal.
    window_ = std::make_unique<view::QtMainWindow>(*dashboardPresenter_);

    dashboardPresenter_->addObserver(window_->dashboardPage());
    dashboardPresenter_->initialize();
    model.initializeDemoData();

    // Drive the simulation from a UI-thread timer. Every tick runs on the Qt
    // event loop, so the presenter callbacks reach the widgets on the UI thread
    // with no cross-thread marshalling. A production build with a background
    // producer would marshal via a queued signal, the Qt analog of the GTK
    // frontend's Glib::signal_idle hop.
    tickTimer_ = std::make_unique<QTimer>();
    QObject::connect(tickTimer_.get(), &QTimer::timeout, window_.get(), [] {
        app::model::SimulatedModel::instance().tickSimulation();
    });
    tickTimer_->start(kTickPeriod);

    window_->show();
}

}  // namespace app::qt
