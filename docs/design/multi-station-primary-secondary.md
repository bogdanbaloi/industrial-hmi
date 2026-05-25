# Multi-station support — Primary/Secondary layout

Implementation plan for the first multi-station HMI layout. The
shipped feature is **Primary → Secondary** (calibration station feeds
production station). The architecture is intentionally designed
to extend to N stations and to fleet view later, captured as
future work in ADR-0011.

Saved locally until we cut a feature branch. Will move into the
repo as `docs/design/multi-station-primary-secondary.md` once work
begins, so reviewers see the plan alongside the code.

---

## 1. Context + goals

**Industrial case.** A common manufacturing topology has two
linked stations: one calibrates / preps samples, the other runs
production using the calibration result. They run on separate
PLCs, talk to separate sensors, but a single operator at a single
HMI terminal supervises both.

**Goals.**

1. Ship a working Primary/Secondary dashboard the operator can switch
   to from the existing palette/layout picker.
2. Demonstrate that the MVP architecture genuinely supports
   multiple Model instances cleanly.
3. Establish the integration primitive (`PrimaryToSecondaryBridge`)
   that extends to N-station fleet topologies as future work.
4. Document the decision in ADR-0011 with the alternatives
   considered.

**Non-goals.**

- Implementing N-station grid view, fleet view, compare mode, or
  any other multi-station layouts in this PR (each is a follow-up).
- Adding new presenters or breaking existing single-station
  layout — current Industrial / Right / Blueprint layouts stay
  unchanged.
- Cross-process primary/secondary (over a real network). The bridge
  is in-process for v1; a future PR replaces the bridge with an
  MQTT bridge for cross-process deployment, no presenter or view
  changes needed.

---

## 2. Architecture decision

### Chosen: two ProductionModel instances + PrimaryToSecondaryBridge

```
       Primary                        Secondary
   ┌───────────┐                  ┌───────────┐
   │ Production│                  │ Production│
   │  Model A  │─── Bridge ──────▶│  Model B  │
   └─────┬─────┘                  └─────┬─────┘
         │                              │
         ▼                              ▼
   Dashboard                      Dashboard
   PresenterA                     PresenterB
         │                              │
         ▼                              ▼
   MultiStationDashboardPage (one view, two panes)
```

- **Two `ProductionModel` instances**, fully independent — same
  class, different state.
- **`PrimaryToSecondaryBridge`** subscribes to Primary's "calibration
  complete" event and calls a `setCalibration(...)` setter on
  Secondary. Implements the existing `IntegrationBackend` interface
  so it shows up in the sidebar's BackendHealthBar alongside
  TCP/MQTT/Modbus/OPC-UA — same operational visibility.
- **Two `DashboardPresenter` instances**, each wired to its own
  model. Same class, configured per instance.
- **One new page `MultiStationDashboardPage`** in `src/gtk/view/
  pages/`. Holds both presenters, renders two `DashboardPage`-
  shaped subtrees in a horizontal split (primary left, secondary
  right).
- **New layout `.ui` file** `main-window-multistation.ui` —
  same MainWindow shell, but `main_notebook` is replaced by the
  MultiStationDashboardPage directly so the operator doesn't
  switch tabs to compare.

### Alternatives considered

**B. One ProductionModel with "station groups"**. Rejected
because: the real deployment has two physical PLCs with
independent state; modeling them as one model with internal
groups would create a synchronization fiction. The presenter
would need to know about groups too, adding `groupId` parameters
to every command.

