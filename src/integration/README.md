# `src/integration/` -- Integration Backends + Telemetry Bridges

Protocol-agnostic integration core plus four concrete network backends
(TCP, MQTT 3.1.1 + 5.0, Modbus TCP, OPC-UA) and the bridge layer that
moves data between them and the application model. The whole module is
GTK-free and presenter-free; drops into any C++20 server / daemon / HMI
that needs to expose a `ProductionModel`-shaped state over a wire
protocol.

---

## Why this module exists separately

An HMI on a shop floor never lives alone. It has to:

- **Publish telemetry** -- the quality bar a checkpoint just hit, the
  state a piece of equipment is in, the work unit currently on the
  line -- to whatever monitoring stack the customer runs (Grafana,
  Ignition, OSIsoft PI, a SCADA historian, a cloud lake).
- **Accept ingest** -- sensor readings from PLCs, button presses from
  remote operator stations, commands from a manufacturing execution
  system (MES).

Different customers want different wires for the same payloads. One
plant wants OPC-UA because the rest of their stack is open62541
servers. The next wants MQTT 5 with retained messages on a Mosquitto
broker. A QA bench wants a quick TCP `nc` shell. A retrofit on a 20-
year-old PLC wants Modbus TCP on RS-485-over-Ethernet.

Putting all four behind a single `IntegrationBackend` interface means:

- The composition root (`main.cpp`) decides which backends are
  enabled at startup from a JSON config; presenters never know.
- A new protocol (gRPC, OPC-DA, AMQP) is a new subclass; existing
  backends, the manager, and every presenter stay untouched.
- The bridges between presenter and protocol are the only place
  payload format leaks in; serializers + formatters stay
  swappable.

---

## Architecture (SOLID at a glance)

```
                  ┌──────────────────────────┐
                  │   IntegrationBackend     │  abstract base
                  │   (start/stop/state)     │
                  └──────────▲───────────────┘
                             │
        ┌───────────┬────────┴─────────┬───────────────┐
        │           │                  │               │
   TcpBackend  MqttClient    ModbusBackend       OpcUaBackend
                                                 (open62541)

                  ┌──────────────────────────┐
                  │   IntegrationManager     │  composes + fans
                  │  (start/stopAll, list)   │  out lifecycle
                  └──────────────────────────┘

   ProductionModel ──► TelemetryPublisher ──► PayloadFormatter ──► Backend wire
                       (snapshot fan-out)     (JSON/CSV/plain)

   Backend wire ──► TelemetrySubscriber ──► SensorIngestBridge ──► ProductionModel
                    (decode + dispatch)     (validates + applies)
```

**SOLID applied per interface:**

- **S**ingle responsibility -- backend owns the channel lifecycle.
  Serializer owns the byte format. Bridge owns the model coupling.
  Manager owns the bundle. Four small surfaces instead of one fat
  one.
- **O**pen/closed -- adding a new protocol = new backend subclass +
  one registration line in `main.cpp`. Adding a new format = new
  `Serializer` impl injected into an existing backend. Neither
  touches the other surface.
- **L**iskov -- `IntegrationManager` iterates
  `vector<unique_ptr<IntegrationBackend>>` and never downcasts.
  `start() / stop() / state()` keep the same contract across TCP,
  MQTT, Modbus, OPC-UA.
- **I**nterface segregation -- `IntegrationBackend` is intentionally
  narrow (5 verbs). Each protocol-specific helper (
  `MqttPacket::encodePublish`, `ModbusPdu::readHoldingRegisters`,
  `OpcUaBackend::registerNode`) lives on the concrete, not on the
  base.
- **D**ependency inversion -- backends receive `ProductionModel&` /
  `ProductsRepository&` / `TelemetryPublisher&` by reference;
  tests inject in-memory fakes via the same interfaces.

---

## Backends -- per-protocol overview

### `TcpBackend` (Boost.Asio)

Plain-text request/response over a long-lived TCP listener. Each
client connection runs on its own `tcp::socket` accepted by an
`acceptor` loop; commands are line-delimited (`\n`) and dispatched
synchronously against the model. Designed for operator shells,
scenario test pipes, and ad-hoc `nc` debugging -- not for high
throughput. The interactive console front-end (`industrial-hmi-
console`) speaks the same line protocol, so the same scripts that
drive a TCP shell also drive the console binary.

**Notable**: stop-time race fixed by capturing `weak_ptr` to the
accept loop instead of `shared_ptr` (otherwise the lambda holds a
strong reference to itself and the closure leaks at shutdown -- a
Valgrind-detected real bug, see commit history).

