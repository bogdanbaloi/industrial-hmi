#include "src/auth/AvatarPlaceholder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>

namespace app::auth {

namespace {

/// Lower-cased ASCII letter test. Plain `std::isalpha` takes an `int`
/// in the [0, UCHAR_MAX] range -- passing a raw `char` is UB on
/// platforms with signed char (CERT-STR37-C). Wrapping in this helper
/// keeps the call sites readable and the cast in one place.
bool isAsciiLetter(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) != 0;
}

char toUpper(char c) {
    const auto u = static_cast<unsigned char>(c);
    return static_cast<char>(std::toupper(u));
}

char toLower(char c) {
    const auto u = static_cast<unsigned char>(c);
    return static_cast<char>(std::tolower(u));
}

/// Append the first ASCII letter from `word` (if any) to `out`,
/// uppercased. Skips leading non-letters ("u_42" -> 'U', not '_').
void appendFirstLetterUpper(std::string_view word, std::string& out) {
    for (char c : word) {
        if (isAsciiLetter(c)) {
            out.push_back(toUpper(c));
            return;
        }
    }
}

/// Build initials from a multi-word display name.
/// "Alice Bloomberg" -> "AB". Stops after two words.
std::string initialsFromWords(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    int taken = 0;
    constexpr int kMaxInitials = 2;
    while (i < text.size() && taken < kMaxInitials) {
        // Skip whitespace between words.
        while (i < text.size()
               && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        // Find word end.
        const std::size_t wordStart = i;
        while (i < text.size()
               && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
            ++i;
        }
        if (wordStart < i) {
            appendFirstLetterUpper(text.substr(wordStart, i - wordStart),
                                   out);
            ++taken;
        }
    }
    return out;
}

/// Pull up to two ASCII letters from a single token (usually the
/// username). "admin" -> "AD", "u_42" -> "U".
std::string initialsFromSingleToken(std::string_view token) {
    std::string out;
    constexpr std::size_t kMaxInitials = 2;
    for (char c : token) {
        if (isAsciiLetter(c)) {
            out.push_back(toUpper(c));
            if (out.size() >= kMaxInitials) break;
        }
    }
    return out;
}

bool containsWhitespace(std::string_view s) {
    return std::ranges::any_of(s, [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    });
}

}  // namespace

std::string computeInitials(std::string_view displayName,
                            std::string_view username) {
    // Prefer multi-word display name (the GitHub / Slack pattern users
    // expect: "Alice Bloomberg" -> "AB", not "AL").
    if (!displayName.empty() && containsWhitespace(displayName)) {
        std::string out = initialsFromWords(displayName);
        if (!out.empty()) return out;
    }
    // Single-word display name OR display name is just whitespace --
    // fall through to the username (or the display name if it has
    // letters but no space, e.g. "Alice" alone).
    if (!displayName.empty()) {
        std::string out = initialsFromSingleToken(displayName);
        if (!out.empty()) return out;
    }
    if (!username.empty()) {
        std::string out = initialsFromSingleToken(username);
        if (!out.empty()) return out;
    }
    // Nothing usable -- broken state, but the badge still has to draw
    // *something* so the operator notices the data issue.
    return std::string{"?"};
}

AvatarColor pickPaletteColor(std::string_view username) {
    // FNV-1a, 32-bit. Trivial, allocation-free, distributes well on
    // short ASCII inputs which is exactly the workload here. The
    // input is lower-cased so changing case doesn't reshuffle the
    // colour assignment.
    constexpr std::uint32_t kOffset = 0x811C9DC5U;
    constexpr std::uint32_t kPrime  = 0x01000193U;
    std::uint32_t h = kOffset;
    for (char c : username) {
        h ^= static_cast<std::uint8_t>(toLower(c));
        h *= kPrime;
    }
    return kAvatarPalette[h % kAvatarPalette.size()];
}

}  // namespace app::auth
