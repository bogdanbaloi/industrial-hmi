#pragma once

#include <sigc++/connection.h>

#include <chrono>
#include <memory>
#include <string>

class QTimer;

namespace app::core {
class Bootstrap;
}

namespace app {
class DashboardPresenter;
class ProductsPresenter;
class BackendHealthPresenter;
}

namespace app::presenter {
class AlertCenter;
class QualityInspectionPresenter;
}

namespace app::ml {
class ImageDecoder;
class FakeImageClassifier;
}

namespace app::integration {
struct IntegrationServices;
}

namespace app::historian {
class SqliteHistoryStore;
class HistorianBridge;
class HistorianMaintenance;
}

namespace app::auth {
class Session;
class SqliteUserRepository;
class Argon2PasswordHasher;
class SqliteAuditLogger;
class AuthService;
}

namespace app::presenter {
class UsersPresenter;
}

namespace app::view {
class QtMainWindow;
class QtPaletteManager;
class QtGettextTranslator;
}

namespace app::qt {

/// Composition root for the Qt desktop frontend. The direct counterpart to
/// `app::console::InitConsole`: it builds the same Model + Presenter + View
/// collaborators, wires them, drives the simulation tick, and shows the shell
/// window. The existence of a third composition root over the SAME presenter
/// and model layer is the concrete proof that the MVP View seam is
/// toolkit-independent (ADR-0020, extends ADR-0002).
///
/// The tick runs on a UI-thread QTimer, so presenter callbacks reach the
/// widgets on the UI thread without cross-thread marshalling.
///
/// Ownership: borrows the already-prepared Bootstrap (logger + config).
/// Bootstrap must out-live this object; in practice both are stack objects in
/// `main()`.
class QtInitRoot {
public:
    explicit QtInitRoot(core::Bootstrap& bootstrap);
    ~QtInitRoot();

    QtInitRoot(const QtInitRoot&)            = delete;
    QtInitRoot& operator=(const QtInitRoot&) = delete;
    QtInitRoot(QtInitRoot&&)                 = delete;
    QtInitRoot& operator=(QtInitRoot&&)      = delete;

    /// Build the presenter + shell, attach the observer, start the tick timer
    /// and show the window. Non-blocking: the Qt event loop is run by
    /// `QApplication::exec()` back in `main()`.
    ///
    /// Returns false when auth is enabled and the operator cancels the login
    /// gate -- `main()` then skips `QApplication::exec()` and exits cleanly,
    /// mirroring the GTK activation handler that bails with no window.
    [[nodiscard]] bool run();

private:
    /// Push the active-alarm count onto the sidebar Alerts badge, marshalled to
    /// the UI thread (the AlertCenter signal may fire on a backend thread).
    void refreshAlertsBadge();

    /// Build the historian stack (SQLite store + model bridge + retention
    /// worker) when enabled in config. Degraded-open policy: a store that fails
    /// to open is dropped and the History page never mounts, matching the GTK
    /// frontend and main()'s registerHistorian.
    void buildHistorian();

    /// Apply a language selection from the Settings picker: persist it, rebind
    /// the shared gettext catalog, then reinstall the translator so Qt
    /// broadcasts QEvent::LanguageChange and every widget retranslates live.
    void changeLanguage(const std::string& code);

    /// Build the auth stack (user store + password hasher + audit log + service
    /// + session) when enabled in config. Same config-gated, degraded-open path
    /// main()'s registerAuth uses.
    void buildAuth();

    /// Push the signed-in user's identity onto the sidebar footer (name + role),
    /// or a signed-out placeholder. Connected to Session::signalChanged.
    void refreshUserIdentity();

    /// Build the shell window, attach observers + signals, populate it and show
    /// it. Runs the one-time presenter initialize() on the first build only, so
    /// a sign-out rebuild does not reset the running simulation.
    void buildAndShowWindow();

    /// Detach observers + signals and drop the window. Shared by the destructor
    /// and the sign-out rebuild.
    void teardownWindow();

    /// Sign-out flow: log out (clears + audits the session), re-show the login,
    /// then rebuild the shell so role-gated nav matches the new user. A cancelled
    /// re-login quits the app. Wired to the sidebar's Sign out control.
    void signOut();

    core::Bootstrap&                    bootstrap_;
    std::unique_ptr<integration::IntegrationServices> integrationServices_;
    std::unique_ptr<presenter::AlertCenter>           alertCenter_;
    std::unique_ptr<BackendHealthPresenter>           backendHealthPresenter_;
    std::unique_ptr<DashboardPresenter>     dashboardPresenter_;
    std::unique_ptr<ProductsPresenter>      productsPresenter_;
    std::unique_ptr<ml::ImageDecoder>            imageDecoder_;
    std::unique_ptr<ml::FakeImageClassifier>     imageClassifier_;
    std::unique_ptr<presenter::QualityInspectionPresenter> inspectionPresenter_;
    std::unique_ptr<historian::SqliteHistoryStore>   historyStore_;
    std::unique_ptr<historian::HistorianBridge>      historianBridge_;
    std::unique_ptr<historian::HistorianMaintenance> historianMaintenance_;
    std::unique_ptr<auth::Session>                   authSession_;
    std::unique_ptr<auth::SqliteUserRepository>      authRepo_;
    std::unique_ptr<auth::Argon2PasswordHasher>      authHasher_;
    std::unique_ptr<auth::SqliteAuditLogger>         auditLogger_;
    std::unique_ptr<auth::AuthService>               authService_;
    std::unique_ptr<presenter::UsersPresenter>       usersPresenter_;
    std::unique_ptr<view::QtGettextTranslator> translator_;
    std::unique_ptr<view::QtPaletteManager> paletteManager_;
    std::unique_ptr<view::QtMainWindow>     window_;
    std::unique_ptr<QTimer>                 tickTimer_;
    sigc::connection                        systemStateConn_;
    sigc::connection                        dashboardStateConn_;
    sigc::connection                        alertsBadgeConn_;
    sigc::connection                        sessionConn_;
    bool                                    windowBuiltOnce_{false};

    static constexpr std::chrono::milliseconds kTickPeriod{2000};
};

}  // namespace app::qt
