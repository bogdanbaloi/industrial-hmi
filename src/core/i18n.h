#pragma once

// Internationalization (i18n) — gettext wrapper (GTK-free)
//
// Provides translation macros for user-facing strings:
//   _("text")    - translate at runtime
//   N_("text")   - mark for extraction, translate later (static init, arrays)
//
// The GETTEXT_PACKAGE macro (defined by CMake) names the .mo file catalog.
// Locale directory and domain binding happen via ConfigManager::applyI18n(),
// which delegates the actual binding to initI18n() below.
//
// Depends only on libintl — no <glibmm/i18n.h>, no GTK. Usable from both
// the GTK front-end and the headless console.

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "industrial-hmi"
#endif

#include <libintl.h>

// Mirror glibmm's macros exactly — behaviour identical, without the
// glibmm transitive include.
#ifndef _
#  define _(String)  gettext(String)
#endif
#ifndef N_
#  define N_(String) (String)
#endif

namespace app::core {

/// Initialize locale, bind text domain, and select catalog.
/// Policy-free mechanism: accepts params, binds gettext, returns.
///
/// @param localeDir Path to the directory containing <lang>/LC_MESSAGES/.
///                  Relative paths are resolved against the executable's
///                  own location (see i18n.cpp), so the binary works
///                  regardless of the process's current directory.
/// @param language  "auto" (respect OS/env), or one of the LINGUAS codes:
///                  "en", "de", "es", "es_MX", "fi", "fr", "ga", "it",
///                  "pt", "pt_BR", "sv". If not "auto", this overrides the
///                  environment by setting LANGUAGE explicitly.
///
/// @note This is pure mechanism. Policy (what language to apply given the
///       current config state, what to do on failure) lives in
///       ConfigManager::applyI18n().
void initI18n(const char* localeDir, const char* language = "auto");

}  // namespace app::core
