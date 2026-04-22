// GTK headers must come before Windows headers to avoid
// wingdi.h macro conflicts (ERROR, IN, OUT, WINDING, IGNORE)
#include "src/gtk/view/MainWindow.h"

#include "src/core/Application.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerImpl.h"
#include "src/config/config_defaults.h"
#include "src/model/ModelContext.h"

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

void Application::initialize(Bootstrap& bootstrap, int /*argc*/, char* /*argv*/[]) {
    if (initialized_) return;

    // Bootstrap has already prepared logger + config + i18n + database.
    // Adopt the shared warnings list and borrow the logger.
    logger_ = &bootstrap.logger();
    startupWarnings_ = bootstrap.warnings();

    logger_->info("Application starting (GTK frontend)");

    // Flush any accumulated warnings so they hit the log before the UI
    // opens; the same list is re-shown via showStartupWarnings() later.
    for (const auto& warning : startupWarnings_) {
        logger_->warn("{}", warning);
    }

    initialized_ = true;
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

    // NOTE: Logger flush/shutdown is owned by Bootstrap — when the
    // Bootstrap object in main() goes out of scope it will flush the
    // final records. Application only borrows the pointer.
    logger_ = nullptr;
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
