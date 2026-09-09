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
#include "src/auth/Argon2PasswordHasher.h"
#include "src/auth/AuthService.h"
#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/auth/SqliteAuditLogger.h"
#include "src/auth/SqliteUserRepository.h"
#include "src/auth/User.h"
#include "src/presenter/UsersPresenter.h"
#include "src/qt/view/QtChangePasswordDialog.h"
#include "src/qt/view/QtLoginDialog.h"
#include "src/qt/view/QtDashboardPage.h"
#include "src/qt/view/QtGettextTranslator.h"
#include "src/qt/view/QtGoodsReceiptPage.h"
#include "src/qt/view/QtProductsPage.h"
#include "src/qt/view/QtTrendsPage.h"
#include "src/qt/view/QtPaletteManager.h"
#include "src/qt/view/QtMainWindow.h"
#include "src/qt/view/QtMultiStationPage.h"
#include "src/qt/view/QtStatusStrip.h"
#include "src/qt/view/QtUiDispatch.h"
#include "src/model/MirrorModel.h"
#include "src/model/ModelContext.h"

#include <sigc++/functors/mem_fun.h>

#include <QCoreApplication>
#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

#include <libintl.h>
#include <string>

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
    // Stop the integration backends before any observer of them is torn down.
    if (integrationServices_ && integrationServices_->manager) {
        integrationServices_->manager->stopAll();
    }
    // Stop the tick, drop the sigc slots + observers, and destroy the window.
    teardownWindow();
    // Detach + drop the translator after the widgets that used it are gone.
    if (translator_) {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::removeTranslator(translator_.get());
        }
        translator_.reset();
    }
    paletteManager_.reset();
    productsPresenter_.reset();
    // Secondary dashboard presenter borrows the MirrorModel (in
    // integrationServices_) and the AlertCenter, so drop it before both.
    secondaryDashboardPresenter_.reset();
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

    // Auth teardown in dependency order: presenter + service borrow the repo /
    // hasher / audit / session, so drop them before the objects they reference,
    // and the session last.
    usersPresenter_.reset();
    authService_.reset();
    auditLogger_.reset();
    authHasher_.reset();
    authRepo_.reset();
    authSession_.reset();

    // Mirror InitConsole's shutdown: drop model callbacks and stop the Asio
    // io_context worker before static teardown gets ambiguous.
    app::model::SimulatedModel::instance().clearCallbacks();
    app::model::ModelContext::instance().stop();
}

void QtInitRoot::teardownWindow() {
    // Stop the tick first so no further callbacks reach the window.
    tickTimer_.reset();
    systemStateConn_.disconnect();
    dashboardStateConn_.disconnect();
    alertsBadgeConn_.disconnect();
    sessionConn_.disconnect();
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
        if (secondaryDashboardPresenter_ &&
            window_->multiStationPage() != nullptr) {
            dashboardPresenter_->removeObserver(
                window_->multiStationPage()->primaryPane());
            secondaryDashboardPresenter_->removeObserver(
                window_->multiStationPage()->secondaryPane());
        }
    }
    window_.reset();
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

void QtInitRoot::changeLanguage(const std::string& code) {
    auto& config = app::config::ConfigManager::instance();
    if (!config.setLanguage(code)) {  // persist + update in-memory value
        bootstrap_.logger().warn(
            "Could not persist language selection '{}'; applying for this "
            "session only", code);
    }
    config.applyI18n();  // rebind the gettext catalog + bump its cache
    // Reinstall so Qt re-runs translate() on every widget (a QEvent::Language
    // change broadcast): uic retranslateUi + our changeEvent seams repaint with
    // the new catalog. removeTranslator alone would fall back to source strings.
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::removeTranslator(translator_.get());
        QCoreApplication::installTranslator(translator_.get());
    }
}

