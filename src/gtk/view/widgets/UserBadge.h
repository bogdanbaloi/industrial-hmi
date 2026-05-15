#pragma once

#include "src/auth/AuthService.h"
#include "src/auth/Session.h"

#include <gtkmm.h>

#include <functional>

namespace app::view {

/// Sidebar widget that surfaces the currently-logged-in user.
///
/// Two labels (username + role) stacked above a "Sign out" button.
/// Empty / hidden when no session is set -- the only path that
/// reaches the main window goes through LoginDialog, so this widget
/// is the operator's confirmation that auth is wired and their
/// session is alive.
///
/// @design Sign out is a callback (`signal_sign_out_clicked`) rather
/// than an inline call to `service.logout()` so the host (MainWindow)
/// can additionally close itself / quit the application -- the
/// presenter / service alone can't do that without coupling to GTK.
class UserBadge : public Gtk::Box {
public:
    using SignOutAction = std::function<void()>;

    UserBadge(app::auth::AuthService& service,
              app::auth::Session& session);
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

private:
    void buildUi();
    void onSignOutClicked();

    app::auth::AuthService& service_;
    app::auth::Session&     session_;

    Gtk::Label*             usernameLabel_{nullptr};
    Gtk::Label*             roleLabel_{nullptr};
    Gtk::Button*            signOutButton_{nullptr};

    SignOutAction           signOutAction_;
};

}  // namespace app::view
