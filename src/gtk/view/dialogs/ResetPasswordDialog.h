#pragma once

#include <gtkmm.h>

#include <cstdint>
#include <string>

namespace app::view {

/// Modal dialog for the "admin resets a user's password" flow.
///
/// Distinct from `UserEditDialog` because:
///   * The verb is different ("reset" vs "edit profile") -- the audit
///     row uses a separate action code so an operator can see at a
///     glance whether their password was changed by themselves or by
///     an admin.
///   * The form is two fields (new + confirm) rather than five; a
///     reduced surface is less mistake-prone for an admin in a hurry.
///   * The "change your own password" flow (with the old-password
///     check) lives in `ProfileDialog` and shouldn't share state with
///     this admin-initiated reset.
///
/// Runs an inner Glib::MainLoop so the caller blocks until Submit /
/// Cancel, matching LoginDialog / UserEditDialog.
class ResetPasswordDialog : public Gtk::Window {
public:
    enum class Result : std::uint8_t {
        Submitted,
        Cancelled,
    };

    /// `username` is shown in the heading purely for human confirmation
    /// ("you are about to reset the password for X") -- the dialog
    /// itself doesn't act on it; the caller passes the user id to the
    /// presenter once Submit fires.
    explicit ResetPasswordDialog(std::string username);
    ~ResetPasswordDialog() override = default;

    ResetPasswordDialog(const ResetPasswordDialog&)            = delete;
    ResetPasswordDialog& operator=(const ResetPasswordDialog&) = delete;
    ResetPasswordDialog(ResetPasswordDialog&&)                 = delete;
    ResetPasswordDialog& operator=(ResetPasswordDialog&&)      = delete;

    Result runModal();

    /// The new password the operator confirmed. Empty in Cancelled
    /// state.
    [[nodiscard]] const std::string& newPassword() const {
        return newPassword_;
    }

private:
    void buildUi();
    void onSubmit();
    void onCancel();
    void showError(const std::string& message);

    std::string                    targetUsername_;
    std::string                    newPassword_;

    Gtk::Entry*                    newEntry_{nullptr};
    Gtk::Entry*                    confirmEntry_{nullptr};
    Gtk::Label*                    errorLabel_{nullptr};

    Glib::RefPtr<Glib::MainLoop>   innerLoop_;
    Result                         result_{Result::Cancelled};
};

}  // namespace app::view
