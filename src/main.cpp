#include "src/core/Bootstrap.h"
#include "src/core/StartupDialog.h"
#include "src/core/StartupErrors.h"
#include "src/config/ConfigManager.h"
#include "src/app/IntegrationBootstrap.h"
#include "src/integration/IntegrationManager.h"
#include "src/auth/Argon2PasswordHasher.h"
#include "src/auth/AuthService.h"
#include "src/auth/Session.h"
#include "src/auth/SqliteAuditLogger.h"
#include "src/auth/SqliteUserRepository.h"
#include "src/presenter/UsersPresenter.h"
#include "src/historian/HistorianBridge.h"
#include "src/historian/HistorianMaintenance.h"
#include "src/historian/SqliteHistoryStore.h"
#include "src/model/MirrorModel.h"
#include "src/model/SimulatedModel.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>   // SetConsoleOutputCP
#  include <fcntl.h>
#  include <io.h>
#  include <clocale>
#endif

#ifdef QT_FRONTEND_MODE
#  include "src/qt/QtInitRoot.h"
#  include <QApplication>
#elif defined(MCP_MODE)
#  include "src/mcp/McpInitRoot.h"
#  include <iostream>
#  include <ostream>
#elif defined(CONSOLE_MODE)
#  include "src/console/InitConsole.h"
#else
#  include "src/core/Application.h"
#endif

