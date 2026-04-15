#include "src/core/Application.h"
#include "src/core/i18n.h"
#include "src/config/ConfigManager.h"
#include <cstdlib>

#ifndef LOCALE_DIR
#define LOCALE_DIR "locale"
#endif

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
    app::core::initI18n(LOCALE_DIR, language.c_str());

    auto& app = app::core::Application::instance();

    if (!app.initialize(argc, argv)) {
        return 1;
    }

    int result = app.run(argc, argv);
    app.shutdown();
    return result;
}
