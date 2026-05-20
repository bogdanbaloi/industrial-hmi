# `src/model/` -- Application Model Layer

The "M" in MVP. Owns the simulated production line state, the
SQLite-backed product master, the type vocabulary every other layer
speaks, and the Boost.Asio I/O context that backs async database
writes. GTK-free, presenter-free; depends only on `sqlite3` + Boost
headers + the project's logger interface.

---

## Why this module exists separately

A clean MVP architecture lives or dies by whether the Model is
**self-contained**. The Model has to:

- Hold the canonical state (equipment status, quality checkpoints,
  work unit progress, system mode).
- Emit change signals so presenters can react.
- Expose query methods that return value snapshots (callers don't
  hold references into protected state).
- Persist what needs to outlive process restarts (products,
  audit, history -- the latter two live in their own modules).

Everything that *isn't* state ownership belongs elsewhere:

- **No business policy** -- the presenter decides whether the
  Start button is enabled, not the model.
- **No UI** -- the model never calls `Glib::signal_idle` or
  imports a GTK header.
- **No protocol** -- if MQTT wants to publish a state change, an
  integration bridge subscribes to the model signal; the model
  doesn't know MQTT exists.
- **No threading marshalling** -- callbacks fire on the thread
  that triggered the change; subscribers (presenters) decide
  where to hop.

That discipline is what makes the same model drive a GTK desktop
front-end + a headless console binary + a future REST API
without a single change.

---

## Directory layout

```
src/model/
├── ProductionModel.h        Pure interface (signals + setters + queries)
├── SimulatedModel.h         Singleton concrete with demo data + simulator
├── ProductionTypes.h        Shared enums + value types (EquipmentStatus, WorkUnit, ...)
├── ProductsRepository.h     Pure interface for the products master
├── Product.h                Product DTO
├── DatabaseManager.{h,cpp}  SQLite-backed ProductsRepository + async write pool
└── ModelContext.h           Boost.Asio io_context owner shared by async writers
```

---

## Architecture (SOLID at a glance)

```
   ┌─────────────────────────┐
   │     ProductionModel     │  interface (signals + setters + queries)
   └────────────▲────────────┘
                │
   ┌────────────┴────────────┐
   │      SimulatedModel     │  singleton concrete; demo data + tick simulator
   └────────────┬────────────┘
                │ change signal
                ▼
        [Presenter subscribers]


   ┌─────────────────────────┐
   │   ProductsRepository    │  interface (read API for products)
   └────────────▲────────────┘
                │
   ┌────────────┴────────────┐
   │     DatabaseManager     │  singleton concrete; SQLite + async write pool
   └─────────────────────────┘
```

**SOLID applied:**

- **S** -- Production state, products persistence, type
  vocabulary, and the Asio context each in their own file. None
  knows about another.
- **O** -- Adding a new equipment field (e.g. ambient
  temperature) = new entry in `EquipmentStatus`, new setter on
  `SimulatedModel`, one new signal. Existing presenters / views
  / integration backends untouched.
- **L** -- Presenters depend on `ProductionModel&` /
  `ProductsRepository&`. Tests inject mocks (`MockProductionModel`,
  `MockProductsRepository`) without behavioural divergence.
- **I** -- `ProductionModel` is wide (~15 setters + signals
  covering every concern) but flat -- no inheritance hierarchy
  underneath. A subset interface (e.g. `EquipmentObserver` only)
  would be over-engineering for the surface size.
- **D** -- Composition root holds the singletons on the stack;
  presenters take them by reference; the integration bridges
  take them through narrower abstractions.

---

## API surface -- per file

### `ProductionTypes.h`

The shared vocabulary every other layer speaks. Plain aggregates +
enums:

- `enum class SystemState { Idle, Running, Stopped, Calibrating, Error }`
- `enum class EquipmentStatusKind { Online, Offline, Processing, Idle }`
- `enum class QualityCheckpointStatusKind { Passing, Warning, Failing }`
- `struct EquipmentStatus { id, kind, supplyLevel, ... }`
- `struct QualityCheckpoint { id, name, kind, passRate, ... }`
- `struct WorkUnit { id, productCode, completedSteps, totalSteps, ... }`
- `struct ActuatorStatus { id, kind, ... }`

Pure-value, copyable, no behaviour. Adding a field means
re-compiling the layers that read it; nothing else.

### `ProductionModel.h`

```cpp
class ProductionModel {
    // Subscriptions -- presenters call these in initialize().
    virtual void onEquipmentChanged(EquipmentCallback) = 0;
    virtual void onQualityCheckpointChanged(QualityCheckpointCallback) = 0;
    virtual void onWorkUnitChanged(WorkUnitCallback) = 0;
    virtual void onSystemStateChanged(StateCallback) = 0;
    // ...

    // Queries -- snapshot by value.
    virtual EquipmentStatus equipmentStatus(std::uint32_t id) const = 0;
    virtual std::vector<QualityCheckpoint> qualityCheckpoints() const = 0;
    virtual SystemState systemState() const = 0;
    // ...

    // Setters -- trigger signals.
    virtual void setEquipmentEnabled(std::uint32_t id, bool) = 0;
    virtual void setEquipmentSupplyLevel(std::uint32_t id, float) = 0;
    virtual void setQualityPassRate(std::uint32_t id, float) = 0;
    virtual void startProduction() = 0;
    virtual void stopProduction() = 0;
    // ...
};
```