namespace {

// Compile-time tag: true for the headless binaries (console, MCP), false for
// the GTK one. Drives the fatal-reporter between stderr and MessageBoxW.
constexpr bool kConsoleMode =
#if defined(CONSOLE_MODE) || defined(MCP_MODE)
    true;
#else
    false;
#endif

// Process exit codes -- documented so CI / shell scripts can branch on them.
// Marked [[maybe_unused]] because the set used by a given build depends
// on which branch of the CONSOLE_MODE #ifdef is active (the GTK path
// propagates the GTK main-loop's own return code via `app.run(...)`).
[[maybe_unused]] constexpr int kExitOk              = 0;
[[maybe_unused]] constexpr int kExitUnexpectedFatal = 1;
[[maybe_unused]] constexpr int kExitStartupFatal    = 2;
[[maybe_unused]] constexpr int kExitUnknownFatal    = 3;




/// Build the Historian (SQLite store + bridge) when enabled in config.
/// Extracted from main() for the same reason as registerMqttBackend
/// etc -- keeps main() under the readability-function-size threshold,
/// and a degraded-config audit reads as a flat list of helpers.
///
/// If `initialize()` fails (bad path, read-only fs, permission), the
/// store is dropped and the bridge stays unconstructed -- the rest of
/// the binary keeps running with a missing History tab, matching the
/// project-wide "degraded > crash" policy.
[[maybe_unused]] void registerHistorian(
        app::config::ConfigManager& config,
        app::core::Logger& logger,
        std::unique_ptr<app::historian::SqliteHistoryStore>& storeOut,
        std::unique_ptr<app::historian::HistorianBridge>& bridgeOut,
        std::unique_ptr<app::historian::HistorianMaintenance>&
            maintenanceOut) {
    app::historian::SqliteHistoryStore::Config storeCfg;
    storeCfg.dbPath = config.getHistorianDbPath();
    storeOut = std::make_unique<app::historian::SqliteHistoryStore>(
        std::move(storeCfg));
    storeOut->setLogger(logger);

    if (!storeOut->initialize()) {
        logger.warn("Historian disabled: SqliteHistoryStore failed to "
                    "open '{}'", config.getHistorianDbPath());
        storeOut.reset();
        return;
    }

    app::historian::HistorianBridge::Config bridgeCfg;
    bridgeCfg.maxBatchSize = static_cast<std::size_t>(
        config.getHistorianBatchSize());
    bridgeCfg.maxBatchAge  = std::chrono::milliseconds{
        config.getHistorianBatchAgeMs()};
    bridgeOut = std::make_unique<app::historian::HistorianBridge>(
        *storeOut,
        app::model::SimulatedModel::instance(),
        bridgeCfg);
    bridgeOut->setLogger(logger);
    bridgeOut->wire();

    // Tiered-retention worker -- one jthread that demotes
    // raw -> 1m -> 1h on a cadence. RAII via unique_ptr; the
    // destructor cancels the stop_token, wakes the cv, joins.
    app::historian::HistorianMaintenance::Config mainCfg;
    mainCfg.sweepInterval   = std::chrono::milliseconds{
        config.getHistorianSweepIntervalMs()};
    mainCfg.rawRetention    = std::chrono::milliseconds{
        config.getHistorianRawRetentionMs()};
    mainCfg.minuteRetention = std::chrono::milliseconds{
        config.getHistorianMinuteRetentionMs()};
    maintenanceOut = std::make_unique<
        app::historian::HistorianMaintenance>(*storeOut, mainCfg);
    maintenanceOut->setLogger(logger);
    maintenanceOut->start();
}

/// Build the auth stack (repository + hasher + service) when enabled
/// in config. Same shape as the other register helpers: stack-owned
/// pieces handed back to main() so the destructors fire in reverse
/// declaration order at exit.
///
/// If the SQLite user store fails to initialise (read-only fs, bad
/// path) the stack stays unconstructed and the caller continues
/// without auth -- matches the project-wide degraded-over-crash
/// policy. Seeded default users (operator / maintenance / admin) are
/// only inserted on first run; a populated table is left alone.
[[maybe_unused]] void registerAuth(
        app::config::ConfigManager& config,
        app::core::Logger& logger,
        std::unique_ptr<app::auth::SqliteUserRepository>& repoOut,
        std::unique_ptr<app::auth::Argon2PasswordHasher>& hasherOut,
        std::unique_ptr<app::auth::SqliteAuditLogger>& auditOut,
        std::unique_ptr<app::auth::AuthService>& serviceOut,
        app::auth::Session& sessionRef) {
    app::auth::SqliteUserRepository::Config repoCfg;
    repoCfg.dbPath = config.getAuthDbPath();
    repoOut = std::make_unique<app::auth::SqliteUserRepository>(
        std::move(repoCfg));
    repoOut->setLogger(logger);

    if (!repoOut->initialize()) {
        logger.warn("Auth disabled: user store failed to open '{}'",
                    config.getAuthDbPath());
        repoOut.reset();
        return;
    }

    // Audit log shares the same SQLite file. Two tables (users +
    // audit_log) in one DB keeps backup + permissioning simple on
    // the operator terminal. A failed open downgrades to "auth
    // without audit" rather than crashing the whole feature.
    app::auth::SqliteAuditLogger::Config auditCfg;
    auditCfg.dbPath = config.getAuthDbPath();
    auditOut = std::make_unique<app::auth::SqliteAuditLogger>(
        std::move(auditCfg));
    auditOut->setLogger(logger);
    if (!auditOut->initialize()) {
        logger.warn("Audit log disabled: failed to open '{}'",
                    config.getAuthDbPath());
        auditOut.reset();
    }

    hasherOut  = std::make_unique<app::auth::Argon2PasswordHasher>();
    serviceOut = std::make_unique<app::auth::AuthService>(
        *repoOut, *hasherOut, sessionRef);
    serviceOut->setLogger(logger);
    if (auditOut) {
        serviceOut->setAuditLogger(*auditOut);
    }

    // Seed the three demo accounts on first run. The seeder is
    // idempotent so a populated DB is left alone.
    serviceOut->seedDefaultUsersIfEmpty();
}

#ifdef _WIN32
/// Windows-only platform init: Cairo renderer override + UTF-8 setup.
///
/// Three independent layers must agree on UTF-8 for the console
/// front-end to render translated strings correctly:
///   1. Win32 console codepage (cmd.exe stdout).
///   2. CRT locale (.UTF-8 supported from Windows 10 v1803; older
///      systems silently fall back).
///   3. Binary mode on stdout -- stops the CRT from LF->CRLF +
///      codepage conversion when stdout is a pipe (Git Bash /
///      mintty). Without this, UTF-8 bytes become Latin-1 mojibake.
///
/// Extracted from main() so the readability-function-size lint stays
/// under the 150-line threshold.
void initWindowsConsole() {
    _putenv_s("GSK_RENDERER", "cairo");
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}
#endif


#if !defined(CONSOLE_MODE) && !defined(QT_FRONTEND_MODE) && !defined(MCP_MODE)
/// Composition-root bundle passed to wireApplicationServices().
/// Grouped into a struct (rather than a long parameter list) so the
/// helper stays under the clang-tidy 8-parameter readability threshold.
/// Every member references storage owned by main()'s stack frame; the
/// helper writes through them but never takes ownership.
struct CompositionRoot {
    std::unique_ptr<app::historian::SqliteHistoryStore>&    historyStore;
    std::unique_ptr<app::auth::AuthService>&                authService;
    app::auth::Session&                                     authSession;
    std::unique_ptr<app::auth::SqliteAuditLogger>&          auditLogger;
    std::unique_ptr<app::model::MirrorModel>&               secondaryModel;
    std::unique_ptr<app::auth::SqliteUserRepository>&       authRepo;
    std::unique_ptr<app::auth::Argon2PasswordHasher>&       authHasher;
    std::unique_ptr<app::presenter::UsersPresenter>&        usersPresenterOut;
};

/// Flow composition-root pointers into the Application singleton and
/// build the (optional) UsersPresenter. Extracted from main() so the
/// readability-function-size lint stays under the 150-line threshold.
void wireApplicationServices(
        app::core::Application&               app,
        app::integration::IntegrationManager& integration,
        CompositionRoot&                      root) {
    // Inject the manager so MainWindow can mount the backend-
    // health bar in the sidebar. Pointer stays valid through
    // app.run() because `integration` lives on main()'s stack
    // frame just above us.
    app.setIntegrationManager(&integration);
    // Historian read side is optional -- mounted only when the
    // store opened successfully above. Pointer (or null) flows
    // to MainWindow which decides whether to register the page.
    app.setHistoryReader(root.historyStore.get());
    // Auth: when registerAuth() succeeded both pointers are non-
    // null and Application::run() will show the LoginDialog first.
    // When auth is disabled (or registration failed) the pointers
    // stay null and run() goes straight to MainWindow as before.
    app.setAuth(root.authService.get(), &root.authSession);
    app.setAuditLogger(root.auditLogger.get());

    // Multi-station secondary: when ui.multistation_enabled was true
    // the secondary MirrorModel was constructed above; flow the
    // pointer into Application so MainWindow can build a second
    // DashboardPresenter and swap the Dashboard tab for the
    // MultiStationDashboardPage.
    app.setSecondaryProductionModel(root.secondaryModel.get());

    // Users management presenter -- wired only when the full auth
    // stack is up (repo + hasher + audit + session). MainWindow
    // gates UsersPage on `currentUser.role == Admin`; non-admins
    // never see the tab even though the presenter is alive.
    if (root.authRepo && root.authHasher && root.auditLogger) {
        root.usersPresenterOut = std::make_unique<app::presenter::UsersPresenter>(
            *root.authRepo, *root.authHasher, root.authSession, *root.auditLogger);
        app.setUsersPresenter(root.usersPresenterOut.get());
    }
}
#endif  // GTK only (not console, not Qt, not MCP)

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    initWindowsConsole();
#endif

#ifdef MCP_MODE
    // stdout is the JSON-RPC channel. Capture it, then point std::cout at
    // stderr so every log line the app writes to std::cout lands on stderr
    // instead of corrupting the protocol stream. The MCP server is handed the
    // captured real stdout.
    std::ostream protocolOut(std::cout.rdbuf());
    std::cout.rdbuf(std::cerr.rdbuf());
#endif

