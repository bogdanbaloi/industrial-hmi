#pragma once

#include "src/auth/AvatarPlaceholder.h"
#include "src/auth/User.h"
#include "src/auth/UserRepository.h"

#include <gtkmm.h>

#include <optional>
#include <string>
#include <vector>

namespace app::view {

/// Square avatar widget. Renders either:
///   * a decoded `Gdk::Pixbuf` when the user has an uploaded avatar
///     blob (PNG / JPEG), OR
///   * a coloured tile with the user's initials, computed via
///     `app::auth::computeInitials` + `pickPaletteColor`, when no
///     blob is present.
///
/// The widget owns no presenter / repository -- callers feed it the
/// User struct + (optionally) the avatar bytes via `setAvatar(...)`.
/// This keeps it free of policy + lets the same widget show up in the
/// sidebar badge, the UsersPage row, and the ProfileDialog preview
/// without coupling to any of them.
///
/// @threading GTK main thread only.
class AvatarWidget : public Gtk::DrawingArea {
public:
    /// @param sizePx Square edge length in pixels. Typical values:
    ///               32 for the sidebar badge, 24 for table rows, 64
    ///               for the profile-dialog preview.
    explicit AvatarWidget(int sizePx);
    ~AvatarWidget() override = default;

    AvatarWidget(const AvatarWidget&)            = delete;
    AvatarWidget& operator=(const AvatarWidget&) = delete;
    AvatarWidget(AvatarWidget&&)                 = delete;
    AvatarWidget& operator=(AvatarWidget&&)      = delete;

    /// Update both the identity (drives initials + colour) and the
    /// optional avatar payload (drives the pixbuf path). Either call
    /// triggers a redraw.
    ///
    /// `avatar` of nullopt means "no uploaded avatar -- render
    /// initials" -- the caller (typically a presenter wrapper)
    /// resolves this from `User::avatarMime` being empty.
    void setUser(const app::auth::User&                user,
                 const std::optional<app::auth::Avatar>& avatar);

    /// Reset to the empty-state (anonymous silhouette colour, "?"
    /// initials). Used when the session clears.
    void clear();

private:
    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr,
                int width, int height);

    /// Decode `bytes` into a Gdk::Pixbuf using gdkpixbuf's stream
    /// loader. Returns null on any decode failure -- the on_draw
    /// path then falls through to the initials path so a corrupted
    /// blob never leaves the badge blank.
    static Glib::RefPtr<Gdk::Pixbuf> decodeAvatar(
        const std::vector<std::uint8_t>& bytes);

    int                                size_;
    std::string                        initials_{"?"};
    app::auth::AvatarColor             color_{0x6B, 0x6B, 0x6B};  // neutral grey
    Glib::RefPtr<Gdk::Pixbuf>          pixbuf_;  // null when no upload
};

}  // namespace app::view
