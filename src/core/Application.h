#pragma once

#include "LoggerBase.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace app::integration {
class IntegrationManager;  // forward decl -- non-owning pointer
}

namespace app::historian {
class HistoryReader;       // forward decl -- non-owning pointer
}

namespace app::core {

class Bootstrap;  // forward decl -- defined in Bootstrap.h

/// Application lifecycle manager (GTK front-end)
///
/// Owns the GTK-specific subsystems that sit on top of a prepared
/// Bootstrap (logger + config + i18n already resolved):
///
///   1. Database (SQLite init)
///   2. GTK application + main window
///   3. Startup warnings dialog
///   4. Shutdown order reversal
///
/// Bootstrap (see src/core/Bootstrap.h) handles the shared part of the
/// lifecycle so the future console entry point can reuse it without
/// dragging in GTK.
class Application {
public:
    static Application& instance();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Initialize GTK-specific subsystems on top of an already-run
    /// Bootstrap. Borrows the Bootstrap's logger -- the Bootstrap must
    /// out-live the Application (in practice: both are stack-owned by
    /// `main()` with Bootstrap declared first).
    ///
    /// @throw DatabaseInitError if SQLite cannot be initialised. The
    ///        caller (main) catches via the CriticalStartupError
    ///        hierarchy and reports through the native dialog.
    void initialize(Bootstrap& bootstrap,
                    int argc,
                    char* argv[]);

    /// Run the GTK main loop. Blocks until the window is closed.
    /// @return exit code (0 = success)
    int run(int argc, char* argv[]);

    /// Shutdown GTK-specific subsystems. Bootstrap's own teardown
    /// (logger flush) happens when the Bootstrap object goes out of
    /// scope in `main()`.
    void shutdown();

    /// Access the application-wide logger (borrowed from Bootstrap).
    Logger& logger();

    /// Any warnings accumulated during Bootstrap + Application init.
    [[nodiscard]] bool hasStartupWarnings() const { return !startupWarnings_.empty(); }
    [[nodiscard]] const std::vector<std::string>& startupWarnings() const { return startupWarnings_; }

    /// Inject the IntegrationManager so MainWindow can mount the
    /// backend-health bar in the sidebar. Optional -- when null, the
    /// bar is hidden and no polling timer is armed. main() calls this
    /// after constructing the manager but before run(). Non-owning;
    /// the manager outlives the Application by stack discipline.
    void setIntegrationManager(integration::IntegrationManager* manager) noexcept {
        integrationManager_ = manager;
    }

    [[nodiscard]] integration::IntegrationManager* integrationManager() const noexcept {
        return integrationManager_;
    }

    /// Inject the historian's read interface so MainWindow can mount
    /// the History page. Optional -- when null, the page is skipped
    /// (historian is opt-in via app-config.json). Non-owning; the
    /// store lives in main()'s stack frame next to the bridge.
    void setHistoryReader(historian::HistoryReader* reader) noexcept {
        historyReader_ = reader;
    }

    [[nodiscard]] historian::HistoryReader* historyReader() const noexcept {
        return historyReader_;
    }

private:
    Application() = default;
    ~Application();

    void showStartupWarnings();

    Logger*                 logger_ = nullptr;   // non-owning -- Bootstrap owns
    integration::IntegrationManager* integrationManager_ = nullptr;  // non-owning
    historian::HistoryReader* historyReader_ = nullptr;              // non-owning
    std::vector<std::string> startupWarnings_;
    bool                    initialized_{false};
};

}  // namespace app::core
