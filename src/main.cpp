#include "src/core/Application.h"
#include "src/core/i18n.h"
#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"
#include "src/model/SimulatedModel.h"
#include <cstdlib>

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Use Cairo renderer on Windows to avoid GL flicker
    _putenv_s("GSK_RENDERER", "cairo");
#endif

    // Load config early so we know the language preference before
    // constructing any translatable UI. Best-effort: if config is missing,
    // getLanguage() returns "auto" and i18n falls back to OS locale.
    auto& config = app::config::ConfigManager::instance();
    (void)config.initialize();
    const std::string language = config.getLanguage();

    // Bind gettext catalogs and select locale (config-driven).
    app::core::initI18n(app::config::defaults::kLocaleDir, language.c_str());

    auto& app = app::core::Application::instance();

    if (!app.initialize(argc, argv)) {
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
}
