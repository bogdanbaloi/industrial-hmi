// Model + presenter headers first so their ProductionError::ERROR and
// StatusZoneViewModel::Severity::ERROR enumerators are parsed before any Qt or
// Boost.Asio header pulls in wingdi.h (ERROR=0 macro).
#include "src/model/SimulatedModel.h"
#include "src/presenter/AlertCenter.h"
#include "src/presenter/BackendHealthPresenter.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/presenter/QualityInspectionPresenter.h"

#include "src/ml/FakeImageClassifier.h"
#include "src/ml/ImageDecoder.h"

#include "src/qt/QtInitRoot.h"

#include "src/app/IntegrationBootstrap.h"
#include "src/integration/IntegrationManager.h"
#include "src/config/ConfigManager.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/historian/HistorianBridge.h"
#include "src/historian/HistorianMaintenance.h"
#include "src/historian/SqliteHistoryStore.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtGoodsReceiptPage.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtTrendsPage.h"
#include "src/qt/view/QtPaletteManager.h"
#include "src/qt/view/QtMainWindow.h"
#include "src/qt/view/QtStatusStrip.h"
#include "src/qt/view/QtUiDispatch.h"
#include "src/model/ModelContext.h"

#include <sigc++/functors/mem_fun.h>

#include <QTimer>

#include <chrono>
#include <cstddef>
#include <vector>

