#pragma once

#include <string_view>

namespace app::view::pwvalidation {

/// Outcome of validating a "new password + confirmation" pair, shared
/// by the admin reset dialog (ResetPasswordDialog) and the self-service
/// change-password flow (ProfileDialog). Extracted from the dialogs'
/// onSubmit handlers so the rule is one definition, unit-tested without
/// a GTK widget.
enum class PasswordError {
    None,      ///< Valid: non-empty and both fields match.
    Empty,     ///< New password is blank.
    Mismatch,  ///< New and confirmation differ.
};

/// Validate a new-password / confirmation pair. Empty is checked before
/// mismatch so a blank form reports the more actionable "required"
/// message rather than "do not match".
[[nodiscard]] inline PasswordError
validateNewPassword(std::string_view newPassword,
                    std::string_view confirmation) {
    if (newPassword.empty()) {
        return PasswordError::Empty;
    }
    if (newPassword != confirmation) {
        return PasswordError::Mismatch;
    }
    return PasswordError::None;
}

}  // namespace app::view::pwvalidation
