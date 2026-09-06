# 0021. Qt runtime palettes via an application-wide style sheet

## Status

Accepted (2026-09). Extends ADR-0008 (runtime palette and layout swap) to the Qt
frontend, and builds on ADR-0020 (the Qt frontend itself).

## Context

The GTK frontend swaps palettes at runtime by loading a CSS file (ThemeManager,
ADR-0008 / REQ-ARCH-006). The Qt frontend needs the same capability: an operator
picks a palette in Settings and the whole app re-themes without a restart while
the choice is remembered.

Two mechanisms exist in Qt. `QPalette` sets a handful of standard colour roles
but does not cover custom widget styling. Qt Style Sheets (QSS) are the CSS-like
analog of the GTK CSS palettes and can theme every widget class. Hardcoding
colours per widget (inline setStyleSheet) was the starting point but does not
swap at runtime and it scatters hex literals.

## Decision

Apply each palette as an application-wide QSS via `qApp->setStyleSheet`, built by
`QtPaletteManager`. A palette is DATA: a `PaletteDef` of semantic role colours
(bg, surface, text, muted, border, accent). One QSS template renders any palette
by token substitution, so a new palette is one struct rather than new code. The
manager persists the choice through `ConfigManager::setPalette` and restores it
on launch via `applyInitial`. Light and Dark are two of the palettes, so choosing
them is the light/dark mode. The picker lives in Settings, where the palette is a
persisted config value.

Per-widget SEMANTIC status colours (ok / warn / alarm) stay on the widgets, so
severity reads consistently across palettes rather than shifting with the chrome.

## Consequences

+ Runtime palette swap on Qt, matching the GTK capability, with one control in
  Settings and persistence through ConfigManager.
+ Adding a palette is a data change (one PaletteDef), not new code. That is the
  open-closed win.
+ QtPaletteManager depends only on ConfigManager (injected), so it reaches no
  singleton itself and stays testable.
- The global QSS themes the chrome only. Status colours are handled separately;
  tying them to the palette is a future step.
- A QSS `QWidget` rule touches every widget, so heavy per-widget overrides must
  stay deliberate. Accepted for a desktop app.
