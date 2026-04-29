# Integration Protocol Reference

Wire-level reference for the two integration channels exposed by
`industrial-hmi`: a line-oriented **TCP command protocol** and a
publish-only **MQTT 3.1.1** telemetry feed. Both are off by default and
must be enabled in `config/app-config.json` (see `network.tcp.enabled`
and `network.mqtt.enabled`).

This document is the contract a third-party client (web frontend,
tablet app, dashboard, scripted operator tool) is written against. The
shapes documented here are stable across patch releases; breaking
changes get a major-version bump.

## TCP Command Protocol

### Connection

- Plain TCP, no TLS. Tunnel through stunnel / wireguard for production.
- Default port `5555` (configurable via `network.tcp.port`).
- One acceptor, one client at a time per accept iteration. Multiple
  concurrent clients share the io_context and are isolated from each
  other -- a slow / dead client cannot block the others past the read
  timeout.
- Encoding: ASCII, newline-terminated (`\n`). `\r\n` is also accepted on
  request lines (the trailing `\r` is stripped server-side).

### Command summary

| Command                       | Response                                              |
|-------------------------------|-------------------------------------------------------|
| `status`                      | one JSON line (state + running)                       |
| `dashboard`                   | one JSON line (full snapshot for client hydration)    |
| `products`                    | count line, then N JSON product lines                 |
| `eq <id> on\|off`             | `OK` or `ERR <reason>`                                |
| `production start\|stop\|reset` | `OK` or `ERR <reason>`                              |
| `help`                        | multi-line usage banner                               |
| `quit` / `exit`               | `BYE`, server closes the connection                   |

Unknown commands return `ERR unknown command: <token>`. Empty input
lines are ignored.

### `status`

Snapshot of the production state machine. Returns exactly one JSON
object on a single line.

```
> status
{"state":"running","running":true}
```

Schema:

| Field     | Type    | Values                                          |
|-----------|---------|-------------------------------------------------|
| `state`   | string  | `idle`, `running`, `error`, `calibration`       |
| `running` | boolean | `true` iff `state == "running"`                 |

### `dashboard`

Full snapshot of the production line in a single JSON line. Designed
for tablet / web / mobile clients to hydrate their UI in one round trip
on connect; live updates after that should arrive via MQTT.

```
> dashboard
{"state":"running","running":true,"equipment":[{"id":0,"status":2,"supplyLevel":85,"message":"85K tablets/hr"},{"id":1,"status":1,"supplyLevel":95,"message":"Film coating"}],"quality":[{"id":0,"name":"Weight Check","status":0,"unitsInspected":645,"defectsFound":12,"passRate":98.1,"lastDefect":"Underweight"}],"workUnit":{"id":"WU-2024-001234","productId":"TAB-200","description":"Batch WU-2024-001234 | TAB-200","completed":3,"total":5}}
```

Top-level schema:

| Field       | Type    | Notes                                       |
|-------------|---------|---------------------------------------------|
| `state`     | string  | Same values as `status`                     |
| `running`   | boolean | Same as `status`                            |
| `equipment` | array   | Every known equipment line                  |
| `quality`   | array   | Every known quality checkpoint              |
| `workUnit`  | object  | Current work unit on the line               |

`equipment[]` element schema:

| Field         | Type    | Notes                                       |
|---------------|---------|---------------------------------------------|
| `id`          | integer | Equipment id (0..N-1)                       |
| `status`      | integer | `0` offline, `1` online, `2` processing, `3` error |
| `supplyLevel` | integer | 0..100 percent                              |
| `message`     | string  | Free-form status text (JSON-escaped)        |

`quality[]` element schema:

| Field            | Type    | Notes                                       |
|------------------|---------|---------------------------------------------|
| `id`             | integer | Checkpoint id                               |
| `name`           | string  | Human-readable                              |
| `status`         | integer | `0` passing, `1` warning, `2` critical      |
| `unitsInspected` | integer | Cumulative count                            |
| `defectsFound`   | integer | Cumulative count                            |
| `passRate`       | number  | Percentage, one decimal place               |
| `lastDefect`     | string  | Free-form (JSON-escaped); `""` if none      |

`workUnit` schema:

| Field         | Type    | Notes                                       |
|---------------|---------|---------------------------------------------|
| `id`          | string  | Work unit identifier                        |
| `productId`   | string  | Product code being processed                |
| `description` | string  | Human-readable summary                      |
| `completed`   | integer | Operations completed in the current cycle   |
| `total`       | integer | Total operations per cycle                  |

