#include "src/core/Bootstrap.h"

#include "src/core/LoggerImpl.h"
#include "src/config/ConfigManager.h"

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
    // Stage 2 — ConfigManager::initialize(). Injects the current logger
    // so ConfigManager can warn if the JSON file is missing or corrupt.
    // Failure is non-fatal: defaults apply, a warning is recorded.
    // -----------------------------------------------------------------
    auto& config = config::ConfigManager::instance();
    config.setLogger(*logger_);
    if (!config.initialize()) {
        warnings_.emplace_back(
            "Configuration file not found. Using default settings.");
        // Keep bootstrap logger + "auto" i18n; no upgrade possible.
    } else {
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

        // -------------------------------------------------------------
        // Stage 3 — replace the bootstrap logger with a configured one
        // (level / path / rotation / console-enable from config).
        // -------------------------------------------------------------
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
    }

    // -----------------------------------------------------------------
    // Stage 4 — i18n. Policy lives in ConfigManager; Bootstrap just
    // triggers it. ConfigManager decides language (explicit vs "auto"
    // vs degraded-config fallback) and logs through the current logger.
    // -----------------------------------------------------------------
    config.applyI18n();

    logger_->info("Bootstrap complete ({} warning{})",
                  warnings_.size(),
                  warnings_.size() == 1 ? "" : "s");
}

}  // namespace app::core
