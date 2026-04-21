#include "src/core/Bootstrap.h"
#include "src/model/SimulatedModel.h"
#include <cstdlib>

#ifdef CONSOLE_MODE
// Headless console front-end (added in Phase 1). For Phase 0 the
// console header doesn't exist yet — we only build the GTK binary
// via the default target until the console sources land.
#  include "src/console/InitConsole.h"
#else
#  include "src/core/Application.h"
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Use Cairo renderer on Windows to avoid GL flicker in GTK4.
    _putenv_s("GSK_RENDERER", "cairo");
#endif

    // Staged startup: logger -> config -> configured logger -> i18n.
    // Always succeeds; degraded-config cases are recorded as warnings.
    app::core::Bootstrap bootstrap;
    bootstrap.run();

#ifdef CONSOLE_MODE
    (void)argc; (void)argv;
    app::console::InitConsole console(bootstrap);
    console.run();
    return 0;
#else
    auto& app = app::core::Application::instance();
    if (!app.initialize(bootstrap, argc, argv)) {
        return 1;
    }

    // Inject the app-wide logger into the SimulatedModel singleton so its
    // state transitions and tick traces show up in the normal log stream.
    // Done here (not in Application::initDatabase) because including
    // SimulatedModel.h there would pull in ProductionTypes::ERROR after
    // gtkmm has already defined the wingdi.h ERROR=0 macro.
    app::model::SimulatedModel::instance().setLogger(app.logger());

    int result = app.run(argc, argv);
    app.shutdown();
    return result;
#endif
}