**Subscription model**: append-only. No `removeSubscriber` --
model + presenters share the process lifetime; per-test cleanup
is "destroy the model singleton between test suites" (or use
`MockProductionModel`).

### `SimulatedModel.h`

Singleton (`instance()`). Holds maps `<id, EquipmentStatus>` etc.
behind a `std::mutex`; every setter writes under lock + emits the
matching signal **after** releasing the lock (so subscriber
callbacks don't re-enter the lock).

`tickSimulation()` is called externally by the composition root on
a timer; it nudges supply levels + quality rates with a fixed-seed
RNG so the demo dashboard has motion without being random across
runs. **No internal timer** -- the caller drives cadence so the
console front-end + scenario tests can step the model
deterministically.

`initializeDemoData()` seeds 3 equipment slots + 3 quality
checkpoints + an initial work unit. Called once at startup.

### `ProductsRepository.h` + `Product.h`

Read-only interface for the products master:

```cpp
class ProductsRepository {
    virtual std::vector<Product> all() = 0;
    virtual std::optional<Product> findById(int) = 0;
    virtual std::vector<Product> search(std::string_view) = 0;
};
```

`Product`: code, name, status, stock, qualityRate. Plain DTO.

Writes go through `DatabaseManager` directly (it implements
`ProductsRepository` plus the async write API).

### `DatabaseManager.{h,cpp}`

SQLite-backed singleton. Implements `ProductsRepository` for
synchronous reads and exposes an async write surface:

```cpp
void addProductAsync(productCode, name, status, stock, qualityRate,
                     std::function<void(bool)> callback);
void updateProductAsync(id, ..., std::function<void(bool)> callback);
void deleteProductAsync(id, std::function<void(bool)> callback);
```

The async write path is the **only place this layer touches Glib**:
the worker thread posts the callback to the GTK main thread via
`Glib::signal_idle` so the presenter's response handler runs on
the UI thread without the caller having to marshal.

### `ModelContext.h`

Owns the Boost.Asio `io_context` + a worker thread. Lifetime
matches the application. Shared by `DatabaseManager` and any
future async writer that wants to piggy-back on the same pool
(no need for each module to spawn its own).

---

## Embedding in another C++ project

Minimum dependencies: `sqlite3`, Boost.Asio (header-only), C++20
compiler.

### Bootstrap

```cpp
#include "model/SimulatedModel.h"
#include "model/DatabaseManager.h"

// Init demo data
app::model::SimulatedModel::instance().initializeDemoData();

// Open the products database (creates schema if missing)
app::model::DatabaseManager::instance().initialize("data/products.sqlite");

// Run a tick (typically from a Glib timer at the desired cadence)
app::model::SimulatedModel::instance().tickSimulation();
```

### Wiring a subscriber (presenter or integration bridge)

```cpp
auto& model = app::model::SimulatedModel::instance();
model.onEquipmentChanged([](const EquipmentStatus& e) {
    // ... transform to a ViewModel, publish to a backend, log, ...
});
```

### Swapping the model in tests

```cpp
class MockProductionModel : public app::model::ProductionModel {
    void setEquipmentEnabled(std::uint32_t id, bool on) override {
        lastEquipmentSet_ = { id, on };
        // ... emit signals if the test cares
    }
    // ... other overrides
};

MockProductionModel mock;
app::DashboardPresenter pres{mock};      // no singleton, no SQLite, no Asio
```

---

## Threading model

- **Model setters write under mutex, emit signals after
  releasing the lock**. Callbacks fire on the caller's thread.
- **Subscribers run wherever the setter was called from** --
  GTK main thread (operator click), worker thread (sim tick),
  Asio thread (ingest bridge). Subscribers marshal as needed.
- **DatabaseManager's async writes** run on the `ModelContext`
  worker thread; callbacks marshal to the GTK main thread via
  `Glib::signal_idle`. This is the only Glib touchpoint in the
  model layer.
- **Singleton instance** is constructed on first `instance()`
  call (Meyers' singleton, C++11 thread-safe local static).

---

## Testing

`tests/SimulatedModelTest.cpp` -- demo data initialisation,
setter -> signal round trips, tick determinism (fixed-seed RNG).

`tests/DatabaseManagerTest.cpp` -- schema bootstrap, read API,
async write callback delivery, search filter semantics.

The model layer is exercised transitively by **every presenter
test** (DashboardPresenter, ProductsPresenter, ...) -- those
construct the real model + observe its outputs.

Run isolated:

```bash
cd build/debug
ctest -R '(Model|DatabaseManager|SimulatedModel)' --output-on-failure
```

---

## Out of scope (intentional)

- **Persistent production state** -- the live model state (
  equipment levels, current work unit) is in-memory only. A
  reboot starts from demo data. A production deployment that
  needs continuity would add a snapshot writer + restore step at
  startup; the model interface stays the same.
- **Multi-line / multi-cell production** -- one set of
  equipment, one set of checkpoints, one work unit at a time.
  Multi-line would require an outer aggregator owning N
  `ProductionModel` instances; the interface doesn't change.
- **CRUD on the live production state** -- equipment slots are
  fixed at startup. Adding "add equipment at runtime" would
  require new model methods + matching signals.
- **Event sourcing / audit on the model itself** -- audit lives
  in `src/auth/` (operator-attributed actions). The model is the
  source of truth; replay-from-event-log is a different
  architecture.
