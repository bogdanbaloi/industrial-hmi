#include "src/core/i18n.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
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
extern "C" int _nl_msg_cat_cntr;

namespace app::core {

namespace {

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
    _putenv_s("LANGUAGE", language);
    _putenv_s("LANG", language);
#else
    setenv("LANGUAGE", language, 1);
    setenv("LANG", language, 1);
#endif
}

bool isAuto(const char* language) {
    return !language || !*language || std::strcmp(language, "auto") == 0;
}

}  // namespace

void initI18n(const char* localeDir, const char* language) {
    if (isAuto(language)) {
#ifdef _WIN32
        propagateLangToLanguage();
#endif
    } else {
        forceLanguage(language);
    }

    // Respect LC_ALL / LANG from environment for number/date formatting.
    std::setlocale(LC_ALL, "");

    // Point gettext at our compiled catalogs
    bindtextdomain(GETTEXT_PACKAGE, localeDir);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    // Invalidate libintl's per-domain cache so the next _() picks up the
    // new language. Harmless on first call (it just bumps from 0 to 1).
    ++_nl_msg_cat_cntr;
}

}  // namespace app::core
