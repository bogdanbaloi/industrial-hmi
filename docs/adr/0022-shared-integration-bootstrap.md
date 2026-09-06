# 0022. Shared integration bootstrap across frontends

## Status

Accepted (2026-09). Extends ADR-0005 (integration manager + backend interface)
and ADR-0020 (the Qt frontend and the toolkit-independence proof). Related to
ADR-0011 (multi-station primary/secondary bridge).

## Context

Every integration backend (TCP, MQTT, OPC-UA server + client, Modbus, the
multi-station bridge) was wired directly in `main.cpp`: ~450 lines of
config-gated construction, plus the owned side objects (the outbound/inbound
bridges, the OPC-UA command sink referenced by its node map, the secondary
mirror model) that must out-live the manager. The console shared `main.cpp`
via `#ifdef CONSOLE_MODE`, but nothing else could reuse the wiring.

Adding backend health to the Qt frontend forced the question: the
`BackendHealthPresenter` needs an `IntegrationManager` populated with backends,
and the only place that builds one is `main.cpp`. Duplicating the wiring in the
Qt composition root would fork ~450 lines and let the two drift on "what wire
protocols does this binary speak".

## Decision

Extract the wiring into a shared, toolkit-agnostic module,
`src/app/IntegrationBootstrap.{h,cpp}` (object library
`objectsIntegrationBootstrap`). `buildIntegrationServices(config, logger)`
builds -- but does not start -- every config-enabled backend and returns an
owning `IntegrationServices` bundle: the `IntegrationManager` plus the side
objects, with the manager declared first so, on destruction, the bridges that
reference the manager's backends tear down before it. The caller starts the
manager (`startAll`) and holds the bundle for the process lifetime.

`main.cpp` (GTK + console) and `QtInitRoot` both call it; auth and historian
composition stay in `main.cpp` since they are not integration concerns. The
Qt frontend renders the result through the existing `BackendHealthPresenter`
and the `ViewObserver` seam (the Connectivity page).

The bootstrap object library links every concrete protocol library, so the
`INDUSTRIAL_HMI_HAS_OPCUA_BACKEND` / `_MODBUS_BACKEND` defines (PUBLIC on those
libraries) reach the bootstrap and, transitively, all three frontend binaries.
That is required for correctness, not just convenience: `IntegrationServices`
has `#ifdef`-gated members, so every translation unit that includes the header
must agree on the backend define set or the struct layout would differ (an ODR
violation).

## Consequences

+ One composition point for the integration layer. "What protocols does this
  binary speak" is answered once, and the GTK, console and Qt binaries cannot
  drift apart.
+ The integration layer is now as toolkit-independent as the presenter layer
  (ADR-0002 / ADR-0020): a third composition root reuses it unchanged. That is
  the same MVP-boundary argument extended one layer outward.
+ The Qt frontend gets real backend health (the Connectivity page) with no new
  backend code -- it reuses `BackendHealthPresenter` and the manager.
- Every frontend now links every protocol library through the bootstrap, so the
  link line is heavier even for a binary that a deployment runs with few
  backends enabled. Accepted: the backends are already small object libraries
  and the alternative (per-frontend wiring) is worse.
- The `#ifdef`-gated bundle members mean all binaries must share the same
  backend-define set. Enforced structurally by the PUBLIC linking above rather
  than by convention.
