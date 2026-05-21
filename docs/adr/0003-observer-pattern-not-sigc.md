# 0003. ViewObserver instead of sigc::signal

## Status

Accepted (2025-10)

## Context

Presenters need to push state changes to one or more views. Inside
gtkmm, the idiomatic mechanism is `sigc::signal<T>` — concise,
type-safe, integrates with `sigc::trackable` for automatic
disconnect on widget destruction.

But using `sigc::signal` in the presenter header would drag a hard
dependency on libsigc++ into `objectsPresenter`, which transitively
pulls glibmm. That breaks the goal in ADR 0001: presenters compile
and unit-test without any GTK stack present.

## Decision

A plain abstract class `app::ViewObserver` with empty default
implementations for every notification:

```cpp
class ViewObserver {
public:
    virtual ~ViewObserver() = default;
    virtual void onWorkUnitChanged(const presenter::WorkUnitViewModel&) {}
    virtual void onEquipmentCardChanged(const presenter::EquipmentCardViewModel&) {}
    virtual void onBackendHealthChanged(const presenter::BackendHealthViewModel&) {}
    // ~25 more on*() callbacks, all defaulted to no-op
};
```

`BasePresenter` holds `std::vector<ViewObserver*> + std::mutex` and
dispatches synchronously on the caller's thread. Concrete views
(every GTK page; `ConsoleView`; `MainWindow` for backend-health
notifications) inherit and override only what they render.

## Alternatives

- **`sigc::signal` directly** — rejected for the dependency reason
  above. Also forces every test to construct `sigc::trackable`
  derivatives, which need a glibmm main loop to behave correctly.

- **`Boost.Signals2`** — rejected. Heavy dependency for the equivalent
  feature set; we'd own its threading model on top.

- **`std::function` callback per event** — rejected. Doesn't model
  "many observers, one event source" cleanly; every signal becomes
  `std::vector<std::function<...>>` reinventing the same wheel.

- **One mega-callback `onAnyChange(Snapshot)`** — rejected. Each view
  would have to diff snapshots to know what to repaint. Targeted
  callbacks keep the rendering code dumb.

## Consequences

+ `objectsPresenter` links nothing GTK-related. Tests compile under
  `tests/presenter/` against `objectsModel + objectsPresenter` only;
  `FakeViewObserver` is 20 lines and records call counts.
+ Empty default impls mean a new view (or a test fake) can ignore
  90% of the surface area without compile errors. Adding a new
  notification is non-breaking for existing observers.
+ Synchronous dispatch under the caller's thread keeps reasoning
  local — no implicit thread hop. Threading is handled at the
  integration backend layer (ADR 0005) before the presenter is
  notified, not buried in the observer mechanism.
- Manual `addObserver` / `removeObserver` discipline; no RAII guard
  helper currently. Wrong teardown order = dangling pointer. Mitigated
  by views holding strong refs to their presenter for their
  lifetime, but worth a follow-up `ObserverScope` RAII type.
- 25+ virtual methods on one interface looks like Interface
  Segregation Principle violation. Justified by the "empty default"
  pattern: each view only overrides what it renders, so the
  effective dependency is per-method, not per-class.
