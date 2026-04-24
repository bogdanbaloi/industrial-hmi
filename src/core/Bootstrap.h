#pragma once

#include "src/core/LoggerBase.h"

#include <memory>
#include <string>
#include <vector>

namespace app::core {

/// Staged startup orchestrator shared by every entry point (GTK desktop
/// today, headless console next, future services tomorrow).
///
/// Resolves the classic "config needs a logger, logger needs config"
/// chicken-and-egg with a two-phase logger:
///
///   1. Bootstrap logger  -- minimal stderr-only ConsoleLogger at INFO.
///      Always available, zero dependencies.
///   2. ConfigManager::initialize() -- uses the bootstrap logger for any
///      degraded-config warnings.
///   3. Configured logger -- built from config (file path, rotation, level,
///      console enable flag). If the swap fails (bad path, permission
///      denied), we keep the bootstrap logger and record a warning.
///   4. ConfigManager::applyI18n() -- policy-driven call into the i18n
///      subsystem. Uses the final logger for graceful-degradation notes.
///
/// Nothing here touches GTK or a UI framework. Bootstrap is GTK-free
/// by construction, so both the desktop and the console binaries can
/// share this type.
///
/// Ownership: Bootstrap owns the Logger via `unique_ptr`. Consumers
/// (Application, InitConsole) borrow it via `logger()`. Bootstrap must
/// out-live any consumer that holds a reference -- in practice this
/// means Bootstrap is a stack object in `main()`.
class Bootstrap {
public:
    Bootstrap();
    ~Bootstrap();

    Bootstrap(const Bootstrap&)            = delete;
    Bootstrap& operator=(const Bootstrap&) = delete;
    Bootstrap(Bootstrap&&)                 = delete;
    Bootstrap& operator=(Bootstrap&&)      = delete;

    /// Run the staged startup. Always succeeds; worst case leaves the
    /// app running with the bootstrap logger + source-English i18n.
    /// Accumulates any warnings in `warnings()` so they can be surfaced
    /// to the user by the active frontend.
    void run();

    /// Borrow the current logger. Valid while Bootstrap is alive.
    Logger& logger() { return *logger_; }

    /// Warnings collected during bootstrap (missing config file, log
    /// file permission denied, etc.). Frontends typically render these
    /// at the first opportunity -- a GTK dialog, a console banner line,
    /// an audit-log entry.
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::unique_ptr<Logger>  logger_;
    std::vector<std::string> warnings_;
};

}  // namespace app::core
