# 0001. MVP layer boundaries

## Status

Accepted (2025-09)

## Context

This is an industrial HMI: equipment monitoring, work-unit lifecycle,
quality checkpoints, product database CRUD. The same business logic
needs to:

- Drive a GTK4 desktop UI on the manufacturing floor.
- Be unit-tested headless in CI, without a display server.
- Stay open to future view consumers (eventually a headless console
  binary; potentially a TUI, a web client, a remote dashboard).

A layered separation is required so the business logic doesn't depend
on any rendering toolkit, and so tests can substitute fakes at clean
boundaries.

## Decision

Model-View-Presenter (MVP), strictly enforced:

- **Model** (`src/model/`) — `ProductionModel`, `SimulatedModel`. Owns
  state, applies state transitions, emits callbacks for "value
  changed". Zero GTK dependency. Driven by an external tick caller.

- **Presenter** (`src/presenter/`) — `DashboardPresenter`,
  `ProductsPresenter`, `BackendHealthPresenter`,
  `QualityInspectionPresenter`, `UsersPresenter`. Translates model
  state into UI-friendly `*ViewModel` DTOs. Holds a thread-safe list
  of `ViewObserver*`. Zero GTK dependency.

- **View** (`src/gtk/view/`) — page widgets implementing
  `ViewObserver`. Renders ViewModels. Wires user actions back to the
  presenter. No business logic, no model access.

A presenter never knows which view layer it talks to. A view never
calls the model directly. ViewModels are plain DTOs — no
serialization tax, no domain methods.

## Alternatives

- **MVC** (controller mediates) — rejected. The "controller" reduces
  to a thin pass-through in a GUI app; MVP with the presenter
  owning the view contract is closer to how testing wants to work.

- **MVVM with data binding** — rejected. GTK4 / gtkmm has no
  first-class observable binding. Faking it would require building
  our own framework on top of `sigc::signal`, gaining nothing over
  an explicit observer interface.

- **Direct model -> view callbacks** — rejected. Couples the model to
  presentation concerns (e.g. "format percentage for display") and
  makes tests load GTK to exercise model logic.

## Consequences

+ Model + presenter unit-tested with no GTK in the test binary.
  `tests/presenter/*` link `objectsModel + objectsPresenter` and run
  under any CI runner including Windows without Xvfb.
+ A second front-end (the console binary, see ADR 0002) was bolted on
  without rewriting any presenter — every presenter signature was
  already View-agnostic.
+ ViewModels make the contract between layers explicit; a renamed
  field in the model can't silently break a view.
- More boilerplate than a "let the widget call the model" shortcut
  would need. Adding a new visible field is a 3-touch change: model
  emits, presenter folds into ViewModel, view renders.
- Manual observer register/unregister discipline — no RAII guard
  helper currently. Easy to forget on a teardown path.
