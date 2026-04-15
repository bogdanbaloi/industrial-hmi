#pragma once

// ============================================================================
// Internationalization (i18n) - gettext wrapper
// ============================================================================
//
// Provides translation macros for user-facing strings:
//   _("text")    - translate at runtime
//   N_("text")   - mark for extraction, translate later (static init, arrays)
//   C_(ctx, s)   - translate with disambiguation context
//   NC_(ctx, s)  - mark with context, translate later
//
// The GETTEXT_PACKAGE macro (defined by CMake) names the .mo file catalog.
// Locale directory and domain binding happen once in main() via initI18n().
// ============================================================================

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "industrial-hmi"
#endif

#include <glibmm/i18n.h>

namespace app::core {

/// Initialize locale, bind text domain, and select catalog.
/// Call once at application startup (before any UI construction).
///
/// @param localeDir Absolute path to the directory containing <lang>/LC_MESSAGES/
/// @param language  "auto" (respect OS/env), or one of the LINGUAS codes:
///                  "en", "de", "es", "es_MX", "fi", "fr", "ga", "it",
///                  "pt", "pt_BR", "sv". If not "auto", this overrides the
///                  environment by setting LANGUAGE explicitly.
void initI18n(const char* localeDir, const char* language = "auto");

}  // namespace app::core
