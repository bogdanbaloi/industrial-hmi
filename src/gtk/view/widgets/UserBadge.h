#pragma once

#include "src/auth/AuthService.h"
#include "src/auth/Session.h"

#include <gtkmm.h>

#include <chrono>
#include <functional>

namespace app::presenter {
class UsersPresenter;     // forward decl -- non-owning pointer
}

namespace app::view {

class AvatarWidget;       // forward decl -- non-owning pointer

/// Sidebar widget that surfaces the currently-logged-in user.
///
/// Layout: [Avatar 32px][VBox: username + role] plus a row of buttons
/// (Profile + Sign out) below. The avatar renders the uploaded photo
/// when present, otherwise generated initials over a hashed-from-
/// username palette colour (see `AvatarPlaceholder`).
///
/// Empty / hidden when no session is set -- the only path that
/// reaches the main window goes through LoginDialog, so this widget
/// is the operator's confirmation that auth is wired and their
/// session is alive.
///
/// @design Sign out is a callback (`signal_sign_out_clicked`) rather
/// than an inline call to `service.logout()` so the host (MainWindow)
/// can additionally close itself / quit the application -- the
/// presenter / service alone can't do that without coupling to GTK.
/// The Profile button is wired in-widget because the dialog is a
/// self-contained modal and does not need MainWindow involvement.
class UserBadge : public Gtk::Box {
public:
    using SignOutAction = std::function<void()>;

    /// @param service  Auth service for logout.
    /// @param session  Auth session for "who is logged in" reads.
    /// @param users    Optional users presenter -- when non-null the
    ///                 avatar renders the uploaded photo (or initials)
    ///                 and a Profile button opens the self-service
    ///                 dialog. Null is fine; the widget falls back to
    ///                 initials-only and hides the Profile button.
    UserBadge(app::auth::AuthService&            service,
              app::auth::Session&                session,
              app::presenter::UsersPresenter*    users = nullptr);
    ~UserBadge() override = default;

    /// Caller-supplied hook for "operator clicked Sign out". The
    /// widget already invokes AuthService::logout() before firing
    /// the action -- the caller's responsibility is to dismiss the
    /// main window (and let the app quit so the next launch returns
    /// to the LoginDialog).
    void onSignOut(SignOutAction action) { signOutAction_ = std::move(action); }

    /// Refresh from the session. Called once at construction and may
    /// be re-called by the host after a logout/login cycle.
    void refresh();

    /// Switch the badge into a horizontal inline layout suitable for
    /// the multistation bottom-bar sidebar. Removes the card chrome
    /// (no padding / no border), shrinks the avatar to 28px, lays
    /// out children as
    ///   `[avatar] [username] [ROLE . since HH:MM] [Profile] [Sign out]`
    /// on one row. Must be called BEFORE the badge becomes visible
    /// (i.e. from the MainWindow code that mounts it). One-way --
    /// going back to vertical requires recreating the badge.
    void setHorizontal();

private:
    void buildUi();
    void onSignOutClicked();
    void onProfileClicked();

    app::auth::AuthService&         service_;
    app::auth::Session&             session_;
    app::presenter::UsersPresenter* users_{nullptr};

    AvatarWidget*           avatar_{nullptr};
    Gtk::Label*             usernameLabel_{nullptr};
    Gtk::Label*             roleLabel_{nullptr};
    Gtk::Button*            profileButton_{nullptr};
    Gtk::Button*            signOutButton_{nullptr};

    // Approximate sign-in time -- captured at widget construction
    // since the widget is built right after a successful LoginDialog.
    // Session has no built-in login timestamp; this keeps the change
    // local to the view rather than reaching into the auth layer.
    std::chrono::system_clock::time_point sessionStart_{
        std::chrono::system_clock::now()};

    SignOutAction           signOutAction_;
    sigc::connection        sessionConn_;   // Session::signalChanged
};

}  // namespace app::view
