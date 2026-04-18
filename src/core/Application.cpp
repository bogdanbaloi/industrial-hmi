// GTK headers must come before Windows headers to avoid
// wingdi.h macro conflicts (ERROR, IN, OUT, WINDING, IGNORE)
#include "src/gtk/view/MainWindow.h"

#include "src/core/Application.h"
#include "src/core/LoggerImpl.h"
#include "src/config/ConfigManager.h"
#include "src/model/DatabaseManager.h"
#include "src/model/ModelContext.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace app::core {

Application& Application::instance() {
    static Application app;
    return app;
}

Application::~Application() {
    if (initialized_) {
        shutdown();
    }
}

bool Application::initialize(int argc, char* argv[]) {
    if (initialized_) return true;

    // Phase 1: Config (may fail gracefully - defaults used)
    initConfig();

    // Phase 2: Console attachment (Windows GUI apps)
#ifdef _WIN32
    if (config::ConfigManager::instance().getLogConsoleEnabled()) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
#endif

    // Phase 3: Logging (uses config values or defaults)
    initLogging();

    logger_->info("Application starting");

    // Phase 4: Database (may fail gracefully)
    initDatabase();

    // Log any warnings that occurred during startup
    for (const auto& warning : startupWarnings_) {
        logger_->warn("{}", warning);
    }

    initialized_ = true;
    return true;
}

int Application::run(int argc, char* argv[]) {
    auto gtkApp = Gtk::Application::create(config::defaults::kGtkAppId);

    gtkApp->signal_activate().connect([this, &gtkApp]() {
        auto* window = new MainWindow();
        gtkApp->add_window(*window);

        // Process all pending idle callbacks (initial data population)
        while (g_main_context_iteration(nullptr, false)) {}

        window->present();

        if (hasStartupWarnings()) {
            showStartupWarnings();
        }
    });

    return gtkApp->run(argc, argv);
}

void Application::shutdown() {
    if (!initialized_) return;

    if (logger_) {
        logger_->info("Application shutting down");
    }

    model::ModelContext::instance().stop();

    if (logger_) {
        logger_->flush();
        logger_->shutdown();
    }

    initialized_ = false;
}

Logger& Application::logger() {
    // Tests instantiate Presenters directly without ever calling
    // Application::initialize(). Those Presenters now emit debug/trace
    // messages during normal flow — if `logger_` is null we'd crash on
    // the first log call. Fall back to a lazy-constructed NullLogger so
    // unit tests stay hermetic.
    if (!logger_) {
        static Logger nullLogger{std::make_unique<NullLogger>()};
        return nullLogger;
    }
    return *logger_;
}

void Application::initConfig() {
    auto& config = config::ConfigManager::instance();
    if (!config.initialize()) {
        startupWarnings_.emplace_back(
            "Configuration file not found. Using default settings.");
    }
}

void Application::initLogging() {
    auto& config = config::ConfigManager::instance();

    try {
        logger_ = std::make_unique<Logger>(
            createConfiguredLogger(
                config.getLogFilePath(),
                config.getLogLevel(),
                config.getLogMaxFileSize(),
                config.getLogMaxFiles(),
                config.getLogConsoleEnabled()
            )
        );
    } catch (const std::exception& e) {
        // File logger failed (permissions?), fall back to console only
        logger_ = std::make_unique<Logger>(
            createConsoleLogger(LogLevel::INFO));
        startupWarnings_.emplace_back(
            std::string("Log file could not be opened: ") + e.what()
            + ". Logging to console only.");
    }
}

void Application::initDatabase() {
    model::ModelContext::instance().setLogger(*logger_);

    auto& db = model::DatabaseManager::instance();
    db.setLogger(*logger_);

    if (!db.initialize()) {
        startupWarnings_.emplace_back(
            "Database initialization failed. Product features may not work.");
    } else {
        logger_->info("Database initialized");
    }
}

void Application::showStartupWarnings() {
    std::string message;
    for (const auto& warning : startupWarnings_) {
        if (!message.empty()) message += "\n\n";
        message += warning;
    }

    auto* dialog = new Gtk::MessageDialog(
        "Startup Warnings",
        false,
        Gtk::MessageType::WARNING,
        Gtk::ButtonsType::OK);

    dialog->set_secondary_text(message);
    dialog->set_modal(true);

    dialog->signal_response().connect([dialog](int) {
        delete dialog;
    });

    dialog->present();
}

}  // namespace app::core
