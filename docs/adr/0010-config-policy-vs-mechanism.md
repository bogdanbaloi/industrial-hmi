# 0010. ConfigManager: policy / mechanism split

## Status

Accepted (2026-01)

## Context

The original `ConfigManager` was a header-only singleton: load a
JSON file, expose `getValue<T>(key)`. Convenient, but it grew a
problem: every consumer that needed config also wanted opinionated
"what does this config mean" logic.

i18n was the canonical example:
- `core::initI18n(localeDir, language)` is a pure binding to
  `bindtextdomain` + `setlocale`. No policy.
- But "what language should we pass?" requires a decision: config
  value if set, else env override, else OS locale, else fall back
  to English with a warning logged. That's policy.

Letting `core::initI18n` make policy decisions pollutes the
mechanism layer with logging dependencies and dependency on
`ConfigManager` (cycle). Letting every caller derive the language
duplicates the policy. The classic policy / mechanism boundary
problem.

## Decision

Split:

- **`src/core/i18n.{h,cpp}`** — pure mechanism. Takes
  `(localeDir, language)`, calls `bindtextdomain` +
  `bind_textdomain_codeset` + `setlocale`. No logging, no config
  dependency, no policy. Testable in isolation; lives in
  `objectsCore`.

- **`src/config/ConfigManager.{h,cpp}`** — policy owner. Exposes
  `applyI18n()`, `getDefaultTheme()`, future `applyTheme()`,
  `applyPalette()`. The "what should we do" knowledge lives here;
  it depends on `core::initI18n` and on the logger but not the
  other way around. Lives in `objectsConfig`, an `OBJECT` library
  that links `objectsCore + objectsConfigDefaults`.

- **`src/config/config_defaults.h`** — pure constants, header-only
  `INTERFACE` library `objectsConfigDefaults`. The "what's the
  fallback if config is silent" values.

Dependency direction: `defaults <- core <- config <- presenter +
view`. No cycles, each library testable on its own.

## Alternatives

- **Keep ConfigManager header-only** — rejected. Policy methods
  with non-trivial logic blow up compile times when included from
  every translation unit, and force every consumer to pull
  `<libintl.h>` transitively.

- **One giant `objectsCore` containing config + i18n + logger** —
  rejected. Cyclical dependency: config wants to log, logger wants
  config for its level, i18n wants the language from config. The
  staged Bootstrap (ADR 0004) breaks this cycle in time; the
  library split breaks it in space.

- **Singleton everywhere** — partially kept. `ConfigManager` is
  still accessed as a singleton from leaf code paths where
  threading a reference through 12 layers would obscure intent.
  But Bootstrap injects the same singleton explicitly into
  `Application::adoptBootstrap()` so tests can substitute a
  fresh instance.

## Consequences

+ `core::initI18n` is testable with a single fake `bindtextdomain`
  call and no GTK / no config. `tests/i18n/i18n_adapter_test.cpp`
  exercises it in 7 cases under any CI runner.
+ Adding a new policy method (e.g. `applyAutoRefresh(intervalMs)`)
  is one edit in `ConfigManager.cpp`; mechanism layers are
  untouched.
+ The dependency graph is acyclic and small enough to draw on a
  napkin — useful for the "introduce me to the codebase" path.
- The singleton on `ConfigManager` is still a singleton. Tests
  that need a clean state must reset it explicitly. The
  alternative (full DI of `ConfigManager&` everywhere) was judged
  not worth the ergonomic cost at this codebase size.
- Library count grew (Core, Config, ConfigDefaults, AppGtk,
  Console, Presenter, View, Model, Auth, Integration, Historian,
  Ml — 12 object libraries). Each is justified by an explicit
  dependency boundary, but the CMake topology is non-trivial; the
  per-module READMEs under `src/*/README.md` document each.
