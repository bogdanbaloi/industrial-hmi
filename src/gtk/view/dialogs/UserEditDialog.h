#pragma once

#include "src/auth/Role.h"
#include "src/auth/User.h"

#include <gtkmm.h>

#include <cstdint>
#include <optional>
#include <string>

namespace app::view {

/// Modal dialog for creating OR editing a user account.
///
/// One dialog drives both the "Add" + "Edit" flows because the forms
/// overlap heavily -- diverging them would mean two near-identical
/// .cpp files that drift apart over time. The mode flag swaps a few
/// affordances:
///
///   Add  -> username editable, password + confirm visible,
///           enabled checkbox hidden (always on for new users),
///           submit button labelled "Create"
///   Edit -> username read-only (renaming breaks the audit trail),
///           password fields hidden (admin uses Reset Password
///           dialog for that), enabled checkbox visible,
///           submit button labelled "Save"
///
/// Runs an inner Glib::MainLoop so the caller can `runModal()` like
/// LoginDialog -- gtkmm-4 dropped synchronous `dialog.run`.
class UserEditDialog : public Gtk::Window {
public:
    enum class Mode : std::uint8_t {
        Add,
        Edit,
    };

    /// Form snapshot the caller pulls after `runModal() == Submitted`.
    /// Plain aggregate -- the presenter does the work, this struct is
    /// just transport.
    struct FormData {
        std::string      username;       // lower-cased by the dialog
        std::string      displayName;
        std::string      password;       // Add mode only; empty in Edit
        app::auth::Role  role{app::auth::Role::Operator};
        bool             enabled{true};
    };

    enum class Result : std::uint8_t {
        Submitted,
        Cancelled,
    };

    /// Construct in Add mode with a clean form, or in Edit mode with
    /// the existing user prefilled. `existing` is consulted only when
    /// `mode == Edit`.
    UserEditDialog(Mode mode, std::optional<app::auth::User> existing);
    ~UserEditDialog() override = default;

    UserEditDialog(const UserEditDialog&)            = delete;
    UserEditDialog& operator=(const UserEditDialog&) = delete;
    UserEditDialog(UserEditDialog&&)                 = delete;
    UserEditDialog& operator=(UserEditDialog&&)      = delete;

    /// Show + spin the inner loop until Submit or Cancel.
    Result runModal();

    /// Snapshot of the form values. Only meaningful after a Submitted
    /// outcome; in Cancelled state the contents are whatever the
    /// operator last typed.
    [[nodiscard]] const FormData& formData() const { return form_; }

private:
    void buildUi();
    void onSubmit();
    void onCancel();
    void showError(const std::string& message);

    Mode                                  mode_;
    std::optional<app::auth::User>        existing_;
    FormData                              form_;

    Gtk::Entry*                           usernameEntry_{nullptr};
    Gtk::Entry*                           displayNameEntry_{nullptr};
    Gtk::Entry*                           passwordEntry_{nullptr};
    Gtk::Entry*                           passwordConfirmEntry_{nullptr};
    Gtk::ComboBoxText*                    roleCombo_{nullptr};
    Gtk::CheckButton*                     enabledCheck_{nullptr};
    Gtk::Label*                           errorLabel_{nullptr};

    Glib::RefPtr<Glib::MainLoop>          innerLoop_;
    Result                                result_{Result::Cancelled};
};

}  // namespace app::view