**C. Two ProductionModel + direct cross-pointer (no bridge)**.
Rejected because: couples the two models at class level (Secondary
holds a pointer to Primary), breaks the test isolation property
(can't unit-test Secondary without instantiating Primary). The bridge
keeps both models pristine and pushes the coupling into one
testable component.

**D. Cross-process from day one (MQTT bridge)**. Rejected for
now: real cross-process deployment is a future PR. The in-process
bridge in v1 has the same `IntegrationBackend` interface, so
swapping it for an MQTT-backed bridge is a backend implementation
change, not an architecture change. Established the pattern, kept
the v1 scope tight.

---

## 3. File-level plan

### New files

```
src/integration/
  PrimaryToSecondaryBridge.h
  PrimaryToSecondaryBridge.cpp

src/gtk/view/pages/
  MultiStationDashboardPage.h
  MultiStationDashboardPage.cpp

assets/ui/
  main-window-multistation.ui

docs/adr/
  0011-multi-station-support.md

docs/design/
  multi-station-primary-secondary.md    # this file, moved at branch cut
  multi-station-roadmap.png        # optional architecture diagram

tests/integration/
  primary_to_secondary_bridge_test.cpp

tests/scenarios/
  multistation_calibration.txt
  multistation_calibration.expected
```

### Modified files

```
src/main.cpp                         # register PrimaryToSecondaryBridge when enabled
src/gtk/view/MainWindow.cpp          # palette/layout picker entry + page mount
src/gtk/view/MainWindow.h            # add multistation handle if needed
src/config/config_defaults.h         # kMainWindowMultistationUI constant
src/config/ConfigManager.h           # getMultiStationEnabled() getter
CMakeLists.txt                       # add MultiStationDashboardPage to objectsView,
                                       PrimaryToSecondaryBridge to objectsIntegration
docs/adr/README.md                   # index entry for ADR-0011
CHANGELOG.md                         # Unreleased entry
README.md                            # Highlights bullet for multi-station support
config/app-config.json               # ui.layout new option documented
```

### Reused (unchanged)

- `ProductionModel` — used as-is, two instances
- `DashboardPresenter` — used as-is, two instances
- `IntegrationBackend` interface — `PrimaryToSecondaryBridge` implements it
- `BackendHealthPresenter` + `BackendHealthBar` — bridge auto-appears
- Existing single-station layouts (Industrial / Right / Blueprint)
- All existing tests

---

## 4. Phased implementation

Each phase has a clear exit criterion. You can stop at the end of
any phase and the code in `main` is shippable — no half-merged
states.

### Phase 1 — `PrimaryToSecondaryBridge` skeleton + tests (4-6 h)

**Goal.** The bridge exists as a tested unit, not yet wired into
the app.

**Tasks.**

1. Create `PrimaryToSecondaryBridge.{h,cpp}` implementing
   `IntegrationBackend`:
   - Ctor takes `ProductionModel& primary, ProductionModel& secondary`.
   - On `start()`: subscribes to Primary's "calibration complete"
     callback. State → `Connected`.
   - On Primary event: extracts calibration result, calls
     `secondary.setCalibration(value)`.
   - On `stop()`: unsubscribes. State → `Disconnected`.
   - `name()` returns `"Master→Secondary"`.
   - `metricsSummary()` returns last-bridged timestamp + count.

2. Write `tests/integration/primary_to_secondary_bridge_test.cpp`:
   - Bridge wires two model fakes correctly.
   - Primary event triggers secondary setter.
   - `stop()` properly unsubscribes (subsequent Primary events do
     NOT reach secondary).
   - Bridge state transitions correctly.
   - Bridge metrics report correctly.

3. Add to `CMakeLists.txt` under `objectsIntegration`. Add test to
   `tests/CMakeLists.txt`.

**Exit criterion.** `ctest -R master_to_secondary` passes 5+ cases.
Bridge is unused in `main.cpp` yet — no behavior change to the
running app.

**Verification.**
```bash
cmake --build build/debug
cd build/debug && ctest --output-on-failure -R master_to_secondary
```

---

### Phase 2 — `MultiStationDashboardPage` (6-8 h)

**Goal.** A new GTK page that hosts two DashboardPresenter
instances side-by-side. Page is not yet mountable from a layout
— integration in Phase 3.

**Tasks.**

1. Create `MultiStationDashboardPage.{h,cpp}` inheriting `Page`:
   - Ctor takes two `DashboardPresenter*` (primary, secondary) +
     services.
   - Builds a horizontal `Gtk::Box` with two child boxes (60/40
     or 50/50 split, decide on visual review).
   - Each child mounts the same subtree DashboardPage already
     builds (work-unit card, equipment row, quality row, control
     panel) but bound to its own presenter.
   - Each child has a small header label: "PRIMARY STATION (A)" /
     "SECONDARY STATION (B)" so the operator knows which is which.
   - Both panes refresh on their own presenter's observer
     callbacks (independent state).

2. Decide reuse strategy: either
   - Extract DashboardPage's widget-building code into a helper
     `buildDashboardWidgets(presenter, container)` and reuse
     (preferred — single source of truth, less duplicate code).
   - Or copy/paste the rendering code (faster but creates drift
     risk). Defer this decision to implementation; check what's
     cleaner after reading DashboardPage.cpp.

3. Add to `CMakeLists.txt`. No tests yet — view layer follows
   existing pattern of Xvfb smoke testing rather than unit
   testing widgets.

**Exit criterion.** Page compiles, renders cleanly when manually
instantiated in a scratch test harness. No mount path in production
binary yet.

**Verification.**
```bash
cmake --build build/debug
# manual scratch test if you wrote one; otherwise visible in phase 3
```

---

### Phase 3 — Wire it up: layout, config, picker (4-5 h)

**Goal.** The operator can pick "Multi-Station (Primary/Secondary)"
from Settings → Palette/Layout, and the page renders with both
stations live.

**Tasks.**

1. Create `assets/ui/main-window-multistation.ui`:
   - Same MainWindow shell as current layouts.
   - In place of the notebook, a placeholder Gtk::Box `id="multistation_mount"`
     where `MultiStationDashboardPage` is appended programmatically
     by MainWindow.
   - Sidebar stays the same (USER / STATUS / ALERTS / I/O sections);
     I/O section will auto-show the PrimaryToSecondaryBridge alongside
     other backends.

2. Add layout option:
   - `config_defaults.h`: `kMainWindowMultistationUI =
     "assets/ui/main-window-multistation.ui"`.
   - `MainWindow::chooseMainWindowUI(palette)`: add case
     `palette == "multistation"`.
   - Settings page palette picker: add new entry "Multi-Station
     (Primary/Secondary) — DARK + LIGHT".

3. `main.cpp` composition root:
   - When config says `ui.multistation_enabled = true` (new flag,
     defaults to false): instantiate **two**
     `SimulatedModel` instances + two `DashboardPresenter`
     instances + `PrimaryToSecondaryBridge` registered with
     `IntegrationManager`.
   - When false: existing single-model path.
   - Both branches must not break each other; if multi-station
     is enabled, the existing single-station layouts gracefully
     show only the primary (or are disabled — decide on UX).

4. `MainWindow::createAllPages()`: when current layout is
   multistation, mount `MultiStationDashboardPage` into the
   `multistation_mount`. When other layouts, current behavior.

5. Update palette overrides for the new sidebar containers if
   the structure changes (likely no change needed; sidebar is
   identical).

**Exit criterion.** Launch the GTK binary with
`ui.multistation_enabled=true`, select the multistation layout
in Settings. Two dashboards render side-by-side. Primary shows
calibration progressing; Secondary shows production using the
calibration result mirrored via the bridge. BackendHealthBar
shows "Master→Secondary: CONNECTED".

**Verification.**
```bash
./enable-auth.sh
# edit build/debug/config/app-config.json: set "ui.multistation_enabled": true
(cd build/debug && ./industrial-hmi.exe)
# Settings -> Palette -> Multi-Station -> observe
```

---

### Phase 4 — Console scenario + docs + ADR (3-4 h)

**Goal.** Multi-station path is tested in CI; ADR-0011 captures
the decision; CHANGELOG + README reflect the new feature.

**Tasks.**

1. Write `tests/scenarios/multistation_calibration.txt` +
   `.expected`:
   - Console binary starts with multistation enabled.
   - `start primary` → primary begins calibration.
   - `status primary` → shows calibrating.
   - Wait, then `status secondary` → shows new calibration value
     received.
   - `quit`.
   - Expected output captures the primary + secondary state lines.

2. Add the scenario to `tests/CMakeLists.txt` so ctest runs it.

3. Write `docs/adr/0011-multi-station-support.md`:
   - Status: Accepted
   - Context: industrial multi-station deployments
   - Decision: 2 models + bridge, see architecture diagram
   - Alternatives: 1 model with groups (rejected), direct cross-
     pointer (rejected), cross-process from day one (deferred)
   - Consequences: + extension to N-station + fleet, - bridge is
     in-process only for v1
   - Roadmap: lists multi-line grid, fleet view, compare mode,
     each as separate future PR.

4. Add index entry to `docs/adr/README.md`.

5. `CHANGELOG.md` Unreleased entry: *"Multi-station Primary/Secondary
   layout (opt-in via `ui.multistation_enabled`). New
   `PrimaryToSecondaryBridge` integration backend connects two
   `ProductionModel` instances in-process. See ADR-0011."*

6. `README.md` Highlights bullet (under existing palette/layout
   bullet): *"**Multi-station support** — Primary/Secondary dashboard
   for paired calibration/production stations; first instance of
   the multi-station architecture documented in ADR-0011."*

7. Take a screenshot of the running multi-station dashboard.
   Add to `docs/screenshots/multistation.png`. Embed in README's
   Highlights section if you've added the screenshots block
   from the GitHub polish checklist.

**Exit criterion.** `ctest -R multistation` passes. ADR-0011 is
in `docs/adr/`. CHANGELOG + README updated. Screenshot added.

**Verification.**
```bash
ctest --test-dir build/debug --output-on-failure -R multistation
# Open docs/adr/0011-multi-station-support.md in your editor
# Open README.md and verify the Highlights bullet
```

---

## 5. ADR-0011 draft (writes during Phase 4)

Outline only — fleshed out at write time with the actual decisions
locked in from Phase 1-3 experience:

```
# 0011. Multi-station support (Primary/Secondary first instance)

## Status
Accepted (2026-MM, PR #NNN)

## Context
Industrial deployments often have linked stations on separate PLCs
sharing a single operator terminal. The current HMI assumes one
station per binary. We need a path to multi-station that doesn't
break the single-station deployments and that extends cleanly to
N-station and fleet topologies.

## Decision
- Two independent `ProductionModel` instances, one per station.
- `PrimaryToSecondaryBridge` connects them, implementing the existing
  `IntegrationBackend` interface so it appears in the sidebar's
  BackendHealthBar with the same operational visibility as TCP/
  MQTT/Modbus/OPC-UA.
- Two `DashboardPresenter` instances, one per model.
- New `MultiStationDashboardPage` hosts both presenters in a
  split layout.
- New `main-window-multistation.ui` selected via the existing
  palette/layout picker.
- Opt-in via `ui.multistation_enabled` config flag; default off.

## Alternatives
- 1 model with station groups: rejected, models a fiction.
- Direct cross-pointer between models: rejected, breaks test
  isolation.
- Cross-process bridge (MQTT) from day one: deferred to future
  PR; interface is ready.

## Consequences
+ Multi-station extends without touching presenter or model code.
+ Bridge appears in BackendHealthBar — operational visibility
  parity with other backends.
+ Same observer + threading discipline as the rest of the
  application; nothing new to reason about.
- Two models in memory = roughly 2x state footprint for the
  multi-station mode. Acceptable on a manufacturing terminal.
- Bridge is currently in-process. Cross-process primary/secondary
  needs a future PR (MQTT-backed bridge); the interface is
  designed to support this without presenter changes.

## Roadmap (future work, separate PRs)
- N-station grid layout (3-4 stations side by side).
- Fleet view (M sites, each with N stations, via central MQTT).
- Compare mode (current batch vs historical batch from historian).
- Cross-process bridge (MQTT-backed replacement for the in-process
  PrimaryToSecondaryBridge).
```

---

## 6. Test plan

**Unit (Phase 1):**
- `PrimaryToSecondaryBridge` correct wiring (5+ cases)
- Bridge state transitions
- Bridge metrics

**Integration (Phase 4):**
- Console scenario: `multistation_calibration.txt` — end-to-end
  primary → bridge → secondary through real `Bootstrap`.

**Manual smoke (Phase 3):**
- Launch GTK binary, switch to multi-station layout, verify
  both panes render and react.
- E-STOP affects which station? Decision: it's a UI-level
  operator command that stops *both* (operator hit the panel
  E-STOP, not a per-station one). Document in ADR.
- Sign out / sign in cycle: multi-station layout persists.
- Language switch under multi-station: both panes re-translate.

**No new test bar required** beyond what exists for single-station
— same observer + presenter under test, just two instances of it.

---

## 7. Risks + open questions

**Risk: DashboardPage code duplication vs extract-helper refactor**

If the existing `DashboardPage` widget-building is large enough to
warrant extracting into a helper for reuse by
`MultiStationDashboardPage`, that refactor itself touches a
working class. Mitigate: do the refactor in Phase 2 as a
no-behavior-change first commit, get it through CI green, then
build on top.

**Risk: layout swap from multi-station back to single confuses
the presenter state**

When the operator switches from multi-station to single-station
layout at runtime, the second presenter + model must be cleanly
torn down. Mitigate: lifecycle handled by MainWindow's existing
page-rebuild mechanism (same pattern as language swap).

**Risk: visual real estate**

A 1920×1080 touchscreen split 50/50 leaves each pane at 960px
wide — tight for the current DashboardPage's element density.
Decide on review: either a 60/40 split (primary gets the
attention), or a denser layout in the multi-station variant.

**Open question: E-STOP semantics**

Should E-STOP stop both stations or only the focused one? Real-
world deployment likely stops both (operator at panel sees
emergency, hits one button). Default: stop both. Document in ADR.
Add a "Stop primary only" / "Stop secondary only" affordance later if
operators ask.

**Open question: alerts**

Alerts from both stations appear in the same sidebar AlertsPanel.
Should the alert message carry a station prefix ("MASTER:
Equipment 2 offline") or should the panel be split? Default:
prefix; cheaper UX, no panel-layout change.

---

## 8. Effort estimate (honest)

| Phase | Lower bound | Upper bound | Notes |
|---|---|---|---|
| 1 (bridge + tests) | 4 h | 6 h | New backend, isolated, well-bounded |
| 2 (multi-station page) | 6 h | 8 h | DashboardPage refactor is the wildcard |
| 3 (wiring + .ui + picker) | 4 h | 5 h | Touches main.cpp + MainWindow + config |
| 4 (scenario + docs + ADR) | 3 h | 4 h | Writing > coding |
| **Total** | **17 h** | **23 h** | ~2-3 working days |

**Spread across calendar**: realistically 1 working week given
review cycles + iterations on visual layout. **Plan for 2 weeks
calendar** to ship cleanly without crunch.

**Concrete branch + commit cadence**:
- Branch `feature/multi-station-primary-secondary` cut from `main`
  at Phase 1 start.
- Commits per phase: 1-3 commits each.
- PR opened at end of Phase 3 (working feature visible to
  reviewers) with "WIP — phase 4 docs incoming" header.
- PR ready-for-review at end of Phase 4.
- Self-review pass + screenshot before requesting external review.

---

## 9. Next action

When you give the go: I create the feature branch, copy this plan
into `docs/design/multi-station-primary-secondary.md` so reviewers
see it alongside the code, and start Phase 1.

If you want to iterate the plan first (different bridge design,
different layout shape, different scope), this file is the place
— we adjust here, then start.
