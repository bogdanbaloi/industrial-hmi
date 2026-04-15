#include "src/core/i18n.h"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <string>
#include <libintl.h>

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
}

}  // namespace app::core
