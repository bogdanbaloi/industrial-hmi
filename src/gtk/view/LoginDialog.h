#pragma once

#include "src/auth/AuthService.h"
#include "src/auth/Session.h"

#include <gtkmm.h>

#include <string>

// Note: Session is part of the constructor signature for documentation
// of intent (the dialog needs an authenticated Session to follow), but
// the AuthService already holds the session reference and writes to it
// on successful login -- the dialog itself never touches the Session
// directly. The constructor is kept for API stability; the parameter
// is consumed but not stored.

namespace app::view {

/// Modal login dialog.
///
/// Runs an inner `Glib::MainLoop` so the call site (composition root)
/// can block until the operator either logs in or quits -- the GTK
/// idiom that matches a Win32 `DialogBox` semantics. On Success the
/// Session passed by reference is populated; on Cancel the session is
/// left empty and the caller treats it as "user declined to log in"
/// (i.e. exit the binary).
///
/// @design Why a custom dialog rather than Gtk::Dialog: gtkmm-4 dropped
/// the synchronous `dialog.run()` helper, so any dialog blocking on
/// operator input needs its own inner loop anyway. A bare `Gtk::Window`
/// keeps the surface small + lets the dialog disable parent
/// interactions via `set_modal(true)` without a parent window
/// existing yet (the main window has not been built at this point in
/// the lifecycle).
///
/// @thread_safety GTK main thread only. The dialog mutates widgets
/// directly from button signals.
class LoginDialog : public Gtk::Window {
public:
    /// Outcome of the dialog. Reported to the caller via `result()`
    /// after `runModal()` returns.
    enum class Result : std::uint8_t {
        /// Authentication succeeded. Session has the User set.
        Success,
        /// Operator clicked Cancel or closed the window via the X
        /// button. Treat as "no login" -- caller exits.
        Cancelled,
    };

    /// @param service Auth coordinator; writes to its own Session on
    ///                successful login.
    /// @param session Existing session reference. Currently unused
    ///                inside the dialog (AuthService owns it); kept on
    ///                the signature so a future "switch user" gesture
    ///                can show the previous user's name as default.
    LoginDialog(app::auth::AuthService& service,
                app::auth::Session& session);
    ~LoginDialog() override = default;

    LoginDialog(const LoginDialog&)            = delete;
    LoginDialog& operator=(const LoginDialog&) = delete;
    LoginDialog(LoginDialog&&)                 = delete;
    LoginDialog& operator=(LoginDialog&&)      = delete;

    /// Show the dialog and block on the inner loop until the operator
    /// resolves it. Returns the resolution so the caller can branch
    /// on Success / Cancelled without having to inspect `Session`.
    Result runModal();

private:
    void buildUi();
    void onLoginClicked();
    void onCancelClicked();
    void onEnterPressedInPassword();
    void showError(const std::string& message);

    app::auth::AuthService&  service_;

    Gtk::Entry*              usernameEntry_{nullptr};
    Gtk::Entry*              passwordEntry_{nullptr};
    Gtk::Label*              errorLabel_{nullptr};
    Gtk::Button*             loginButton_{nullptr};
    Gtk::Button*             cancelButton_{nullptr};

    Glib::RefPtr<Glib::MainLoop> innerLoop_;
    Result                       result_{Result::Cancelled};
};

}  // namespace app::view
