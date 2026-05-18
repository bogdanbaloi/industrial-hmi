#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace app::auth {

/// 24-bit RGB triple. Plain aggregate so callers can feed it straight
/// into Gdk / Cairo without an adapter -- the renderer in
/// `src/gtk/view/widgets/AvatarWidget` reads `.r/.g/.b` directly.
struct AvatarColor {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

[[nodiscard]] constexpr bool operator==(const AvatarColor& a,
                                        const AvatarColor& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/// Curated palette for generated avatars. Eight hues chosen for
/// adequate contrast against white text (the initials are rendered
/// white over this background) and distinguishable from each other at
/// a 32x32 pixel size where the badge appears in the sidebar.
///
/// Kept stable: changing the order would shift every user's color
/// after a rebuild, which is jarring even though there's no functional
/// regression. Append-only if we ever extend it.
inline constexpr std::array<AvatarColor, 8> kAvatarPalette = {{
    {0x1F, 0x6F, 0xEB},  // blue
    {0x2D, 0xA4, 0x4E},  // green
    {0xBF, 0x37, 0x89},  // magenta
    {0xCF, 0x22, 0x2E},  // red
    {0x9A, 0x6A, 0x00},  // gold
    {0x6E, 0x40, 0xC9},  // purple
    {0x0A, 0x88, 0x7E},  // teal
    {0xD9, 0x73, 0x4E},  // orange
}};

/// Pure-logic helpers for the fallback "no upload" avatar -- the small
/// coloured tile with the user's initials that the badge widget draws
/// whenever `User::avatarMime` is empty.
///
/// Split from the actual Gdk rendering on purpose:
///   * What to display (initials, palette colour) is portable + cheap
///     to unit test -- belongs next to the rest of the auth domain.
///   * How to rasterise it into a Gdk::Pixbuf is GTK-specific and
///     lives in `src/gtk/view/widgets/AvatarWidget` (Faza 5).
///
/// This split also means a future Qt / web front-end reuses the
/// initials + colour decisions without dragging GTK in.

/// Compute the 1-2 character label drawn over the generated avatar.
///
/// Rules (matched to the GitHub / Slack convention operators expect):
///   * Prefer `displayName`: take the first letter of each of the
///     first two whitespace-separated words ("Alice Bloomberg" -> "AB").
///   * Single-word display name OR display name empty: fall back to
///     the first two ASCII letters of `username` ("admin" -> "AD").
///   * Username also empty (shouldn't happen post-validation): return
///     "?" so the badge stays visibly broken rather than blank.
///
/// Always uppercase. Non-letter characters are skipped before picking
/// the letter so usernames like "u_42" yield "U" not "U_". Multi-byte
/// UTF-8 is treated as opaque -- we only look for ASCII letters, which
/// matches the username constraints (ASCII-only by AuthService policy).
[[nodiscard]] std::string computeInitials(std::string_view displayName,
                                          std::string_view username);

/// Pick the palette colour for a given username. Deterministic +
/// stable: the same username always maps to the same colour across
/// sessions (and across rebuilds, as long as `kAvatarPalette` stays
/// stable). Case-insensitive so renaming "Alice" to "alice" wouldn't
/// shift the colour even if such a rename were supported.
///
/// Uses a simple FNV-1a hash mod palette size. Cryptographic strength
/// is not a requirement -- a uniform distribution across 8 buckets is
/// what matters, and FNV-1a empirically gives that on short ASCII
/// inputs without dragging a hashing library in.
[[nodiscard]] AvatarColor pickPaletteColor(std::string_view username);

}  // namespace app::auth