void QtInitRoot::buildAuth() {
    auto& config = app::config::ConfigManager::instance();
    if (!config.isAuthEnabled()) {
        return;
    }
    auto& logger = bootstrap_.logger();

    authSession_ = std::make_unique<auth::Session>();

    auth::SqliteUserRepository::Config repoCfg;
    repoCfg.dbPath = config.getAuthDbPath();
    authRepo_ = std::make_unique<auth::SqliteUserRepository>(std::move(repoCfg));
    authRepo_->setLogger(logger);
    if (!authRepo_->initialize()) {
        logger.warn("Auth disabled: user store failed to open '{}'",
                    config.getAuthDbPath());
        authRepo_.reset();
        authSession_.reset();
        return;
    }

    // Audit log shares the same SQLite file; a failed open downgrades to
    // "auth without audit" rather than killing the feature.
    auth::SqliteAuditLogger::Config auditCfg;
    auditCfg.dbPath = config.getAuthDbPath();
    auditLogger_ =
        std::make_unique<auth::SqliteAuditLogger>(std::move(auditCfg));
    auditLogger_->setLogger(logger);
    if (!auditLogger_->initialize()) {
        logger.warn("Audit log disabled: failed to open '{}'",
                    config.getAuthDbPath());
        auditLogger_.reset();
    }

    authHasher_  = std::make_unique<auth::Argon2PasswordHasher>();
    authService_ = std::make_unique<auth::AuthService>(*authRepo_, *authHasher_,
                                                       *authSession_);
    authService_->setLogger(logger);
    if (auditLogger_) {
        authService_->setAuditLogger(*auditLogger_);
    }
    // Seed operator/maintenance/admin demo accounts on first run (idempotent).
    authService_->seedDefaultUsersIfEmpty();

    // The user-management presenter needs the audit sink, so build it only when
    // audit opened -- no audit means no Users admin page (degraded, not a crash).
    if (auditLogger_) {
        usersPresenter_ = std::make_unique<presenter::UsersPresenter>(
            *authRepo_, *authHasher_, *authSession_, *auditLogger_);
    }
}

void QtInitRoot::refreshUserIdentity() {
    if (!window_ || !authSession_) {
        return;
    }
    // Named so the footer format + the signed-out placeholder are not bare
    // literals (clang-tidy does not police magic strings).
    const QString identityFormat = QStringLiteral("%1 · %2");
    const QString signedOutLabel = QStringLiteral("Signed out");

    const auto user = authSession_->currentUser();
    QString text;
    if (user.has_value()) {
        const QString name = QString::fromStdString(
            user->displayName.empty() ? user->username : user->displayName);
        const QString role =
            QString::fromStdString(std::string(auth::roleName(user->role)));
        text = identityFormat.arg(name, role);
    } else {
        text = signedOutLabel;
    }
    window_->setUserIdentity(text);
}

bool QtInitRoot::run() {
    auto& logger = bootstrap_.logger();
    logger.info("Application starting (Qt frontend)");

    // Route Qt's translate() (tr + .ui strings) through the shared gettext
    // catalog Bootstrap already bound, so the Qt frontend reuses the same
    // translations as GTK / console. Installed before any widget is built so the
    // first paint is already localised.
    translator_ = std::make_unique<view::QtGettextTranslator>();
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::installTranslator(translator_.get());
    }

    // Auth gate: build the auth stack, then (when enabled) show the modal login
    // before anything else is built or shown. A cancelled login means "operator
    // declined" -- return false so main() skips the event loop and exits, the
    // same contract as the GTK activation handler.
    buildAuth();
    if (authService_ != nullptr) {
        view::QtLoginDialog login(*authService_);
        if (login.exec() != QDialog::Accepted) {
            logger.info("Auth: sign-in cancelled, exiting");
            return false;
        }
    }

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

    // Multi-station: when the integration layer built a secondary MirrorModel
    // (ui.multistation_enabled), give it its own DashboardPresenter so a second
    // pane can render it. The PrimaryToSecondaryBridge in the integration bundle
    // keeps the mirror in step with the primary station (ADR-0011).
    if (integrationServices_->secondaryModel) {
        secondaryDashboardPresenter_ = std::make_unique<DashboardPresenter>(
            *integrationServices_->secondaryModel);
        secondaryDashboardPresenter_->setAlertCenter(*alertCenter_);
    }

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

    // Build, wire, populate and show the shell.
    buildAndShowWindow();
    return true;
}

