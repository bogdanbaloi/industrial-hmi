#include "src/core/i18n.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <libintl.h>

#ifdef _WIN32
// Windows only defines LANG/LC_MESSAGES env vars when the user sets them
// manually. Fall back to the user default locale from the OS if neither
// LANG nor LANGUAGE is in the environment.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

// GNU gettext exports this counter; every translation lookup checks it and
// re-reads the catalog when it changes. Bumping it after re-binding the
// domain is the standard way to force a live language switch — without it,
// the first `_()` call caches the old locale and later calls keep returning
// stale strings even after bindtextdomain/textdomain are invoked again.
// Available on glibc and on MSYS2's libintl-8 runtime.
// NOLINTNEXTLINE(readability-identifier-naming) — external C symbol.
extern "C" int _nl_msg_cat_cntr;

namespace app::core {

namespace {

#ifndef _WIN32
// "auto" on Linux: derive LANGUAGE from the system's LANG/LC_ALL so
// gettext honors the user's desktop language. We *overwrite* LANGUAGE
// rather than respecting an existing value, because an earlier run of
// this process may have pushed an explicit language into the
// environment (e.g. the user switched to Deutsch then back to Auto) —
// without this, "auto" would silently keep the previous explicit
// choice. LANG/LC_ALL are read-only from our perspective: they come
// from the shell or /etc/default/locale.
void propagateLangToLanguage() {
    const char* src = std::getenv("LC_ALL");
    if (!src || !*src) src = std::getenv("LC_MESSAGES");
    if (!src || !*src) src = std::getenv("LANG");
    if (!src || !*src) return;

    // Strip encoding + modifier: "pt_PT.UTF-8@euro" -> "pt_PT".
    std::string v = src;
    if (auto at = v.find('@'); at != std::string::npos) v.resize(at);
    if (auto dot = v.find('.');   dot != std::string::npos) v.resize(dot);
    if (v.empty() || v == "C" || v == "POSIX") return;

    setenv("LANGUAGE", v.c_str(), 1);
}
#endif

#ifdef _WIN32
// "auto" on Windows: always query the OS and overwrite LANGUAGE/LANG.
// We can't short-circuit on existing env vars because a previous run of
// this process may have set them to an *explicit* language — in which
// case "auto" would silently keep that language instead of picking up
// the system locale.
void propagateLangToLanguage() {
    // 1) Ask Windows for the user's Region setting. Returns tags like
    //    "en-US", "pt-PT"; libintl expects POSIX "pt_PT", so translate.
    wchar_t wbuf[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(wbuf, LOCALE_NAME_MAX_LENGTH) > 0) {
        char buf[LOCALE_NAME_MAX_LENGTH] = {};
        std::size_t len = 0;
        wcstombs_s(&len, buf, sizeof buf, wbuf, _TRUNCATE);
        std::string name(buf);
        for (auto& c : name) if (c == '-') c = '_';
        if (!name.empty()) {
            _putenv_s("LANGUAGE", name.c_str());
            _putenv_s("LANG", name.c_str());
            return;
        }
    }

    // 2) Fallback: if the OS call failed (extremely unlikely), fall
    //    through to whatever LANG was set by the shell before launch.
    const char* lang = std::getenv("LANG");
    if (lang && *lang) {
        std::string v = lang;
        auto dot = v.find('.');
        if (dot != std::string::npos) v.resize(dot);
        _putenv_s("LANGUAGE", v.c_str());
    }
}
#endif

// Override environment so gettext picks the requested language,
// regardless of OS locale.
void forceLanguage(const char* language) {
    if (!language || !*language) return;

#ifdef _WIN32
    // MSYS2 libintl is happy as long as LANGUAGE + LANG both name the
    // target; the C runtime tolerates arbitrary LANG values.
    _putenv_s("LANGUAGE", language);
    _putenv_s("LANG", language);
#else
    // On glibc, gettext ignores LANGUAGE whenever LC_MESSAGES resolves
    // to "C", "POSIX", *or* "C.UTF-8" — "C.UTF-8" specifically is
    // treated as a no-translation locale even though it's UTF-aware.
    // So we must NOT overwrite LANG with the target language (e.g.
    // "de"), because setlocale would fail on systems without the
    // de_DE locale generated, fall back to C, and kill translations.
    // Instead leave LANG as the shell provided it (typically
    // en_US.UTF-8 on stock Ubuntu) and only set LANGUAGE, which
    // glibc's gettext honors as long as LC_MESSAGES isn't C-family.
    setenv("LANGUAGE", language, 1);
#endif
}

bool isAuto(const char* language) {
    return !language || !*language || std::strcmp(language, "auto") == 0;
}

// bindtextdomain(..., "locale") resolves against the current working
// directory. That's fragile: launching `./build/debug/industrial-hmi.exe`
// from the repo root leaves CWD at the repo root, so gettext would look
// for `<repo-root>/locale/de/LC_MESSAGES/industrial-hmi.mo` which does
// not exist — the catalogs live in `<exe-dir>/locale/`. Resolve the
// directory ourselves so the binary works regardless of CWD.
std::string resolveLocaleDir(const char* localeDir) {
    namespace fs = std::filesystem;
    if (!localeDir || !*localeDir) return "locale";
    fs::path requested{localeDir};

    // Absolute path: trust the caller.
    if (requested.is_absolute()) return requested.string();

    // 1) CWD-relative (preserves existing behaviour for callers that
    //    already run from the correct working directory, e.g. CI).
    std::error_code ec;
    if (fs::exists(requested, ec)) {
        return fs::absolute(requested, ec).string();
    }

    // 2) Executable-relative fallback — the catalogs ship next to
    //    the binary when built via CMake (ADD_GETTEXT_CATALOGS copies
    //    them into build/<cfg>/locale/).
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char mbuf[MAX_PATH * 2] = {};
        std::size_t converted = 0;
        if (wcstombs_s(&converted, mbuf, sizeof mbuf, wbuf, _TRUNCATE) == 0) {
            fs::path candidate = fs::path{mbuf}.parent_path() / requested;
            if (fs::exists(candidate, ec)) return candidate.string();
        }
    }
#else
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        fs::path candidate = exe.parent_path() / requested;
        if (fs::exists(candidate, ec)) return candidate.string();
    }
#endif

