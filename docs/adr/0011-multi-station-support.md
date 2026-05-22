# 0011. Multi-station support (Master/Slave first instance)

## Status

Accepted (2026-05)

## Context

Industrial deployments often have two linked stations on separate
PLCs sharing a single operator terminal -- a common shape is a
calibration / fill station upstream that feeds a production station
downstream. The single-station HMI we ship today assumes one model
per process; extending naively would either duplicate the whole
codebase per station or force a single model with internal "station
groups" that doesn't match physical reality.

We needed a path to multi-station that:

- Does not break single-station deployments (the default has to keep
  working unchanged).
- Demonstrates that the MVP layering from ADR-0001 genuinely
  supports multiple Model instances cleanly.
- Establishes a primitive that extends to N-station and to
  cross-process (master and slave on different machines) without a
  fundamental redesign.
- Captures the trade-offs in this ADR so the v2 (cross-process)
  follow-up has a clear path.

## Decision

Ship Master/Slave as the **first instance** of multi-station
support, opt-in via `ui.multistation_enabled` in config.

### Model layer

Two `ProductionModel` instances:

- **Master** -- the existing `SimulatedModel` singleton. Ticks
  simulation, fires events on its own thread.
- **Slave** -- a new `MirrorModel` concrete class. Holds independent
  state, exposes the same `ProductionModel` setter surface every
  other ingest bridge writes into (Modbus / MQTT / OPC-UA), but
  does **not** run its own simulation. Slave state is whatever the
  bridge pushes it. Commands on the slave (Start / Stop / Reset)
  update local state and fire `onSystemStateChanged` so the
  operator sees something happen on click, but they don't trigger
  any background production -- the slave is a *receiver*, not a
  producer.

### Bridge layer

A new `MasterToSlaveBridge` implementing the existing
`IntegrationBackend` interface:

- Constructor takes `(master, slave)` references.
- `start()` subscribes to master's `onEquipmentStatusChanged` and
  forwards each event into slave's `setEquipmentSupplyLevel` --
  the same entry point Modbus / MQTT / OPC-UA ingest bridges use.
- `stop()` flips a `running_` flag so subsequent callbacks return
  early (ProductionModel has no remove-callback API by design --
  this is the correct, non-leaking shutdown).
- Registered with `IntegrationManager` like every other backend, so
  it appears in the sidebar `BackendHealthBar` alongside TCP /
  MQTT / Modbus / OPC-UA. Operator sees `Master->Slave: CONNECTED`
  with a forwarded-event count in the tooltip.

### View layer

A new `MultiStationDashboardPage` that hosts **two**
`DashboardPage` instances side by side via a `Gtk::Box`
(horizontal split, 50/50). Each `DashboardPage` is bound to its
own `DashboardPresenter`; the two presenters are independent
observers of their respective models. No code duplication between
the panes -- the existing `DashboardPage` is composed twice, no
internal extraction needed.

### Wiring

- `ConfigManager::isMultiStationEnabled()` getter (defaults false).
- `main.cpp` instantiates `MirrorModel` + `MasterToSlaveBridge` and
  registers the bridge with `IntegrationManager`. Passes the
  `MirrorModel*` to `Application::setSlaveProductionModel(...)`.
- `MainWindow::createAllPages()` branches: if a slave model was
  injected, build a second `DashboardPresenter` and register a
  `MultiStationDashboardPage` in the notebook (instead of the
  single `DashboardPage`). Sidebar (E-STOP / status badge / alerts
  / I/O) stays bound to the master presenter -- the operator's
  canonical surface.

## Alternatives

### A. One `ProductionModel` with internal "station groups"

**Rejected.** The real deployment has two physical PLCs with
independent state. Modeling them as one model with internal groups
would create a synchronization fiction. The presenter would need
to know about groups too, adding `groupId` parameters to every
command and breaking the "one presenter == one station" mental
model. Worse, it makes the obvious extension (3+ stations) require
an internal redesign rather than just another bridge.

### B. Two `SimulatedModel` instances (lift the singleton)

