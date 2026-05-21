# 0005. IntegrationBackend interface for all protocols

## Status

Accepted (2026-02)

## Context

The HMI integrates with four distinct industrial protocols: TCP line
protocol for external supervisors, MQTT (publish + subscribe) for
fleet telemetry, Modbus TCP master polling PLCs, and OPC-UA (both
server and client) for SCADA topologies. More are likely later
(Modbus RTU over serial, S7 / Profinet, KNX in non-industrial
deployments).

Each protocol has its own library — `paho` for MQTT, `libmodbus`
for Modbus, `open62541` for OPC-UA, raw sockets for TCP. They have
nothing in common at the API level: different threading models,
different connection semantics, different reconnect strategies.

If every protocol exposes its own thread, its own callback shape,
and its own "what state am I in" enum, the presenter ends up with
a switch on protocol type buried in every code path that touches
integration. That's the opposite of OCP.

## Decision

A single abstract interface `app::IntegrationBackend` in
`src/integration/IntegrationBackend.h`:

```cpp
class IntegrationBackend {
public:
    virtual ~IntegrationBackend() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual std::string name() const = 0;
    virtual BackendState connectionState() const noexcept;
    virtual std::string metricsSummary() const;
};
```

`IntegrationManager` owns a `std::vector<std::unique_ptr<IntegrationBackend>>`
and calls `startAll()` / `stopAll()` on shutdown. Concrete backends
(`TcpBackend`, `MqttBackend`, `ModbusBackend`, `OpcUaBackend`) own
their own thread and reconnect loop; they push state changes to the
presenter through `BackendHealthPresenter` which then folds them into
`BackendHealthViewModel`.

The presenter never knows which protocol delivered which event — it
sees `EquipmentLevelSample{station_id, level, timestamp}` and stores
it. Wire format -> domain mapping happens inside the backend impl.

## Alternatives

- **One presenter per backend** — rejected. State aggregation
  (overall health, "which backend last delivered our equipment
  level") would still need a coordinator. Just push that
  coordinator down to `IntegrationManager` and keep one presenter.

- **Tagged union / variant per backend type** — rejected. Every
  consumer would `std::visit` to dispatch, and adding a new protocol
  would require touching every consumer. Defeats the abstraction.

- **Protocol-specific signals from each backend** — rejected for the
  same reason `sigc::signal` lost in ADR 0003: drags the protocol
  library into every observer, and observers would need
  protocol-specific knowledge to subscribe.

## Consequences

+ Adding a new protocol = implement four methods + register one
  factory line in `main.cpp`. Modbus RTU over serial would be a
  ~300-LOC backend plus a `socat`-based test harness in
  `tests/integration/`, no presenter changes required.
+ The `BackendHealthBar` view widget renders any number of backends
  identically, sorted alphabetically. Operator sees "OPC-UA: CONNECTED,
  MQTT: CONNECTING, TCP: ONLINE, Modbus: DEGRADED" with consistent
  pill styling per state.
+ Compile-time opt-out: each protocol library is gated by a CMake
  option (`BUILD_MQTT_BACKEND=ON/OFF` etc). Embedded builds without
  paho still produce a working binary, just with fewer backends
  available at runtime.
- `IntegrationBackend` carries a small `metricsSummary()` method
  that returns a free-form string; not all protocols populate it
  the same way. Worth tightening into a typed `BackendMetrics`
  struct in a follow-up if more views need it.
- Each backend owns its own thread. Four backends = four threads at
  rest. Acceptable for the deployment target (manufacturing
  terminal, not a battery-powered edge device), but a notable
  footprint cost vs an event loop architecture.
