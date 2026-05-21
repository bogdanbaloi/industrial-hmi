# 0008. Runtime palette + layout swap

## Status

Accepted (2026-04)

## Context

Manufacturing-floor terminals deploy to many environments:
ambient-lit control rooms (operator prefers dark), high-glare
shop-floor stations next to large windows (operator prefers
light), and customer demos where a recognizable palette
(Industrial, Nord, Dracula, Blueprint...) sets the tone.

Restarting the application to change a color scheme is fine for a
developer; it's a serious workflow break for an operator mid-shift.
Likewise, some deployments want the sidebar on the right (mirrored
ergonomics) or replaced with a top-bar entirely (Blueprint layout
where alerts + logs live in popovers rather than a fixed sidebar).

## Decision

Two orthogonal runtime swap dimensions:

- **8 color palettes** — Industrial (dark+light), Nord (dark+light),
  Right (dark+light), Dracula (dark only), Retro CRT (dark only),
  Paper (light only), Blueprint (dark only), Cockpit (dark only).
  Each is a separate `assets/styles/themes/*.css` loaded at
  startup. The Settings page palette picker writes
  `ui.palette` to config and reloads the stylesheet immediately;
  no restart.

- **3 main-window layouts** — default (left sidebar), Right
  (mirrored), Blueprint (top-bar). The picker writes
  `ui.layout` (encoded in the palette choice) and `MainWindow`
  parses a different `assets/ui/main-window-*.ui` file, re-mounting
  the page widgets into the new tree without dropping presenter
  state.

Live language switching (11 locales via gettext) follows the same
pattern — rebuild the page tree so every `_()` and every
`translatable="yes"` re-resolves.

## Alternatives

- **One palette, ship customer-specific binaries** — rejected. We'd
  maintain N CI matrices and ship N artifacts for the same code.

- **GTK Settings -> system theme** — rejected. Couples the
  application to the OS theme; operator's KDE preference shouldn't
  decide the HMI's contrast.

- **Restart-required palette change** — rejected. The HMI lifetime
  is long (one boot per shift on the terminal). A restart loses
  active work-unit context.

## Consequences

+ Customer demos run "Industrial -> Nord -> Blueprint -> back" in
  one session without restart. The architecture is visible to a
  visiting reviewer, not hidden.
+ Each palette is a small CSS file with only the overrides it
  needs; the base `adwaita-theme.css` is the single source of
  default colors and metrics. Adding a new palette is one CSS file
  plus one entry in the picker.
+ Live language reload exercises the same page-rebuild seam, so
  the codepath is well-tested.
- Adding a new sidebar element (e.g. the user card in v1.2.0)
  requires updating every palette's overrides for the new CSS
  classes. Without that, the new element falls back to the base
  defaults and may look broken in a custom palette. The
  v1.2.0 sidebar redesign hit exactly this on Nord + Right; the
  follow-up "palette audit for sidebar design system" tracks the
  remaining palettes.
- The page-rebuild approach means observer subscriptions are
  re-registered on every palette swap. Cheap (in-memory pointer
  list) but a hot path that has to stay correct under teardown
  order constraints.