**Rejected for v1, considered for v2.** `SimulatedModel` is a
strict singleton (private constructor + static `instance()`) used
from many call sites. Lifting that constraint touches every
caller and risks subtle breakage in code paths that assume "the
one model". `MirrorModel` is the lower-risk path: introduce a
separate concrete for the slave role, keep `SimulatedModel` as
the master, both implement the same `ProductionModel` interface.

A v2 follow-up may un-singleton `SimulatedModel` so the slave can
*also* tick its own simulation -- useful for demos where the slave
is genuinely independent. The view + presenter layer don't change.

### C. Two presenters sharing the singleton model

**Rejected.** Two views rendering the same model state == two
identical panes. Doesn't demonstrate the architecture, doesn't
help the operator, and confuses the "what does master/slave mean"
question.

### D. Direct cross-pointer between models (no bridge)

**Rejected.** Slave holding a pointer to master couples the two
models at class level and breaks the test isolation property
(can't unit-test slave without instantiating master). The bridge
keeps both models pristine and pushes the coupling into one
testable component.

### E. Cross-process from day one (MQTT-backed bridge)

**Deferred to v2.** A real cross-process master/slave deployment
talks MQTT (or OPC-UA) between two HMI instances on different
machines. The interface is ready: replace `MasterToSlaveBridge`
(in-process) with an `MqttMasterSlaveBridge` (cross-process)
without touching the view or presenter layer. The v1 in-process
bridge establishes the pattern and lets the v2 swap be a backend
replacement, not an architectural change.

## Consequences

### Positive

- **Multi-station extends without touching presenter or model
  code.** A 3-station deployment is a second slave + a second
  bridge. A fleet view (N stations) is N slaves + N bridges + a
  list-rendering view.
- **Bridge appears in `BackendHealthBar`** -- operational
  visibility parity with TCP / MQTT / Modbus / OPC-UA. Operator
  sees the bridge's state and forwarded-event count without
  thinking it's any different.
- **Same observer + threading discipline** as the rest of the
  application; nothing new to reason about.
- **Single config flag** controls all of this. Existing
  single-station deployments are unaffected -- `git diff` for
  them shows zero behavioral change.
- **Cross-process v2 is a backend replacement, not a redesign.**
  Same view, same presenters, same models; different bridge.

### Negative / accepted costs

- **`MirrorModel` is "passive" by design.** Slave-side commands
  (Start / Stop / Reset / Calibration) update local state but
  don't trigger production -- the slave is a receiver. Honest
  for the in-process v1; v2 with un-singletoned `SimulatedModel`
  could allow active slave.
- **Two models in memory** = roughly 2x state footprint when
  multi-station is on. Acceptable on a manufacturing terminal;
  not optimized for a battery-powered edge device.
- **Sidebar stays bound to master only.** Status badge, E-STOP,
  and alerts reflect the master's state. Defensible call (master
  is canonical), but means slave-side alerts wouldn't surface
  through the alert center in v1. Captured as a v2 design
  question.
- **Console front-end doesn't render multi-station yet.** The
  GTK View has `MultiStationDashboardPage`; the console binary
  still renders a single model. Adding multi-station support to
  the console is straightforward (host two `ConsoleView` halves)
  but out of scope for v1.

## Roadmap (future work, separate PRs)

1. **Console multi-station support** -- render slave state in the
   console binary; add scenario tests for the bridge end-to-end
   through console.
2. **N-station grid layout** -- 3 or 4 stations side by side
   instead of 2. New layout `.ui` file + a `MultiStationGridPage`
   that takes a `std::vector<DashboardPresenter*>`.
3. **Fleet view** -- M sites, each with N stations, via central
   MQTT. New `FleetView` page that aggregates a vector of slave
   `MirrorModel` instances each fed by an MQTT-backed slave
   bridge.
4. **Cross-process bridge** -- `MqttMasterSlaveBridge` that
   replaces the in-process `MasterToSlaveBridge` for deployments
   where master and slave run on different machines. View +
   presenter unchanged.
5. **Un-singleton `SimulatedModel`** -- allow slave to also run
   its own simulation. Optional; needed for demo cases where the
   slave is genuinely independent.
6. **Slave-side alert plumbing** -- decide if alerts from the
   slave should surface in the same sidebar alert center (with
   prefix), or in a separate panel. UX call, defer until an
   operator uses v1 and tells us.