    // 3) Give up and pass through — gettext will fail silently and
    //    _() will return source strings, which at least keeps the UI
    //    usable in English.
    return requested.string();
}

}  // namespace

void initI18n(const char* localeDir, const char* language) {
    if (isAuto(language)) {
        // Both platforms: derive LANGUAGE from the OS-level locale so
        // the user sees their desktop language on first run.
        propagateLangToLanguage();
    } else {
        forceLanguage(language);
    }

    // Respect LC_ALL / LANG from environment for number/date formatting.
    std::setlocale(LC_ALL, "");

#ifndef _WIN32
    // glibc gettext silently ignores LANGUAGE whenever LC_MESSAGES
    // resolves to a C-family locale ("C", "POSIX", or even "C.UTF-8").
    // Some WSL / container / CI environments ship with LANG=C.UTF-8,
    // which would silently defeat live language switching. Force
    // LC_MESSAGES to a real locale (en_US.UTF-8 is always present on
    // stock Ubuntu) so LANGUAGE is honored. Fall through quietly if
    // even that isn't installed — translations are then best-effort.
    const char* curMsg = std::setlocale(LC_MESSAGES, nullptr);
    const bool msgIsC = !curMsg ||
                        std::strcmp(curMsg, "C") == 0 ||
                        std::strcmp(curMsg, "POSIX") == 0 ||
                        std::strcmp(curMsg, "C.UTF-8") == 0 ||
                        std::strcmp(curMsg, "C.utf8")   == 0;
    if (msgIsC) {
        (void)std::setlocale(LC_MESSAGES, "en_US.UTF-8");
    }
#endif

    // Resolve the locale directory so gettext works regardless of CWD.
    const std::string resolvedDir = resolveLocaleDir(localeDir);

    // Point gettext at our compiled catalogs
    bindtextdomain(GETTEXT_PACKAGE, resolvedDir.c_str());
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    // Invalidate libintl's per-domain cache so the next _() picks up the
    // new language. Harmless on first call (it just bumps from 0 to 1).
    ++_nl_msg_cat_cntr;
}

}  // namespace app::core
