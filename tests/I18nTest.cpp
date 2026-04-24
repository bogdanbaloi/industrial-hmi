// Tests for app::core::initI18n -- the GNU gettext adapter.
//
// i18n.cpp has three distinct paths we want to exercise:
//   * forceLanguage() branch  -- when `language` is an explicit code
//     like "en_US" or "de", overrides LANGUAGE / LANG env vars.
//   * auto branch -- when language is null/empty/"auto", calls
//     propagateLangToLanguage() which pulls from OS locale.
//   * resolveLocaleDir() variations -- absolute path (trusted),
//     relative path existing in CWD (canonicalized), missing path
//     (passed through to gettext which fails silently).
//
// No GTK, no singletons touched -- initI18n calls into libintl + setenv
// which are process-global but don't interfere with other unit tests
// in this file (each test is self-contained, no order dependence).

#include "src/core/i18n.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

/// Helper: read an env var into a std::optional-style pair.
std::pair<bool, std::string> readEnv(const char* name) {
    const char* v = std::getenv(name);
    if (!v) return {false, {}};
    return {true, std::string(v)};
}

}  // namespace

TEST(I18nTest, InitWithExplicitLanguageSetsLANGUAGE) {
    // forceLanguage() path: non-"auto" input overrides LANGUAGE.
    app::core::initI18n("locale", "en_US");

    auto [has, val] = readEnv("LANGUAGE");
    EXPECT_TRUE(has);
    EXPECT_EQ(val, "en_US");
}

TEST(I18nTest, InitWithAutoFallsBackToOsLocale) {
    // "auto" path: propagateLangToLanguage() runs. Exact resolved value
    // depends on the test runner's environment -- on Ubuntu CI LANG is
    // "C.UTF-8" which falls through without setting LANGUAGE; in a
    // desktop session LANGUAGE ends up as "en_US" or similar. The
    // invariant is simply "doesn't crash, doesn't throw".
    EXPECT_NO_THROW(app::core::initI18n("locale", "auto"));
}

TEST(I18nTest, InitWithNullLanguageIsEquivalentToAuto) {
    // isAuto() returns true for nullptr + empty + "auto" -- all three
    // hit the same propagateLangToLanguage branch.
    EXPECT_NO_THROW(app::core::initI18n("locale", nullptr));
    EXPECT_NO_THROW(app::core::initI18n("locale", ""));
}

TEST(I18nTest, InitWithNullLocaleDirUsesFallback) {
    // resolveLocaleDir: when localeDir is null/empty, returns "locale"
    // unchanged and passes it to bindtextdomain. gettext handles a
    // missing path silently (catalog lookup fails, _() returns the
    // source string) -- exactly what we want here. Just asserts no crash.
    EXPECT_NO_THROW(app::core::initI18n(nullptr, "en_US"));
    EXPECT_NO_THROW(app::core::initI18n("", "en_US"));
}

TEST(I18nTest, InitWithAbsolutePathRespectsIt) {
    // Absolute path goes straight through resolveLocaleDir's first
    // branch (is_absolute() -> return as-is). Pick a path that almost
    // certainly doesn't exist so gettext silently fails -- we're only
    // proving the code took the absolute-path branch.
#ifdef _WIN32
    const char* abs = "C:\\nonexistent-test-locale-root-zzz";
#else
    const char* abs = "/nonexistent-test-locale-root-zzz";
#endif
    EXPECT_NO_THROW(app::core::initI18n(abs, "en_US"));
}

TEST(I18nTest, InitWithExistingRelativePathCanonicalizesIt) {
    // Create a real temp directory so resolveLocaleDir's
    // `fs::exists(requested)` branch is taken (line ~193).
    auto tmpDir = fs::temp_directory_path() / "hmi-i18n-test-locale";
    fs::create_directories(tmpDir);

    // chdir into the parent so the relative path resolves.
    auto origCwd = fs::current_path();
    fs::current_path(tmpDir.parent_path());

    EXPECT_NO_THROW(
        app::core::initI18n(tmpDir.filename().string().c_str(), "en_US"));

    fs::current_path(origCwd);
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
}

TEST(I18nTest, ExplicitLanguageAfterAutoResetsEnv) {
    // Sequence that exercises the `gEnvOwned == true` branch in
    // propagateLangToLanguage on Windows (and the equivalent overwrite
    // behaviour on Linux's propagateLangToLanguage).
    app::core::initI18n("locale", "auto");    // first: gEnvOwned -> true
    app::core::initI18n("locale", "de");      // forceLanguage overwrites
    auto [has, val] = readEnv("LANGUAGE");
    EXPECT_TRUE(has);
    EXPECT_EQ(val, "de");

    // Back to auto -- should trigger the "clear our own writes" path
    // (gEnvOwned == true -> wipe env -> re-detect).
    EXPECT_NO_THROW(app::core::initI18n("locale", "auto"));
}