### `MqttClient` + `MqttPacket` (hand-rolled MQTT 3.1.1 / 5.0)

In-process MQTT client without external dependencies. `MqttPacket`
encodes / decodes the binary wire format byte-for-byte (CONNECT,
PUBLISH, SUBSCRIBE, PINGREQ, DISCONNECT, plus the 5.0-only
properties block); `MqttClient` runs the keep-alive timer + the
publish queue + the broker reconnect loop on a dedicated thread.
Supports both QoS 0 and QoS 1 with PUBACK round-trips.

**Why hand-rolled**: the deployments this serves run on devices
where `paho-mqtt-cpp` brings in too much dependency closure. The
~1k-line implementation is enough for the publish + subscribe paths
the HMI actually uses; QoS 2 (PUBREC / PUBREL / PUBCOMP) is
intentionally out of scope.

### `Modbus` (Modbus TCP master + slave bridge)

`ModbusPdu` encodes function codes 0x03 (Read Holding) + 0x06 (Write
Single) + 0x10 (Write Multiple). `ModbusClient` owns the TCP
connection to a slave and exposes a request-response API.
`ModbusPollLoop` schedules cyclic reads against a register map (one
entry per equipment slot) and feeds the deltas into the
`ModbusIngestBridge`, which validates + applies them to the model.

A `ModbusBackend` wrapping this stack lets the HMI itself act as a
master polling external PLCs; tested via the
`docker/modbus_slave_simulator.py` sidecar in the Compose stack.

### `OpcUa` (open62541 wrapper) -- optional

`open62541` is pulled via CMake `FetchContent` only when
`-DENABLE_OPCUA=ON`. `OpcUaBackend` registers a server node tree
derived from `FactoryNodeMap` (equipment status, quality
checkpoints, work unit state) and writes values as the model
changes. `OpcUaIngestBridge` reads back operator commands written
to writable nodes. The open62541 client side is also wrapped
(`Open62541Client`) for the rarer case where the HMI consumes
another OPC-UA server.

The OPC-UA path is the most "industrial-shop-floor" of the four --
Allen-Bradley, Siemens, and most SCADA stacks speak it natively.

---

## Bridges -- where wire meets model

The bridges are the only place coupling between transport + business
state happens; backends + presenters never know about each other.

### `TelemetryPublisher` + concrete telemetry bridges

Subscribes to the model (equipment changed, quality checkpoint
changed, work unit progressed) and fans the snapshot to every
registered `TelemetryPayloadFormatter` (JSON, plain text, CSV).
Each backend that wants telemetry calls `subscribe(...)` once at
start; the bridge handles dispatch.

### `SensorIngestBridge`

The reverse direction. Receives decoded sensor readings from a
backend, validates the equipment id + value range, applies via
`ProductionModel::setEquipmentSupplyLevel` etc. The validation
mirrors the presenter-side guards (defence in depth -- a malicious
or malformed wire payload cannot push out-of-range state into the
model).

### `ProductionTelemetryBridge`

Specialised bridge that emits the "work unit started / progressed /
completed" lifecycle events as discrete messages rather than
periodic snapshots. Subscribed by audit-adjacent consumers
(Grafana annotations, MES event hooks).

---

## Serializers + formatters

Independent strategy axis from the backends -- any backend can ship
any format.

| Class | Format | Typical use |
|---|---|---|
| `CsvSerializer` | RFC 4180 CSV with UTF-8 BOM | Operator bulk exports |
| `JsonSerializer` / `JsonTelemetryFormatter` | Compact JSON object per event | MQTT, REST |
| `PlainTextTelemetryFormatter` | `key=value` lines | TCP / `nc` |

Adding a new format is one new `Serializer` impl (~80 lines for CSV
as a reference); zero changes to any backend.

---

## Embedding in another C++ project

Minimum dependencies: `boost-system` + `boost-asio` (header-only),
`open62541` if OPC-UA is wanted, otherwise zero. C++20 compiler.

### Bootstrap (~25 lines)