Empty arrays (`"equipment":[]`, `"quality":[]`) and the empty work
unit (`{"id":"","productId":"","description":"","completed":0,"total":0}`)
are valid and signal a freshly-booted simulator with no data yet.

### `products`

Lists every non-deleted product in the database. The first response
line is the decimal count; the next N lines are JSON product objects,
one per line (NDJSON-style, not a single JSON array).

```
> products
2
{ "productCode": "P-001", "name": "Alpha", "status": "active", "stock": 42, "qualityRate": 99.5 }
{ "productCode": "P-002", "name": "Beta",  "status": "active", "stock":  7, "qualityRate": 97.0 }
```

Per-product schema:

| Field         | Type    | Notes                                       |
|---------------|---------|---------------------------------------------|
| `productCode` | string  | Unique business key (escape rules: JSON)    |
| `name`        | string  | Human-readable                              |
| `status`      | string  | `active` / `discontinued` / etc.            |
| `stock`       | integer | Non-negative                                |
| `qualityRate` | number  | Percentage, one decimal place               |

The line-per-record format keeps the protocol streaming-friendly: a
client can parse each record as it arrives without waiting for the full
response.

### `eq <id> on|off`

Toggles equipment `<id>` (unsigned integer). Equipment ids are
`0..N-1` for an `N`-line installation; for the bundled simulator that
is `0..3`.

```
> eq 0 off
OK
> eq 99 on
OK            # silently accepted; out-of-range ids are a no-op in the simulator
> eq abc on
ERR bad id (expected unsigned integer)
> eq 0 maybe
ERR expected on|off
```

### `production start|stop|reset`

Drives the top-level state machine.

```
> production start
OK
> production stop
OK
> production reset
OK
> production sideways
ERR expected start|stop|reset
```

`reset` returns the simulator to its initial state (all equipment
online, work unit cleared, quality counters zeroed).

### `help`

Returns the canonical usage banner. Reply spans multiple lines; clients
should read until the next prompt rather than expecting a fixed line
count.

### `quit` / `exit`

Server replies `BYE\n` and closes the socket. The client should wait
for EOF before exiting to avoid a TCP RST.

### Error format

Any failure returns one line beginning with `ERR `. The remainder is
human-readable and not a stable identifier -- clients should not pattern
match on the suffix.

## MQTT Telemetry Feed

The MQTT backend is **publish-only**, QoS 0, MQTT 3.1.1 over plain TCP.
Designed as a one-way firehose for dashboards and historians; commands
go through TCP.

### Topic schema

With `topic_prefix = "factory-42/line-A"`:

| Topic                                            | Payload          |
|--------------------------------------------------|------------------|
| `factory-42/line-A/state`                        | `running` etc.   |
| `factory-42/line-A/equipment/<id>/state`         | `ok` or `fault`  |
| `factory-42/line-A/quality/<id>/rate`            | `98.5`           |

Subscribe with `mosquitto_sub -h <broker> -t '<prefix>/#' -v`.

### Quality of service

QoS 0 (fire-and-forget). Messages can be dropped on broker or network
failure. Do not use for command/control or audit trails -- use TCP for
those.

## Building a Tablet / Web / Mobile Client

Pragmatic split for clients that don't link the C++ binary:

1. **Read state via MQTT**, subscribed to `<prefix>/#`. This is the
   live dashboard feed -- low-latency, push-based, no polling.
2. **Issue commands via TCP**, opening a short-lived connection for
   each user action (`production start`, `eq 0 off`, etc.). Closing
   with `quit` is polite but optional -- the server tolerates abrupt
   client disconnects.
3. **Hydrate the products list via TCP** on connect, since MQTT does
   not stream the catalog.

This split keeps each transport doing what it is good at: MQTT for
fan-out telemetry, TCP for request/response control.

### Minimal browser sketch (WebSocket bridge needed)

Browsers cannot speak raw TCP / MQTT-over-TCP. Two viable routes:

- **MQTT over WebSockets**: configure the broker (mosquitto, EMQX,
  HiveMQ) to expose a WebSocket listener; subscribe with `mqtt.js` or
  `paho-mqtt`.
- **TCP via a thin reverse proxy**: a small WebSocket-to-TCP shim
  (~50 lines of Node / Go) bridges the browser to port 5555.

Native iOS / Android clients can use `Network.framework` (Swift) or
`okhttp` + a Kotlin MQTT library directly, with no shim.

## Versioning

Protocol shape is stable. Additive changes (new commands, new
JSON fields) are non-breaking and ship in any release. Removals or
field-type changes ship only with a major version bump and are called
out in `CHANGELOG.md` under "Protocol".