    // Top-level exception guard. Exit codes:
    //   2 = startup fatal (config/DB), 1 = std::exception escaped,
    //   3 = non-std exception. See kExit* constants above.
    try {
        // Staged startup: logger -> config -> configured logger -> i18n.
        app::core::Bootstrap bootstrap;
        bootstrap.run();

#ifdef QT_FRONTEND_MODE
        // Qt frontend: its own composition root (QtInitRoot) builds the same
        // model + presenters + integration the GTK/console paths do, but over a
        // QApplication event loop. Isolated behind this #ifdef so gtkmm and Qt
        // headers never share a translation unit.
        QApplication qtApp(argc, argv);
        app::qt::QtInitRoot qtRoot(bootstrap);
        // run() returns false when the operator cancels the auth login gate;
        // skip the event loop and exit cleanly, like the GTK path.
        if (!qtRoot.run()) {
            return 0;
        }
        return QApplication::exec();
#elif defined(MCP_MODE)
        // MCP server: a fourth, headless consumer. Its own composition root
        // builds the same model + presenters + historian the other frontends
        // use, then serves the Model Context Protocol over stdio. It does not
        // build the integration backends, so this branch skips them entirely.
        app::mcp::McpInitRoot mcpRoot(bootstrap);
        return mcpRoot.run(protocolOut);
#else
        // Integration backends -- opt-in per deployment via JSON.
        // Stack-owned through main() so RAII shuts them down on exit.
        auto& config = app::config::ConfigManager::instance();
        // Integration backends are built (not started) by the shared
        // bootstrap so the GTK, console and Qt frontends compose the same
        // protocol set from one place. The bundle is stack-owned through
        // main() so RAII shuts the backends down on exit; the manager is
        // started below after auth / historian are wired.
        auto integrationServices =
            app::integration::buildIntegrationServices(config,
                                                       bootstrap.logger());
        auto& integration = *integrationServices.manager;

        // Auth + Historian stacks. Declared in construction order so
        // destruction reverses naturally. See registerAuth() /
        // registerHistorian() for the wiring + degraded-open paths.
        app::auth::Session                                authSession;
        std::unique_ptr<app::auth::SqliteUserRepository>  authRepo;
        std::unique_ptr<app::auth::Argon2PasswordHasher>  authHasher;
        std::unique_ptr<app::auth::SqliteAuditLogger>     auditLogger;
        std::unique_ptr<app::auth::AuthService>           authService;
        std::unique_ptr<app::historian::SqliteHistoryStore>   historyStore;
        std::unique_ptr<app::historian::HistorianBridge>      historianBridge;
        std::unique_ptr<app::historian::HistorianMaintenance> historianMaintenance;

        if (config.isAuthEnabled()) {
            registerAuth(config, bootstrap.logger(),
                         authRepo, authHasher, auditLogger,
                         authService, authSession);
        }

        if (config.isHistorianEnabled()) {
            registerHistorian(config, bootstrap.logger(),
                              historyStore, historianBridge,
                              historianMaintenance);
        }

        integration.startAll();

#ifdef CONSOLE_MODE
        (void)argc; (void)argv;
        app::console::InitConsole console(bootstrap);
        console.run();
        integration.stopAll();
        return kExitOk;
#else
        auto& app = app::core::Application::instance();
        std::unique_ptr<app::presenter::UsersPresenter> usersPresenter;
        CompositionRoot root{
            historyStore, authService, authSession, auditLogger,
            integrationServices.secondaryModel, authRepo, authHasher,
            usersPresenter};
        wireApplicationServices(app, integration, root);

        app.initialize(bootstrap, argc, argv);   // throws DatabaseInitError on DB failure

        // Inject the app-wide logger into the SimulatedModel singleton so
        // its state transitions and tick traces show up in the normal log
        // stream. Done here (not in Application::initDatabase) because
        // including SimulatedModel.h there would pull in
        // ProductionTypes::ERROR after gtkmm has already defined the
        // wingdi.h ERROR=0 macro.
        app::model::SimulatedModel::instance().setLogger(app.logger());

        const int result = app.run(argc, argv);
        integration.stopAll();
        app.shutdown();
        return result;
#endif
#endif  // QT_FRONTEND_MODE
    } catch (const app::core::CriticalStartupError& e) {
        app::core::reportFatalStartup(e, kConsoleMode);
        return kExitStartupFatal;
    } catch (const std::exception& e) {
        app::core::reportUnexpectedFatal(e.what(), kConsoleMode);
        return kExitUnexpectedFatal;
    } catch (...) {
        app::core::reportUnexpectedFatal(
            "Unknown (non-std::exception) fatal error reached main.",
            kConsoleMode);
        return kExitUnknownFatal;
    }
}
