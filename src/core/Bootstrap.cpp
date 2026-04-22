#include "src/core/Bootstrap.h"

#include "src/core/LoggerImpl.h"
#include "src/core/StartupErrors.h"
#include "src/core/i18n.h"
#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"

#include <cstdlib>   // std::getenv
#include <format>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace app::core {

Bootstrap::Bootstrap() = default;
Bootstrap::~Bootstrap() {
    if (logger_) {
        logger_->flush();
        logger_->shutdown();
    }
}

void Bootstrap::run() {
    // -----------------------------------------------------------------
    // Stage 1 — bootstrap logger (stderr only, INFO level).
    // Available before any config is read so stages 2+ can log warnings.
    // -----------------------------------------------------------------
    logger_ = std::make_unique<Logger>(createConsoleLogger(LogLevel::INFO));

    // -----------------------------------------------------------------
    // Stage 1.5 — bind gettext to the OS locale BEFORE we touch config.
    // If config turns out to be missing / corrupt, the fatal error
    // message surfaced through StartupDialog needs to be translated —
    // but the user's preferred language lives *inside* config, which
    // is the very thing that failed. "auto" falls back to the OS /
    // environment locale, which is the best signal we have at this
    // point. Once config loads, Stage 4 re-binds to the explicit
    // language from config (may or may not match OS locale).
    // -----------------------------------------------------------------
    // Capture shell-provided env vars BEFORE initI18n potentially
    // overwrites them, then log both pre- and post-bind values. Keeps
    // the i18n policy observable in the log without needing a debugger.
    const char* preLANGUAGE = std::getenv("LANGUAGE");
    const char* preLANG     = std::getenv("LANG");
    const std::string preLangSnapshot =
        std::string("LANGUAGE=") + (preLANGUAGE && *preLANGUAGE ? preLANGUAGE : "(unset)")
        + " LANG="               + (preLANG     && *preLANG     ? preLANG     : "(unset)");

    app::core::initI18n(config::defaults::kLocaleDir, "auto");

    const char* postLANGUAGE = std::getenv("LANGUAGE");
    const char* postLANG     = std::getenv("LANG");
    logger_->info("Bootstrap i18n (auto): shell[{}] -> effective[LANGUAGE={} LANG={}]",
                  preLangSnapshot,
                  postLANGUAGE && *postLANGUAGE ? postLANGUAGE : "(unset)",
                  postLANG     && *postLANG     ? postLANG     : "(unset)");

    // -----------------------------------------------------------------
    // Stage 2 — ConfigManager::initialize(). Missing / corrupt config
    // is treated as FATAL: the app refuses to start rather than
    // silently running with built-in defaults. Operational degradation
    // (log file, i18n fallback) remains non-fatal and just records
    // warnings that the frontend surfaces later.
    // -----------------------------------------------------------------
    auto& config = config::ConfigManager::instance();
    config.setLogger(*logger_);
    if (!config.initialize()) {
        // At this stage we cannot distinguish "file missing" from
        // "file unparseable" without more instrumentation in
        // ConfigManager::loadConfig. Err on the side of ConfigMissing
        // because that is the overwhelmingly common cause and the
        // operator-facing message is equally actionable either way.
        //
        // Message is localised via gettext (bound above to OS locale).
        // Missing translations degrade to the source English — which
        // is acceptable for an unrecoverable deployment error.
        throw ConfigMissingError(std::vformat(
            _("Could not load configuration from {}. "
              "Re-install the application or restore the config file."),
            std::make_format_args(config::defaults::kConfigPath)));
    }

#ifdef _WIN32
    // Optional: attach to parent console so child GUI processes can
    // print to the shell that launched them. Controlled by config.
    if (config.getLogConsoleEnabled()) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            (void)freopen("CONOUT$", "w", stdout);
            (void)freopen("CONOUT$", "w", stderr);
        }
    }
#endif

    // -----------------------------------------------------------------
    // Stage 3 — replace the bootstrap logger with a configured one
    // (level / path / rotation / console-enable from config). Failure
    // here is NOT fatal — we keep the bootstrap stderr logger and
    // record a warning. The app is still usable without a log file.
    // -----------------------------------------------------------------
    try {
        auto configured = std::make_unique<Logger>(
            createConfiguredLogger(
                config.getLogFilePath(),
                config.getLogLevel(),
                config.getLogMaxFileSize(),
                config.getLogMaxFiles(),
                config.getLogConsoleEnabled()));
        logger_->flush();
        logger_ = std::move(configured);
        config.setLogger(*logger_);      // re-point config at the new one
    } catch (const std::exception& e) {
        warnings_.emplace_back(
            std::string("Log file could not be opened: ") + e.what()
            + ". Logging to console only.");
        // Keep bootstrap logger — still functional, just not persisted.
    }

    // -----------------------------------------------------------------
    // Stage 4 — i18n. Policy lives in ConfigManager; Bootstrap just
    // triggers it. ConfigManager decides language and logs through
    // the current logger. Never throws.
    // -----------------------------------------------------------------
    config.applyI18n();

    logger_->info("Bootstrap complete ({} warning{})",
                  warnings_.size(),
                  warnings_.size() == 1 ? "" : "s");
}

}  // namespace app::core