void QtInitRoot::buildAndShowWindow() {
    // Shell owns the page widgets; the presenters never learn they are talking
    // to Qt widgets rather than GTK pages or a terminal.
    view::QtMainWindow::Context windowContext;
    windowContext.historyReader     = historyStore_.get();
    windowContext.onLanguageChanged = [this](const std::string& code) {
        changeLanguage(code);
    };
    windowContext.session = authSession_.get();
    // Sign-out control only when auth is active.
    if (authService_) {
        windowContext.onSignOut = [this] { signOut(); };
    }
    // Change-password control needs the users presenter (built only when the
    // audit log opened); any authenticated role may change its own password.
    if (usersPresenter_) {
        windowContext.onChangePassword = [this] { changePassword(); };
    }
    // Admin-only pages: pass their collaborators only for an Admin session, so
    // Users + Audit mount for admins and stay hidden (and unbuilt) otherwise.
    bool isAdmin = false;
    if (authSession_) {
        const auto user = authSession_->currentUser();
        isAdmin = user.has_value() && auth::canManageUsers(user->role);
    }
    windowContext.usersPresenter = isAdmin ? usersPresenter_.get() : nullptr;
    windowContext.auditReader    = isAdmin ? auditLogger_.get() : nullptr;
    windowContext.secondaryDashboardPresenter =
        secondaryDashboardPresenter_.get();
    window_ = std::make_unique<view::QtMainWindow>(
        *dashboardPresenter_, *productsPresenter_, *alertCenter_,
        *inspectionPresenter_, app::config::ConfigManager::instance(),
        *paletteManager_, std::move(windowContext));

    dashboardPresenter_->addObserver(window_->dashboardPage());
    dashboardPresenter_->addObserver(window_->trendsPage());
    productsPresenter_->addObserver(window_->productsPage());
    backendHealthPresenter_->addObserver(window_->statusStrip());
    inspectionPresenter_->addObserver(window_->goodsReceiptPage());
    // Multi-station panes each observe their own presenter (primary live model,
    // secondary mirror).
    if (window_->multiStationPage() != nullptr) {
        dashboardPresenter_->addObserver(
            window_->multiStationPage()->primaryPane());
        secondaryDashboardPresenter_->addObserver(
            window_->multiStationPage()->secondaryPane());
    }
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
    // Keep the sidebar footer in sync with the signed-in user (auth only).
    if (authSession_) {
        sessionConn_ = authSession_->signalChanged().connect(
            sigc::mem_fun(*this, &QtInitRoot::refreshUserIdentity));
        refreshUserIdentity();
    }

    // One-time presenter + model bootstrap: only on the first build, so a
    // sign-out rebuild re-attaches a fresh window to the STILL-RUNNING
    // simulation rather than resetting it.
    if (!windowBuiltOnce_) {
        dashboardPresenter_->initialize();
        if (secondaryDashboardPresenter_) {
            secondaryDashboardPresenter_->initialize();
        }
        productsPresenter_->initialize();
        inspectionPresenter_->initialize();
        app::model::SimulatedModel::instance().initializeDemoData();
        windowBuiltOnce_ = true;
    }

    // Populate the fresh window's snapshot views. Safe on a rebuild: it does not
    // reset the simulation, and the dashboard catches up on the next tick.
    productsPresenter_->loadProducts();
    backendHealthPresenter_->poll();
    refreshAlertsBadge();

    // Drive the simulation from a UI-thread timer. The timer is its own QObject
    // context (not the window) so it is recreated cleanly on a sign-out swap.
    tickTimer_ = std::make_unique<QTimer>();
    QObject::connect(tickTimer_.get(), &QTimer::timeout, tickTimer_.get(), [this] {
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

void QtInitRoot::signOut() {
    if (!authService_) {
        return;
    }
    auto& logger = bootstrap_.logger();
    authService_->logout();  // clears the session (fires signalChanged) + audits
    logger.info("Auth: operator signed out");

    view::QtLoginDialog login(*authService_);
    if (login.exec() != QDialog::Accepted) {
        logger.info("Auth: no re-login after sign-out, exiting");
        QCoreApplication::quit();
        return;
    }
    // Rebuild the shell so role-gated nav matches the new user. Deferred to the
    // next event-loop turn so we never delete the window from inside its own
    // Sign out button's slot.
    QTimer::singleShot(0, [this] {
        teardownWindow();
        buildAndShowWindow();
    });
}

void QtInitRoot::changePassword() {
    if (!usersPresenter_ || !window_) {
        return;
    }
    view::QtChangePasswordDialog dialog(window_.get());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto status = usersPresenter_->changeOwnPassword(
        dialog.currentPassword().toStdString(),
        dialog.newPassword().toStdString());

    // QtInitRoot is not a QObject, so translate through the shared gettext
    // catalog directly rather than tr().
    const QString title = QString::fromUtf8(gettext("Change password"));
    if (status == presenter::UsersStatus::Ok) {
        QMessageBox::information(window_.get(), title,
                                 QString::fromUtf8(gettext("Password changed.")));
    } else {
        const std::string msgid{presenter::statusMessage(status)};
        QMessageBox::warning(window_.get(), title,
                             QString::fromUtf8(gettext(msgid.c_str())));
    }
}

}  // namespace app::qt