namespace app::qt {

namespace {
// Canned confidences for the goods-receipt demo classifier. FakeImageClassifier
// replays these regardless of the decoded image; the view labels them a demo.
constexpr float kDemoConfIntact  = 0.93F;
constexpr float kDemoConfScuff   = 0.045F;
constexpr float kDemoConfCrushed = 0.018F;
constexpr float kDemoConfWater   = 0.007F;

std::vector<ml::Classification> makeDemoInspectionResults() {
    return {
        {0, "Intact packaging", kDemoConfIntact},
        {1, "Minor surface scuff", kDemoConfScuff},
        {2, "Crushed corner", kDemoConfCrushed},
        {3, "Water damage", kDemoConfWater},
    };
}
}  // namespace

QtInitRoot::QtInitRoot(core::Bootstrap& bootstrap) : bootstrap_{bootstrap} {}

QtInitRoot::~QtInitRoot() {
    // Teardown in reverse order of run(). Stop the tick first so no further
    // callbacks reach the window, then detach and drop.
    tickTimer_.reset();
    // Drop the sigc slot into the status strip before the window (and the strip)
    // are destroyed.
    systemStateConn_.disconnect();
    dashboardStateConn_.disconnect();
    alertsBadgeConn_.disconnect();

    // Stop the integration backends before any observer of them is torn down.
    if (integrationServices_ && integrationServices_->manager) {
        integrationServices_->manager->stopAll();
    }

    if (window_) {
        if (dashboardPresenter_) {
            dashboardPresenter_->removeObserver(window_->dashboardPage());
            dashboardPresenter_->removeObserver(window_->trendsPage());
        }
        if (productsPresenter_) {
            productsPresenter_->removeObserver(window_->productsPage());
        }
        if (backendHealthPresenter_) {
            backendHealthPresenter_->removeObserver(window_->statusStrip());
        }
        if (inspectionPresenter_) {
            inspectionPresenter_->removeObserver(window_->goodsReceiptPage());
        }
    }
    window_.reset();
    paletteManager_.reset();
    productsPresenter_.reset();
    dashboardPresenter_.reset();
    // Inspection presenter holds references to the classifier + decoder, so
    // drop it before them.
    inspectionPresenter_.reset();
    imageClassifier_.reset();
    imageDecoder_.reset();
    // AlertCenter last: the presenter holds a bare reference to it, so it must
    // out-live the presenter that raises alarms into it.
    alertCenter_.reset();
    // BackendHealthPresenter holds a reference to the manager inside
    // integrationServices_, so drop the presenter first, then the bundle.
    backendHealthPresenter_.reset();
    integrationServices_.reset();

    // Historian teardown in dependency order: stop the retention worker (jthread
    // on the store), then drop the bridge (its destructor flushes pending rows),
    // then close the store. All must precede clearCallbacks so the bridge is off
    // the model before the model's callbacks vanish.
    historianMaintenance_.reset();
    historianBridge_.reset();
    historyStore_.reset();

    // Mirror InitConsole's shutdown: drop model callbacks and stop the Asio
    // io_context worker before static teardown gets ambiguous.
    app::model::SimulatedModel::instance().clearCallbacks();
    app::model::ModelContext::instance().stop();
}

void QtInitRoot::refreshAlertsBadge() {
    if (!window_) {
        return;
    }
    const int count = static_cast<int>(alertCenter_->snapshot().size());
    auto* window = window_.get();
    view::postToUi(window, [window, count] { window->setAlertsBadge(count); });
}

void QtInitRoot::buildHistorian() {
    auto& config = app::config::ConfigManager::instance();
    if (!config.isHistorianEnabled()) {
        return;
    }
    auto& logger = bootstrap_.logger();

    historian::SqliteHistoryStore::Config storeCfg;
    storeCfg.dbPath = config.getHistorianDbPath();
    historyStore_   = std::make_unique<historian::SqliteHistoryStore>(
        std::move(storeCfg));
    historyStore_->setLogger(logger);
    if (!historyStore_->initialize()) {
        logger.warn("Historian disabled: SqliteHistoryStore failed to open '{}'",
                    config.getHistorianDbPath());
        historyStore_.reset();
        return;
    }

    // Bridge: persist model scalar changes. Same wiring main()'s
    // registerHistorian performs for the GTK / console frontends.
    historian::HistorianBridge::Config bridgeCfg;
    bridgeCfg.maxBatchSize =
        static_cast<std::size_t>(config.getHistorianBatchSize());
    bridgeCfg.maxBatchAge =
        std::chrono::milliseconds{config.getHistorianBatchAgeMs()};
    historianBridge_ = std::make_unique<historian::HistorianBridge>(
        *historyStore_, app::model::SimulatedModel::instance(), bridgeCfg);
    historianBridge_->setLogger(logger);
    historianBridge_->wire();

    // Tiered-retention worker (raw -> 1m -> 1h) on a cadence.
    historian::HistorianMaintenance::Config mainCfg;
    mainCfg.sweepInterval =
        std::chrono::milliseconds{config.getHistorianSweepIntervalMs()};
    mainCfg.rawRetention =
        std::chrono::milliseconds{config.getHistorianRawRetentionMs()};
    mainCfg.minuteRetention =
        std::chrono::milliseconds{config.getHistorianMinuteRetentionMs()};
    historianMaintenance_ =
        std::make_unique<historian::HistorianMaintenance>(*historyStore_,
                                                          mainCfg);
    historianMaintenance_->setLogger(logger);
    historianMaintenance_->start();
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

    // Integration layer: build every config-enabled backend through the shared
    // IntegrationBootstrap (the same composition main() and the console use),
    // start it, and expose it through a BackendHealthPresenter for the
    // Connectivity page. Reusing the integration composition behind a third
    // frontend is the integration-layer half of the toolkit-independence proof
    // (ADR-0022, REQ-ARCH-013).
    integrationServices_ = std::make_unique<integration::IntegrationServices>(
        integration::buildIntegrationServices(
            app::config::ConfigManager::instance(), logger));
    integrationServices_->manager->startAll();
    backendHealthPresenter_ = std::make_unique<BackendHealthPresenter>(
        *integrationServices_->manager);

    // Edge-AI goods-receipt inspection: the real QualityInspectionPresenter
    // (decode -> classify -> top-K) driven by the project's FakeImageClassifier
    // as a demo model. Reusing the ML presenter behind Qt continues the
    // toolkit-independence proof (the image is decoded for real; the canned
    // classification is surfaced plainly as a demo in the view).
    imageDecoder_    = std::make_unique<ml::ImageDecoder>();
    imageClassifier_ = std::make_unique<ml::FakeImageClassifier>(
        makeDemoInspectionResults(), "Goods-receipt demo");
    inspectionPresenter_ = std::make_unique<presenter::QualityInspectionPresenter>(
        *imageClassifier_, *imageDecoder_);

    // Historian: persisted time-series store + model bridge + retention worker,
    // built through the same config-gated, degraded-open path main() uses. The
    // read side (or null) flows into the window, which mounts the History page
    // only when the store opened. Reusing the historian behind a third frontend
    // extends the toolkit-independence proof to the persistence layer
    // (REQ-ARCH-014).
    buildHistorian();

    // Shell owns the page widgets; the presenters never learn they are talking
    // to Qt widgets rather than GTK pages or a terminal.
    window_ = std::make_unique<view::QtMainWindow>(
        *dashboardPresenter_, *productsPresenter_, *alertCenter_,
        *inspectionPresenter_, app::config::ConfigManager::instance(),
        *paletteManager_, historyStore_.get());

    dashboardPresenter_->addObserver(window_->dashboardPage());
    dashboardPresenter_->addObserver(window_->trendsPage());
    productsPresenter_->addObserver(window_->productsPage());
    backendHealthPresenter_->addObserver(window_->statusStrip());
    inspectionPresenter_->addObserver(window_->goodsReceiptPage());
    // Feed the status-strip system-state pill from the presenter's state signal
    // (the same signal the GTK SystemStatusBadge listens to).
    systemStateConn_ = dashboardPresenter_->signalSystemStateChanged().connect(
        sigc::mem_fun(*window_->statusStrip(), &view::QtStatusStrip::setSystemState));
    // Feed the Overview uptime donut from the same state signal.
    dashboardStateConn_ = dashboardPresenter_->signalSystemStateChanged().connect(
        sigc::mem_fun(*window_->dashboardPage(),
                      &view::QtDashboardPage::setSystemState));
    // Keep the sidebar Alerts badge in sync with the active-alarm count.
    alertsBadgeConn_ = alertCenter_->signalAlertsChanged().connect(
        sigc::mem_fun(*this, &QtInitRoot::refreshAlertsBadge));
    dashboardPresenter_->initialize();
    productsPresenter_->initialize();
    inspectionPresenter_->initialize();
    model.initializeDemoData();

    // Populate the products table once (onProductsLoaded fires synchronously).
    productsPresenter_->loadProducts();

    // First backend-health poll so the status strip is populated before the
    // window paints (matches the GTK backend-health bar).
    backendHealthPresenter_->poll();
    // Initial badge (demo data may already have raised alarms).
    refreshAlertsBadge();

    // Drive the simulation from a UI-thread timer. Every tick runs on the Qt
    // event loop, so the presenter callbacks reach the widgets on the UI thread
    // with no cross-thread marshalling. A production build with a background
    // producer would marshal via a queued signal, the Qt analog of the GTK
    // frontend's Glib::signal_idle hop.
    tickTimer_ = std::make_unique<QTimer>();
    QObject::connect(tickTimer_.get(), &QTimer::timeout, window_.get(), [this] {
        app::model::SimulatedModel::instance().tickSimulation();
        // Drive alarm shelf auto-expiry and re-poll backend health on the same
        // UI-thread cadence the GTK frontend uses (no separate timers needed).
        alertCenter_->tick();
        backendHealthPresenter_->poll();
    });
    tickTimer_->start(kTickPeriod);

    // Apply the stored palette (or the default) before showing.
    paletteManager_->applyInitial();

    // Start in fullscreen (industrial kiosk mode), matching the GTK frontend.
    // The Settings Windowed toggle restores the 1920x1080 windowed size.
    window_->showFullScreen();
}

}  // namespace app::qt
