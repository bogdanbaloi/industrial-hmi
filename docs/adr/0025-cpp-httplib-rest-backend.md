# 0025. cpp-httplib for a read-only REST/HTTP integration backend

## Status

Accepted (2026-09). Extends ADR-0005 (the IntegrationBackend interface every
protocol implements) with a fifth backend, and reuses the read-only safety
posture ADR-0023 established for the MCP server. Builds on ADR-0015
(nlohmann/json for structured payloads) and ADR-0014 (Result / errors at the
boundary).

## Context

The application already speaks four wire protocols behind one
`IntegrationBackend` seam: a line-oriented TCP protocol, MQTT 3.1.1, Modbus
TCP and OPC-UA. Each is a long-lived backend the `IntegrationManager` starts and
stops uniformly. None of them is reachable from an ordinary web client: there is
no `curl http://host/status`, no browser tab, no dashboard-embeddable JSON feed.

A read-only REST/HTTP endpoint is the standard-web-client sibling of the
protocols we already serve. It exposes the same production snapshot, alarm list
and product catalogue over `GET` requests any HTTP client understands, which is
the lowest-friction way for an external monitoring tool, a status page or a demo
to read plant state. The value is in reusing the existing read seams
(`ProductionModel`, `ProductsRepository`, `presenter::AlertCenter`) rather than
inventing a new data path.

## Decision

Add a fifth backend, `HttpBackend`, implementing `IntegrationBackend`, serving
four read-only routes as JSON:

- `GET /health` -> `{"status":"ok"}` (liveness, no model access)
- `GET /status` -> the production state snapshot
- `GET /alarms` -> the active-alarm snapshot
- `GET /products` -> the product catalogue

It is opt-in behind `network.http.enabled` (default false) and, at build time,
behind `BUILD_HTTP_BACKEND` (default OFF), mirroring the opt-in gating of the
OPC-UA backend. Its lifecycle mirrors `TcpBackend`: a bind that fails fast in
`start()`, an accept loop on an owned `std::jthread`, and a `stop()` that unblocks
the loop before joining.

- **cpp-httplib via FetchContent.** cpp-httplib is a single public-domain (MIT)
  header with a tiny server API (`Server::Get`, `bind_to_any_port`, `stop`). It
  pulls in no runtime dependency, is vendored the same way nlohmann/json,
  Boost.SML and open62541 already are, and its header-only nature keeps it out
  of every translation unit but the one that includes it (the server is PIMPL'd
  behind a forward-declared `httplib::Server`).
- **Read-only, no write route in this version.** No `POST`/`PUT` route is
  exposed. A state-changing HTTP route would let an unauthenticated client
  change plant state, so it waits on the same authorization story the MCP server
  deferred its write tool behind (ADR-0023): such a route belongs behind
  `auth::Session`, role-gated and audited.
- **The alarm payload is shared with the MCP server.** `GET /alarms` calls
  `app::mcp::runAlarmsSnapshot(AlertCenter&)` verbatim -- the same read-only,
  thread-safe projection the MCP `alarms_snapshot` tool serves -- so the two
  consumers cannot drift.
- **The status payload is shared with TcpBackend.** The status-JSON shape is
  extracted into `buildStatusJson(const ProductionModel&)` (StatusJson.h/.cpp)
  and called by both `TcpBackend` and `HttpBackend`, so the `status` command and
  the `/status` route stay identical.
- **Routes are a named constexpr table.** The four paths are `constexpr` named
  constants in one route table, not scattered string literals; the table is the
  single place a route is declared and the seam a unit test inspects for
  duplicate paths.

## Consequences

- The IntegrationBackend seam now spans five protocols with one lifecycle
  contract; adding HTTP touched no model or presenter code.
- `HttpBackend` is the first integration backend to read presenter-layer state
  (`AlertCenter`), which the pre-presenter `buildIntegrationServices` composition
  does not own. The bootstrap therefore accepts the `AlertCenter` by pointer and
  registers the backend only when one is supplied; wiring a live `AlertCenter`
  into the shared composition is follow-up work, tracked separately, and the
  backend and its tests are complete independently of it.
- cpp-httplib calls Winsock directly on Windows (not through Boost.Asio), so the
  `objectsHttp` object library carries its own explicit `ws2_32` / `mswsock`
  link line rather than relying on the transitive Winsock the Boost-based
  backends inherit.

## Alternatives rejected

- **Boost.Beast.** Already available through the Boost dependency, but its
  ASIO-coroutine HTTP server is a large amount of boilerplate for four `GET`
  routes and is inconsistent with the hand-rolled, raw-socket style the rest of
  the integration layer uses. cpp-httplib's `Server::Get` maps one-to-one onto
  the route surface we need.
- **A write route in v1.** Rejected for the same reason ADR-0023 deferred the
  MCP write tool: an unauthenticated actor must not change plant state before an
  authorization and audit story exists.
- **Rolling a raw HTTP/1.1 parser by hand** (as we did for MQTT/Modbus framing).
  Justified for a binary industrial protocol whose framing is the portfolio
  point; not justified for HTTP, where a correct, fuzzed, widely-used
  single-header implementation exists and the value is the reuse of the model
  seams, not the parser.
