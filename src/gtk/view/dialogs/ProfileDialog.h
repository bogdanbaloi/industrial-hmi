#pragma once

#include "src/presenter/UsersPresenter.h"

#include <gtkmm.h>

#include <cstdint>
#include <string>
#include <vector>

namespace app::view {

class AvatarWidget;

/// Self-service "my profile" dialog.
///
/// Two independent sections in one modal:
///   * Avatar -- live preview on the left, "Upload" / "Remove" buttons
///     on the right. Submit runs `setOwnAvatar` / `clearOwnAvatar`.
///   * Password -- old + new + confirm fields. Submit runs
///     `changeOwnPassword`.
///
/// Sections submit independently (separate buttons) so the operator
/// can change just their avatar without re-entering their password (a
/// painful gesture on a touchscreen). The dialog stays open after each
/// submit, surfacing inline success / failure feedback; the operator
/// closes it explicitly with the Done button.
///
/// @design Kept separate from UserEditDialog because the verbs differ
/// (self-service vs admin CRUD), the audit row uses a different
/// action code, and a single dialog with both surfaces would balloon
/// past what reads cleanly in one screen.
class ProfileDialog : public Gtk::Window {
public:
    explicit ProfileDialog(app::presenter::UsersPresenter& presenter);
    ~ProfileDialog() override = default;

    ProfileDialog(const ProfileDialog&)            = delete;
    ProfileDialog& operator=(const ProfileDialog&) = delete;
    ProfileDialog(ProfileDialog&&)                 = delete;
    ProfileDialog& operator=(ProfileDialog&&)      = delete;

    /// Show + spin the inner main loop until the operator clicks Done
    /// (or closes the window). The dialog has no submit/cancel verbs
    /// of its own at the top level -- each section commits its own
    /// changes.
    void runModal();

private:
    void buildUi();
    void loadCurrentUser();

    void onUploadClicked();
    void onRemoveAvatarClicked();
    void onChangePasswordClicked();
    void onDoneClicked();

    void showStatus(const std::string& message, bool ok);

    app::presenter::UsersPresenter& presenter_;

    AvatarWidget*                avatarPreview_{nullptr};
    Gtk::Label*                  identityLabel_{nullptr};

    Gtk::Entry*                  oldPasswordEntry_{nullptr};
    Gtk::Entry*                  newPasswordEntry_{nullptr};
    Gtk::Entry*                  confirmPasswordEntry_{nullptr};

    Gtk::Label*                  statusLabel_{nullptr};

    Glib::RefPtr<Glib::MainLoop> innerLoop_;
};

}  // namespace app::view
