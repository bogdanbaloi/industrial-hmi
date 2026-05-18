// Tests for the avatar placeholder helpers -- the pure logic the
// badge widget consults when a user has no uploaded photo.
//
// Two surfaces:
//   * computeInitials -- string -> string transform, deterministic.
//   * pickPaletteColor -- string -> colour, stable across calls and
//     uniformly distributed across the palette buckets.
//
// No GTK -- the renderer that actually rasterises the tile lives in
// the view layer; these tests stay GTK-free so they run under any CI
// label (including the headless console build).

#include "src/auth/AvatarPlaceholder.h"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>
#include <string_view>

namespace {

using app::auth::AvatarColor;
using app::auth::computeInitials;
using app::auth::kAvatarPalette;
using app::auth::pickPaletteColor;

}  // namespace

// --- computeInitials ----------------------------------------------------

TEST(AvatarPlaceholderTest, TwoWordDisplayNameYieldsTwoInitials) {
    EXPECT_EQ(computeInitials("Alice Bloomberg", "alice"), "AB");
    EXPECT_EQ(computeInitials("Bob Smith",       "bob"),   "BS");
}

TEST(AvatarPlaceholderTest, OnlyFirstTwoWordsCount) {
    // A long name like "Jean-Luc de la Vega" should still yield two
    // characters -- the third word onwards is ignored. The hyphen
    // inside "Jean-Luc" is a non-space so the whole token counts as
    // the first word ("J").
    EXPECT_EQ(computeInitials("Jean-Luc de la Vega", "jlv"), "JD");
}

TEST(AvatarPlaceholderTest, SingleWordDisplayNameFallsBackToTokenLetters) {
    EXPECT_EQ(computeInitials("Alice", "alice"), "AL");
}

TEST(AvatarPlaceholderTest, EmptyDisplayNameUsesUsername) {
    EXPECT_EQ(computeInitials("", "admin"),        "AD");
    EXPECT_EQ(computeInitials("", "operator"),     "OP");
    EXPECT_EQ(computeInitials("", "maintenance"),  "MA");
}

TEST(AvatarPlaceholderTest, NonLetterPrefixSkipped) {
    // "u_42" still starts with an underscore-then-letter -- the helper
    // should skip non-letters when picking the first character.
    EXPECT_EQ(computeInitials("",  "u_42"),   "U");
    EXPECT_EQ(computeInitials("",  "_alice"), "AL");
    EXPECT_EQ(computeInitials("",  "123bob"), "BO");
}

TEST(AvatarPlaceholderTest, AlwaysUppercase) {
    EXPECT_EQ(computeInitials("", "alice"),         "AL");
    EXPECT_EQ(computeInitials("alice bloomberg",
                              "alice"),             "AB");
}

TEST(AvatarPlaceholderTest, WhitespaceOnlyDisplayNameFallsThrough) {
    EXPECT_EQ(computeInitials("   ", "alice"), "AL");
}

TEST(AvatarPlaceholderTest, EmptyEverythingYieldsQuestionMark) {
    // Defensive: should never happen if validation runs, but the badge
    // must draw something rather than collapse.
    EXPECT_EQ(computeInitials("", ""), "?");
}

TEST(AvatarPlaceholderTest, AsciiOnlyExtractionIgnoresLatinExtended) {
    // The username contract is ASCII-only; if a non-ASCII character
    // sneaks through, the helper picks the next ASCII letter rather
    // than breaking. The displayName can be richer text, but the same
    // rule applies for the initials extraction.
    EXPECT_EQ(computeInitials("", "\xC3\xA9 alice"), "AL");
}

// --- pickPaletteColor ---------------------------------------------------

TEST(AvatarPlaceholderTest, PaletteColorIsDeterministic) {
    // Same username MUST always map to the same colour -- otherwise
    // the badge would shift colour across sessions, eroding the
    // visual-recognition benefit.
    const auto a = pickPaletteColor("alice");
    const auto b = pickPaletteColor("alice");
    EXPECT_EQ(a, b);
}

TEST(AvatarPlaceholderTest, PaletteColorIsCaseInsensitive) {
    EXPECT_EQ(pickPaletteColor("Alice"), pickPaletteColor("alice"));
    EXPECT_EQ(pickPaletteColor("ADMIN"), pickPaletteColor("admin"));
}

TEST(AvatarPlaceholderTest, PaletteColorIsAlwaysFromPalette) {
    // Sanity: never returns a colour outside `kAvatarPalette`.
    const std::array<std::string_view, 5> samples = {
        "alice", "bob", "carol", "dave", "eve"};
    for (auto name : samples) {
        const auto c = pickPaletteColor(name);
        bool inPalette = false;
        for (const auto& p : kAvatarPalette) {
            if (p == c) { inPalette = true; break; }
        }
        EXPECT_TRUE(inPalette) << "color for " << name << " not in palette";
    }
}

TEST(AvatarPlaceholderTest, PaletteColorDistributesAcrossBuckets) {
    // Spot-check that the hash actually spreads across the palette --
    // a buggy FNV implementation that always returned 0 would still
    // pass the determinism + in-palette tests above.
    const std::array<std::string_view, 16> names = {
        "alice", "bob", "carol", "dave", "eve", "frank", "grace", "henry",
        "ivy",   "jane","kai",   "liam","mia", "noah",  "olive", "paul"};
    std::set<int> buckets;
    for (auto name : names) {
        const auto c = pickPaletteColor(name);
        for (std::size_t i = 0; i < kAvatarPalette.size(); ++i) {
            if (kAvatarPalette[i] == c) {
                buckets.insert(static_cast<int>(i));
                break;
            }
        }
    }
    // 16 names across 8 buckets should hit at least half the palette
    // even with a moderately uneven hash. (Empirically hits all 8 on
    // the current FNV-1a; the loose bound documents the invariant
    // without making the test fragile to future palette tweaks.)
    EXPECT_GE(buckets.size(), 4U)
        << "FNV distribution collapsed -- colour pool is mostly unused";
}
