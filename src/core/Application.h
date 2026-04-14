#pragma once

#include "LoggerBase.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace app::core {

/// Application lifecycle manager
///
/// Owns initialization and teardown of all subsystems in correct order:
///   1. Config  (load JSON settings, fallback to defaults)
///   2. Logging (create configured logger)
///   3. Database (initialize SQLite)
///   4. UI (create GTK application, run main loop)
///
/// If any non-critical subsystem fails (config file missing, DB error),
/// the application starts with degraded functionality and shows a
/// startup report dialog listing all warnings.
///
/// Shutdown reverses the order: stop I/O, flush logs, release resources.
class Application {
public:
    static Application& instance();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Initialize all subsystems. Call once from main().
    /// @return true if application can start (possibly with warnings)
    [[nodiscard]] bool initialize(int argc, char* argv[]);

    /// Run the GTK main loop. Blocks until the window is closed.
    /// @return exit code (0 = success)
    int run(int argc, char* argv[]);

    /// Shutdown all subsystems in reverse init order.
    void shutdown();

    /// Access the application-wide logger
    Logger& logger();

    /// Check if there were startup warnings
    [[nodiscard]] bool hasStartupWarnings() const { return !startupWarnings_.empty(); }

    /// Get startup warnings for display
    [[nodiscard]] const std::vector<std::string>& startupWarnings() const { return startupWarnings_; }

private:
    Application() = default;
    ~Application();

    void initConfig();
    void initLogging();
    void initDatabase();
    void showStartupWarnings();

    std::unique_ptr<Logger> logger_;
    std::vector<std::string> startupWarnings_;
    bool initialized_{false};
};

}  // namespace app::core
