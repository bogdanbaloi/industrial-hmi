# `src/presenter/` -- MVP Presenter Layer

Pure-C++ presenters mediating between the application model (signals
from `SimulatedModel`, `ProductsRepository`, `AuditLogger`,
`IntegrationManager`) and any view implementing the `ViewObserver`
interface. GTK-free by construction so the same presenters drive both
the GTK4 desktop front-end and the headless console binary, and ship
in unit tests without dragging a GUI toolkit in.

---

## Why this module exists separately

The classical mistake in desktop apps is letting Model and View talk
to each other directly. After a few releases the UI is full of
business logic ("if quality drops below 95% AND equipment is idle AND
operator role is Maintenance, show the calibrate button"), the model
is full of UI concerns ("after writing the value, queue a Glib idle
callback to refresh the gauge"), and nothing can be tested without a
real screen.

The Presenter layer is the discipline that prevents that drift:

- **Model** -- pure C++ state + signals (`SimulatedModel`,
  `ProductsRepository`, `Session`). Knows nothing about presenters
  or views.
- **Presenter** -- subscribes to model signals, transforms raw model
  state into `ViewModel` DTOs (one per UI concern), pushes them to
  every registered `ViewObserver`. Gates user-initiated actions
  through RBAC + audit before applying them to the model. **GTK-free.**
- **View** -- implements `ViewObserver`. Receives ViewModels +
  renders. Receives user gestures + forwards them to the presenter
  as method calls. Does NOT touch the model directly.

Result: every presenter unit-tested headlessly with a mock view;
swapping the GTK front-end for a console front-end is one new
`ViewObserver` implementation; adding a third front-end (Qt, web,
REST API) reuses every presenter unchanged.

---

## Architecture (SOLID at a glance)

```
   ┌──────────────────┐         ┌──────────────────┐
   │  Model (signals) │────────►│    Presenter     │◄────────┐
   │  SimulatedModel  │ change  │  (THIS LAYER)    │ method  │
   │  ProductsRepo    │ events  │                  │ calls   │
   │  Session         │         │                  │         │
   └──────────────────┘         └────────┬─────────┘         │
                                         │                   │
                                         │ ViewModel DTOs    │
                                         ▼                   │
                                 ┌──────────────────┐        │
                                 │  ViewObserver    │        │
                                 │  (View impls)    │────────┘
                                 │  GTK page /      │  user
                                 │  console / etc.  │  gesture
                                 └──────────────────┘
```

Six presenters today; each owns one product surface:

| Presenter | Drives | Notable concern |
|---|---|---|
| `DashboardPresenter` | Dashboard page (equipment cards, quality gauges, control panel, work unit) | Multi-axis ViewModels + RBAC role caching |
| `ProductsPresenter` | Products database page (CRUD over `ProductsRepository`) | Async writes via `Glib::signal_idle` to marshal back to GTK thread |
| `UsersPresenter` | User management page + ProfileDialog | RBAC façade; refuses self-delete; plaintext NEVER in audit details |
| `BackendHealthPresenter` | Sidebar I/O panel | Coloured dot per backend (TCP / MQTT / Modbus / OPC-UA) |
| `QualityInspectionPresenter` | Edge-AI inspection page | Pipes camera frames through ONNX classifier |
| `AlertCenter` | Alert overlay across pages | Coalesces duplicate alerts, debounces re-fire |

**SOLID applied:**

- **S**ingle responsibility -- each presenter owns one product
  surface. `DashboardPresenter` does not know about products;
  `UsersPresenter` does not know about quality.
- **O**pen/closed -- a new page = new presenter + new ViewModel +
  one registration. No existing presenter changes; no `if (page ==
  new_one)` switches anywhere.
- **L**iskov -- `BasePresenter` enforces the same observer
  contract for every concrete (notify the list, ignore null
  observers, mutex-guarded register/unregister). The view treats
  every presenter the same way through `ViewObserver`.
- **I**nterface segregation -- `ViewObserver` uses **empty default
  implementations** instead of pure virtual methods, so a view
  overrides only the callbacks it actually renders. The
  console view ignores Dashboard-specific methods without
  declaring "= 0" stubs.
- **D**ependency inversion -- every presenter takes its
  collaborators by reference / interface in the constructor
  (`UsersPresenter(UserRepository&, PasswordHasher&, Session&,
  AuditLogger&)`). Tests inject in-memory fakes; production wires
  the SQLite / Argon2 concretes.

---

## API surface -- class-by-class

### `ViewObserver`

```cpp
class ViewObserver {
public:
    virtual ~ViewObserver() = default;

    // One callback per ViewModel type. Default {} so a view
    // overrides only what it renders. Console front-end overrides
    // ~6 of these; GTK Dashboard overrides ~12; AuditLogPage
    // overrides 1.
    virtual void onWorkUnitChanged(const WorkUnitViewModel&) {}
    virtual void onEquipmentChanged(const EquipmentCardViewModel&) {}
    virtual void onQualityCheckpointChanged(const QualityCheckpointViewModel&) {}
    virtual void onControlPanelChanged(const ControlPanelViewModel&) {}
    virtual void onActuatorChanged(const ActuatorCardViewModel&) {}
    virtual void onBackendHealthChanged(const BackendHealthViewModel&) {}
    virtual void onProductsLoaded(const ProductsViewModel&) {}
    virtual void onViewProductReady(const ViewProductDialogViewModel&) {}
    virtual void onInspectionResult(const InspectionResultViewModel&) {}
    virtual void onAlertRaised(const AlertViewModel&) {}
    virtual void onError(const std::string&) {}
    // ... more
};
```

**Why empty defaults**: ISP. A console view doesn't render product-
detail dialogs and shouldn't have to write `void
onViewProductReady(...) override {}` 12 times.

### `BasePresenter`

```cpp
class BasePresenter {
public:
    virtual void initialize() = 0;

    void addObserver(ViewObserver* observer);
    void removeObserver(ViewObserver* observer);

protected:
    // Helpers used by every concrete to dispatch.
    void notifyAll(const std::function<void(ViewObserver*)>& fn);

    std::vector<ViewObserver*> observers_;
    mutable std::mutex         observersMutex_;
};
```

Observer list is mutex-guarded; notification iterates a snapshot
copy so a view that re-enters `addObserver` during a callback
doesn't invalidate the iterator. Same pattern as gtkmm's signal
internals.

### ViewModel DTOs (`src/presenter/modelview/`)

Plain aggregates. One per UI concern (EquipmentCard, WorkUnit,
QualityCheckpoint, ControlPanel, BackendHealth, ...). Built fresh
on every model event -- presenters never mutate a ViewModel in
place. Cheap copy + value-semantics make threading trivially safe.

### Action gating examples

```cpp
// DashboardPresenter -- user clicked Start
void DashboardPresenter::startProduction() {
    const auto current = session_.currentUser();
    if (!current || !app::auth::canStartStop(current->role)) {
        notifyError("Permission denied");
        audit_->record(makeFailureEvent("START", "forbidden"));
        return;
    }
    model_.startProduction();
    audit_->record(makeSuccessEvent("START", current->username));
}
```

Pattern repeats: pull current user → check `Role` helper → apply
or refuse → audit either way. Defence-in-depth (the sidebar
already hid the button if the role lacked permission, but the
presenter re-checks because RBAC must NOT depend on UI state).

---

## Embedding in another C++ project

Minimum dependencies: nothing beyond the model + auth interfaces
the presenter constructor takes. C++20 compiler. Specifically NOT
gtkmm.

### Wiring a presenter

```cpp
#include "presenter/DashboardPresenter.h"

// Composition root:
app::DashboardPresenter dashPresenter(simulatedModel);
dashPresenter.setAudit(*auditLogger, authSession);
dashPresenter.initialize();   // subscribes to model signals

// Hook up a view (any class implementing ViewObserver):
dashPresenter.addObserver(&myView);
```

### Driving from a non-GTK frontend

```cpp
class ConsoleView : public app::ViewObserver {
public:
    void onWorkUnitChanged(const WorkUnitViewModel& vm) override {
        std::cout << std::format("Work unit: {} ({}/{})\n",
                                  vm.id, vm.completedSteps, vm.totalSteps);
    }
    void onEquipmentChanged(const EquipmentCardViewModel& vm) override {
        std::cout << std::format("Equip {} -> {}\n", vm.id, vm.status);
    }
    // ... override only the few methods the console wants to render.
};

ConsoleView view;
dashPresenter.addObserver(&view);
```

### Adding a new presenter

1. Define ViewModel struct(s) in `src/presenter/modelview/`.
2. Add callback(s) to `ViewObserver` (empty default).
3. Subclass `BasePresenter`, take model + collaborators in
   constructor, override `initialize()` to subscribe to model
   signals.
4. In `initialize()` body, transform model state to ViewModel +
   `notifyAll(...)`.
5. Wire from composition root + a view.

Zero touches to existing presenters.

---

## Threading model

- **Presenter callbacks fire on the model's signal thread**, which
  is typically NOT the GTK main thread (model ticks run on a
  worker, ingest bridges on Boost.Asio threads).
- View implementations marshal to their own thread:
  - **GTK pages** use `Glib::signal_idle` to hop to the main
    thread before touching widgets.
  - **Console view** is single-threaded; the callback runs
    directly.
- **Observer registration is mutex-guarded**; the dispatch
  iterates a copy of the list, so a view subscribing during a
  callback is safe.
- **Async writes** (ProductsPresenter `addProduct`,
  `updateProduct`, `deleteProduct`) run on DatabaseManager's
  worker thread + callback back through `Glib::signal_idle` --
  the only place this layer touches GTK, and only because
  `DatabaseManager` enforces it.

---

## Testing

Each presenter ships with a focused test file in `tests/`:

- `DashboardPresenterTest.cpp` -- model signals → ViewModel
  dispatch matrix, role gating on start/stop/calibrate/reset.
- `ProductsPresenterTest.cpp` -- search + filter, sync read
  path with a fake `ProductsRepository`.
- `ProductsPresenterAsyncTest.cpp` -- add/update/delete callback
  ordering, error propagation through the Glib idle hop.
- `UsersPresenterTest.cpp` -- **40 cases**: 3 roles × 7 verbs
  RBAC matrix, validation rejects, self-mutation refusals,
  hashed-password round trips, **12 audit-format tests** pinning
  row content per verb (category, result, identifiers, plaintext
  absence).
- `BackendHealthPresenterTest.cpp` -- backend state → ViewModel
  colour mapping.
- `QualityInspectionPresenterTest.cpp` -- ONNX inference path
  with a fake classifier.

Pattern: real presenter + real auth core (Argon2 + SQLite in-
memory) + fake `ViewObserver` recording calls. Asserts on what the
presenter told the view, not on what the view rendered.

Run isolated:

```bash
cd build/debug
ctest -R 'Presenter' --output-on-failure
```

---

## Out of scope (intentional)

- **Async presenter for reads** -- everything except the
  Products write path is synchronous. Industrial HMI workloads
  are sub-ms; threading every read adds complexity for no
  measured win.
- **Reactive / state-store framework** (Redux-style) -- the
  ViewModel-per-event push pattern is enough for this surface
  size (6 presenters, ~15 ViewModels). A state store buys
  centralisation we don't need.
- **MVVM bindings** -- views read ViewModels directly via the
  callback parameter. A property-binding layer would couple
  presenters to a binding framework with no benefit at our scale.
