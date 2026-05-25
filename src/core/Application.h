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

namespace app::auth {
class AuthService;         // forward decl -- non-owning pointer
class Session;             // forward decl -- non-owning pointer
class AuditLogger;         // forward decl -- non-owning pointer
}

namespace app::presenter {
class UsersPresenter;      // forward decl -- non-owning pointer
}

namespace app::model {
class ProductionModel;     // forward decl -- non-owning pointer
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

    /// Inject the auth service + session. When both pointers are non-
    /// null AND `auth.enabled` is true in config, `run()` shows a
    /// modal LoginDialog before the main window appears. Cancel from
    /// the dialog short-circuits the rest of run() and the binary
    /// exits with code 0 (user declined to sign in -- not an error).
    ///
    /// Non-owning; both objects live in main()'s stack frame next to
    /// the user repository + hasher.
    void setAuth(auth::AuthService* service, auth::Session* session) noexcept {
        authService_ = service;
        authSession_ = session;
    }

    /// Inject the audit logger. When set together with the session,
    /// MainWindow propagates both into the presenter constructors so
    /// every operator-initiated action records a row.
    void setAuditLogger(auth::AuditLogger* audit) noexcept {
        auditLogger_ = audit;
    }

    [[nodiscard]] auth::AuthService* authService() const noexcept {
        return authService_;
    }
    [[nodiscard]] auth::Session* authSession() const noexcept {
        return authSession_;
    }
    [[nodiscard]] auth::AuditLogger* auditLogger() const noexcept {
        return auditLogger_;
    }

    /// Inject the users management presenter. Non-owning; the
    /// concrete is built in main() next to the user repository +
    /// hasher + session and lives the same scope. MainWindow
    /// registers the UsersPage (admin-only) when this pointer is
    /// non-null AND the current session holds Admin.
    void setUsersPresenter(presenter::UsersPresenter* p) noexcept {
        usersPresenter_ = p;
    }
    [[nodiscard]] presenter::UsersPresenter* usersPresenter() const noexcept {
        return usersPresenter_;
    }

    /// Inject the secondary-station production model. Only set when
    /// multi-station mode is enabled (config flag
    /// `ui.multistation_enabled` true). When non-null, MainWindow
    /// builds TWO DashboardPresenter instances -- one for the primary
    /// (SimulatedModel singleton) and one for this secondary model --
    /// and replaces the Dashboard tab with the
    /// MultiStationDashboardPage. The PrimaryToSecondaryBridge that links
    /// the two models is registered separately on the
    /// IntegrationManager so it appears in the sidebar BackendHealthBar
    /// like every other integration backend.
    ///
    /// Non-owning; the secondary model is constructed in main() alongside
    /// the bridge and lives the same scope as the IntegrationManager.
    void setSecondaryProductionModel(model::ProductionModel* m) noexcept {
        secondaryProductionModel_ = m;
    }
    [[nodiscard]] model::ProductionModel* secondaryProductionModel() const noexcept {
        return secondaryProductionModel_;
    }

private:
    Application() = default;
    ~Application();

    void showStartupWarnings();

    Logger*                 logger_ = nullptr;   // non-owning -- Bootstrap owns
    integration::IntegrationManager* integrationManager_ = nullptr;  // non-owning
    historian::HistoryReader* historyReader_ = nullptr;              // non-owning
    auth::AuthService*      authService_ = nullptr;                  // non-owning
    auth::Session*          authSession_ = nullptr;                  // non-owning
    auth::AuditLogger*      auditLogger_ = nullptr;                  // non-owning
    presenter::UsersPresenter* usersPresenter_ = nullptr;             // non-owning
    model::ProductionModel* secondaryProductionModel_ = nullptr;          // non-owning -- multi-station secondary
    std::vector<std::string> startupWarnings_;
    bool                    initialized_{false};
};

}  // namespace app::core
