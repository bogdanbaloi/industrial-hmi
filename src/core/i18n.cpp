#include "src/core/i18n.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <string>
#include <libintl.h>

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
// On Windows, setlocale(LC_ALL, "") does NOT read LANG. If the user set LANG
// but not LANGUAGE, propagate it so gettext can still resolve translations.
void propagateLangToLanguage() {
    const char* lang = std::getenv("LANG");
    const char* language = std::getenv("LANGUAGE");
    if (lang && *lang && (!language || !*language)) {
        // Strip encoding suffix: "it_IT.UTF-8" -> "it_IT"
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
