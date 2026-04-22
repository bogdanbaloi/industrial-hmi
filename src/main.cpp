#include "src/core/Bootstrap.h"
#include "src/core/StartupDialog.h"
#include "src/core/StartupErrors.h"
#include "src/model/SimulatedModel.h"
#include <cstdlib>
#include <exception>

#ifdef CONSOLE_MODE
// Headless console front-end (added in Phase 1). For Phase 0 the
// console header doesn't exist yet — the binary target is only
// introduced when the console sources land.
#  include "src/console/InitConsole.h"
#else
#  include "src/core/Application.h"
#endif

namespace {

// Compile-time tag: true for the console binary, false for the GTK one.
// Drives the fatal-reporter between stderr and MessageBoxW.
constexpr bool kConsoleMode =
#ifdef CONSOLE_MODE
    true;
#else
    false;
#endif

// Process exit codes — documented so CI / shell scripts can branch on them.
// Marked [[maybe_unused]] because the set used by a given build depends
// on which branch of the CONSOLE_MODE #ifdef is active (the GTK path
// propagates the GTK main-loop's own return code via `app.run(...)`).
[[maybe_unused]] constexpr int kExitOk              = 0;
[[maybe_unused]] constexpr int kExitUnexpectedFatal = 1;
[[maybe_unused]] constexpr int kExitStartupFatal    = 2;
[[maybe_unused]] constexpr int kExitUnknownFatal    = 3;

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Use Cairo renderer on Windows to avoid GL flicker in GTK4.
    _putenv_s("GSK_RENDERER", "cairo");
#endif

    // Top-level exception guard. Every fatal condition at or below
    // Bootstrap / Application / InitConsole surfaces here and is
    // reported via the appropriate native channel.
    //
    //   kExitStartupFatal (2)  -> deployment problem (config / DB)
    //   kExitUnexpectedFatal (1) -> std::exception escaping the app
    //   kExitUnknownFatal (3)  -> non-std exception (shouldn't happen)
    try {
        // Staged startup: logger -> config -> configured logger -> i18n.
        // Throws CriticalStartupError on fatal config issues.
        app::core::Bootstrap bootstrap;
        bootstrap.run();

#ifdef CONSOLE_MODE
        (void)argc; (void)argv;
        app::console::InitConsole console(bootstrap);
        console.run();
        return kExitOk;
#else
        auto& app = app::core::Application::instance();
        app.initialize(bootstrap, argc, argv);   // throws DatabaseInitError on DB failure

        // Inject the app-wide logger into the SimulatedModel singleton so
        // its state transitions and tick traces show up in the normal log
        // stream. Done here (not in Application::initDatabase) because
        // including SimulatedModel.h there would pull in
        // ProductionTypes::ERROR after gtkmm has already defined the
        // wingdi.h ERROR=0 macro.
        app::model::SimulatedModel::instance().setLogger(app.logger());

        const int result = app.run(argc, argv);
        app.shutdown();
        return result;
#endif
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
