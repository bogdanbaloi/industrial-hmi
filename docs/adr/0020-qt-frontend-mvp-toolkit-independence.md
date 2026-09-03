# 0020. Qt6 desktop frontend proves the MVP boundary is toolkit-independent

## Status

Accepted (2026-09). Extends ADR-0002 (two frontends, one main) to a third
frontend, and empirically validates the boundary claims of ADR-0001 (MVP layer
boundaries) and ADR-0003 (Observer via ViewObserver, not sigc).

## Context

ADR-0002 proved the View seam by running a second, HEADLESS frontend (the
console binary) over the same presenter and model layer. That showed the
presenter does not depend on GTK, but both frontends still shared one property:
neither is a second GUI toolkit.

A stronger proof is a third frontend on a DIFFERENT GUI toolkit. Qt6 has its own
event loop, its own widget hierarchy and its own thread-affinity rules, so
making the existing `DashboardPresenter` render through Qt without changing a
line of presenter or model code demonstrates the boundary far more convincingly
than a terminal view does.

There is also a product reason. This Qt shell is earmarked as the base of a
future supply-chain application. Building it now, against the existing factory
presenters, lets us prove the seam holds before the domain shifts. When the
views later swap factory metrics for supply-chain metrics, the change lands in
the View plus new view-models, not in a rewrite, because the presenter contract
is already toolkit-agnostic.

## Decision

Add an opt-in executable `industrial-hmi-qt`, gated by `BUILD_QT_FRONTEND` (default OFF),
built as a sibling of `industrial-hmi-console`: it links the shared presenter,
model and integration object libraries and adds only a new View layer under
`src/qt` plus its own composition root.

- Qt6 comes from the system toolchain via `find_package(Qt6 COMPONENTS Widgets)`,
  not from Conan. MSYS2 CLANG64 already ships Qt6 packages, and a Conan-vendored
  Qt would be a heavy, slow bootstrap for an opt-in target.
- `QtInitRoot` is the composition root, a direct structural mirror of
  `InitConsole`: same `SimulatedModel` singleton, same DI-constructed
  `DashboardPresenter`, same background tick thread.
- `QtDashboardWindow` multiply-inherits `QMainWindow` and `app::ViewObserver`.
  It holds no logic: it renders the view-models it receives and forwards button
  clicks to the presenter's existing action methods.
- Cross-thread hand-off: ViewObserver callbacks arrive on the tick thread and
  are re-marshalled onto the Qt UI thread with
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, the Qt-native analogue
  of the GTK side's `Glib::signal_idle`.
- The frontend is staged in vertical slices (Slice 1: control panel, status
  zone, work unit; later slices: equipment and actuator cards, quality and
  header status) so each step builds and runs green.

### Alternatives rejected

- QML / Qt Quick. Adds a declarative binding layer before the imperative Widgets
  proof is even shipped. Widgets first, QML only if a later need appears.
- Conan-vendored Qt6. Heavy build, no reliable prebuilt on MSYS2 CLANG64, and it
  would entangle the repo's existing dependency setup for one opt-in target.
- A third `#ifdef` branch inside `src/main.cpp`. Qt's event loop and link set
  are too divergent from the `Bootstrap` plus `Application` pair that `main.cpp`
  already serializes. A dedicated `src/qt/main.cpp` plus `QtInitRoot` keeps
  `main.cpp` from acquiring a third toolkit-specific branch, exactly as the
  console frontend uses `InitConsole` rather than bloating `main.cpp`.

## Consequences

+ The presenter and model layers are reused unmodified. The only new code is the
  View plus a composition root, which is the concrete proof ADR-0001 and
  ADR-0003 predicted but never demonstrated on a second GUI toolkit.
+ The seam is now validated across three view technologies (GTK, terminal, Qt),
  so the future supply-chain pivot changes views, not the core.
+ `BUILD_QT_FRONTEND` defaults OFF, so the GTK, console, CI and WSL builds are
  untouched and Qt6 need not be installed unless the frontend is built.
- Qt6 is a new system dependency for anyone who opts in, and the target needs a
  Windows MSYS2 build-verify since the WSL image has no Qt6.
- `AUTOMOC` is enabled, scoped to the Qt object library only, so no other target
  runs moc.