```cpp
#include "integration/IntegrationManager.h"
#include "integration/TcpBackend.h"
#include "integration/MqttClient.h"

app::integration::IntegrationManager mgr;

if (config.isTcpBackendEnabled()) {
    mgr.registerBackend(
        std::make_unique<app::integration::TcpBackend>(
            config.getTcpBackendPort(), model, productsRepo));
}

if (config.isMqttEnabled()) {
    mgr.registerBackend(
        std::make_unique<app::integration::MqttClient>(
            config.getMqttBrokerHost(), config.getMqttBrokerPort(),
            telemetryPublisher));
}

mgr.startAll();    // non-blocking; each backend on its own thread
// ... run the application ...
mgr.stopAll();     // blocking; joins every backend thread
```

### Wiring telemetry

```cpp
app::integration::TelemetryPublisher publisher{model};
publisher.addFormatter(app::integration::JsonTelemetryFormatter{});
publisher.addSink(mqttClient);     // MQTT publishes the JSON
publisher.addSink(tcpBackend);     // TCP shell also sees it
```

### Adding a new protocol

```cpp
class GrpcBackend : public app::integration::IntegrationBackend {
public:
    GrpcBackend(model::ProductionModel& m, std::string addr);
    bool start() override;
    bool stop()  override;
    app::integration::BackendState state() const override;
    std::string name() const override          { return "grpc"; }
    std::string metricsSummary() const override;
};

// In main.cpp:
mgr.registerBackend(std::make_unique<GrpcBackend>(model, "0.0.0.0:50051"));
// Done. No other file changes.
```

---

## Threading model

- Each backend runs on its **own dedicated thread** (`std::jthread`
  or Boost.Asio `io_context::run` on a worker), so a stalled MQTT
  broker never blocks TCP serving or vice versa.
- All inter-thread state goes through `ProductionModel`'s mutex +
  copy-on-read snapshot pattern. Backends never share data
  directly.
- Shutdown is synchronous (`stopAll()` joins). Asymmetry is
  intentional: starting in parallel is fine, but the composition
  root needs to know when every backend has stopped before
  releasing the model.

---

## Health surfaced to the UI

Every backend exposes a `BackendState` (Disconnected / Connecting
/ Connected / Degraded) consumed by `BackendHealthPresenter` and
rendered as a coloured dot in the sidebar's I/O panel. State
changes are notified via a callback the manager wires at
registration -- no polling.

Granular per-backend metrics live in `metricsSummary()`: bytes
sent / received, last error, time since last successful publish.
Surfaced in the `Settings -> Backends` page for an operator
diagnosing connectivity issues without dropping to a shell.

---

## Testing

`tests/IntegrationManagerTest.cpp` -- start/stop matrix, partial-
failure handling (one backend throws, others stay up), the
`lastStartErrors()` accumulator.

`tests/TcpBackendTest.cpp` -- accept loop, line-protocol round
trips, the weak_ptr capture fix verified under Valgrind.

`tests/MqttClientTest.cpp` + `tests/MqttPacketTest.cpp` -- byte-
exact encode / decode for CONNECT / PUBLISH / SUBSCRIBE / PINGREQ /
DISCONNECT (3.1.1 and 5.0 with property blocks), broker reconnect
loop with simulated socket errors.

`tests/ModbusPduTest.cpp` + `tests/ModbusClientTest.cpp` --
function-code encode / decode, master-side request-response,
poll loop dispatch into the ingest bridge.

`tests/OpcUaBackend*Test.cpp` -- end-to-end against an in-process
open62541 server (gated behind `-DENABLE_OPCUA=ON`).

`tests/SensorIngestBridgeTest.cpp` + `tests/ProductionTelemetryBridgeTest.cpp`
-- validation + dispatch through to the model.

Run isolated:

```bash
cd build/debug
ctest -R '(Backend|Mqtt|Modbus|Integration|OpcUa|Telemetry|Ingest|Csv|Json)' --output-on-failure
```

---

## Out of scope (intentional)

- **gRPC / Protobuf** -- the four shipped protocols cover every
  customer we've seen. gRPC drops in as a new backend whenever a
  customer asks; no architectural change required.
- **MQTT QoS 2** -- the publish + subscribe paths the HMI uses
  don't need 4-step handshakes; the wire encoder leaves the
  PUBREC / PUBREL / PUBCOMP packet ids reserved but never sends
  them.
- **TLS / mTLS on TCP + MQTT** -- shop floor deployments terminate
  TLS at a reverse proxy (haproxy, Traefik). Adding native TLS
  would couple every backend to a TLS library and is rarely worth
  it at industrial scale.
- **TCP RPC framing (length-prefix, MessagePack)** -- the line
  protocol is operator-debuggable and Good Enough for the demo
  surface. A binary framing layer would be a new backend, not a
  modification.
