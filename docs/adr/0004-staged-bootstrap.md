# 0004. Staged Bootstrap orchestrator

## Status

Accepted (2026-01)

## Context

Application startup has a chicken-and-egg between the logger and the
config:

- The logger needs a path + log level from config to be properly
  configured.
- The config loader needs *something* to log to when a file is
  missing, corrupt, or falls back to defaults — otherwise the
  operator has zero diagnostic on a degraded boot.

Add i18n (needs config to pick the language), the SQLite stores
(historian, auth, audit log — each can fail independently), and the
front-end branch (GTK vs console, see ADR 0002), and "startup" is
a sequence with real ordering constraints and degradation
decisions at every step.

If both binaries (GTK + console) re-derive the order ad-hoc, they
will silently drift.

## Decision

A single `app::core::Bootstrap` class in `src/core/Bootstrap.{h,cpp}`
that orchestrates a two-phase logger pattern:

1. **bootstrapLogger()** — a minimal stderr logger at INFO level,
   created before config is even attempted. Any subsequent step can
   log diagnostics through this.
2. **config.initialize()** — load JSON, fall back to defaults on
   missing / corrupt file, warn through the bootstrap logger.
3. **configureLogger()** — re-create the logger with the level + path
   the config dictates. If the configured path is unwritable, keep
   the stderr fallback rather than crashing.
4. **config.applyI18n()** — `ConfigManager` decides the language
   (config value, env override, OS locale) and calls
   `core::initI18n()` — the pure mechanism (ADR 0010).

After `Bootstrap::run()` returns, both `main.cpp` branches receive
the same configured logger + config + i18n state. The GTK or
console front-end takes over from there and does its own
front-end-specific wiring (auth, integration backends, etc).

## Alternatives

- **One giant `main.cpp`** — rejected. The wiring would have to be
  duplicated between the GTK and console branches; the next
  refactor would silently diverge.

- **Lazy-init singletons** — rejected. Singletons defer the failure
  mode to first-use, so a config-missing error surfaces in random
  places hours after startup instead of in `main()`.

- **Throw on any startup failure** — rejected. The project's posture
  is graceful degradation: missing config -> defaults; unwritable
  log file -> stderr; SQLite open failure -> historian/auth
  silently disabled with a logged warning. Only truly unrecoverable
  errors (config corrupt-and-no-defaults, OOM) escape as typed
  exceptions caught by the `main()` reporter.

## Consequences

+ Both binaries share an identical bootstrap. Adding a new stage
  (e.g. theme manager initialization) is one edit, not two.
+ Every degradation decision is in one file. A reviewer can read
  `Bootstrap::run()` and `Bootstrap::configureLogger()` and see the
  full failure mode catalogue without grep.
+ The two-phase logger means even the earliest config warnings
  appear in operator logs, not just stderr they may not be looking
  at.
- `Bootstrap` is mutable state that lives on the stack of `main()`
  and is then handed to `Application::adoptBootstrap()` / passed by
  reference to `InitConsole`. The lifetime is short and explicit,
  but it's an "object that owns the world for a moment" which can
  look unusual.
