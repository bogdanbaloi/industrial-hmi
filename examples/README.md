# Client scripts (`examples/`)

Reproducible Python scripts that exercise every integration backend
the HMI ships with. Two flavours:

1. **Quick checks** -- single-shot scripts that send one request,
   print the result, exit. Useful while developing, for CI smoke
   tests, or to teach the protocol surface to a new engineer.
2. **Live scenarios** -- long-running loops that simulate a real
   actor on the wire (an operator at a TCP terminal, a fleet of
   sensors publishing MQTT, a SCADA polling OPC-UA). Useful for
   customer demos and for stress-testing the HMI under realistic
   continuous load.

Plus one **orchestrator** that runs every live scenario in parallel,
so a single command produces a fully populated "day-in-the-life"
demo across three protocols at once.

## Setup

Python 3.10 or newer.

```bash
# WSL Ubuntu / Linux / macOS:
python -m venv .venv
source .venv/bin/activate
pip install -r examples/requirements.txt

# Windows MSYS2 CLANG64 -- the `cryptography` wheel (a transitive
# asyncua dependency) needs Rust toolchain support, which is awkward
# on MSYS2. The TCP and MQTT scripts work on bare Python without
# the venv; for OPC-UA, run the scripts from WSL.
```

`paho-mqtt` and `asyncua` are the only non-stdlib dependencies, both
pinned in `requirements.txt` to ranges that match what the HMI
implements on the wire (MQTT 3.1.1, OPC-UA 1.04).

## Quick checks (single-shot)

| Script | Backend role | What it does |
|---|---|---|
| `tcp_control.py` | HMI = TCP server | Walks SYSTEM badge + equipment switches through a canned sequence of commands, prints replies, exits. |
| `mqtt_publish_sensor.py` | HMI = MQTT subscriber | Publishes one `on`/`off` message on a sensor topic. The matching equipment switch flips on the dashboard. |
| `opcua_read_state.py` | HMI = OPC-UA server | Browses `Objects/Factory/*` and reads every node the address space exposes. |
| `opcua_invoke_method.py` | HMI = OPC-UA server (inbound control) | Invokes a `Factory/Commands/*` method or writes `Factory/EquipmentLines/Line<id>/Enabled`. The HMI's `ProductionModel` reacts the same way it would to a TCP `production start` or a GTK button click. |

These are designed to be quick to run and quick to read. Each one is
under 100 lines so it doubles as documentation of the protocol it
demonstrates.

## Live scenarios (continuous, ctrl-C to stop)

| Script | Backend role | Continuous behaviour |
|---|---|---|
| `mqtt_subscribe_telemetry.py` | HMI = MQTT publisher | Tails every message the HMI publishes on `<prefix>/#`. Prints with timestamp. Useful as a passive observer next to a live HMI. |
| `opcua_subscribe_equipment.py` | HMI = OPC-UA server | Subscribes to the three equipment Status nodes; prints every data-change notification. |
| `tcp_operator_session.py` | HMI = TCP server | Holds a TCP socket open and behaves like an attentive operator: polls `status` every five seconds, toggles a random equipment every thirty seconds, restarts production every two minutes. The TCP pill stays green for the entire session. |
| `mqtt_sensor_loop.py` | HMI = MQTT subscriber | Publishes a steady stream of sensor events on `<sensor_prefix>/equipment/<id>/state` -- alternating on/off across the three lines, configurable cadence. Demonstrates that `SensorIngestBridge` handles continuous traffic, not just a single message. |
| `opcua_dashboard.py` | HMI = OPC-UA server | Polls every monitored node once per second and redraws a small console dashboard in place using ANSI escape codes. Mirrors what an external SCADA would see. |

All live scenarios accept the same set of flags (see below) so you
can replay a deterministic timeline, cap them at a fixed duration
for screenshots, or run them in observe-only mode.

## Orchestrator

`factory_simulation.py` runs three live scenarios in parallel:

- `tcp_operator_session.py` against the TCP backend
- `mqtt_sensor_loop.py` against the MQTT subscriber
- `opcua_dashboard.py` against the OPC-UA server

One command, three protocols hitting the HMI continuously. This is
the script to launch when demonstrating the system to a customer:
the dashboard reflects work from each backend in real time, the I/O
panel pills all turn green at once, and the live scenarios stay alive
until the demonstrator presses ctrl-C.

## Common flags

Every live scenario accepts the same flags so a deployment engineer
can compose them consistently:

| Flag | Default | Meaning |
|---|---|---|
| `--interval N` | per-script | Seconds between actions / polls. |
| `--duration N` | infinite | Auto-exit after N seconds. Useful for recording fixed-length demo videos and for CI. |
| `--seed N` | OS random | Deterministic random sequence. Two runs with the same seed produce the same timeline -- handy for reproducible screenshots and bug reports. |
| `--readonly` | off | Where supported, observe only -- do not send commands or publish anything. Lets an auditor watch the HMI without modifying state. |
| `--host` / `--port` / `--broker` / `--endpoint` | per-protocol | Override the connection target. Default targets are the local HMI on its documented ports. |

## Prerequisites

| Backend | Prereq | Default target |
|---|---|---|
| TCP | none -- HMI starts the server when `network.tcp.enabled = true` | `127.0.0.1:5555` |
| MQTT | a broker. Easiest is `mosquitto -p 1883` locally. | `127.0.0.1:1883` |
| OPC-UA | none -- HMI starts the server when `network.opcua.enabled = true` | `opc.tcp://127.0.0.1:4840` |

The HMI's default configuration enables TCP and OPC-UA out of the
box; MQTT requires either a local broker or pointing the config at
a public broker (`test.mosquitto.org` works without authentication).

## Recommended demo flow

A demonstrator running the system in front of a customer typically
sets things up like this:

```text
Terminal 1: broker
    mosquitto -p 1883

Terminal 2: HMI
    ./build/with-backends/industrial-hmi

Terminal 3: orchestrator
    python examples/factory_simulation.py

Terminal 4 (optional): passive observer
    python examples/mqtt_subscribe_telemetry.py
```

The dashboard's I/O panel shows three green pills (TCP, MQTT, OPC-UA)
within seconds of launching the orchestrator. The simulator inside
the HMI runs alongside the external actors, so the operator sees the
same kind of traffic a real plant would produce.

For short recordings, add `--duration 60 --seed 42` to the
orchestrator: a one-minute reproducible run, identical screenshot
every time.

## When to use which

- **Day-to-day development, single-protocol debug**: pick one quick
  check or one live scenario. Quickest feedback.
- **Customer demo**: run the orchestrator. Three protocols, one
  command, ten seconds to set up.
- **Pre-deployment validation**: leave the orchestrator running for
  an hour against the target deployment; if no scenario disconnects
  and no pill flips off green, the integration surface is healthy.
- **Bug reproduction**: pin `--seed` and `--duration`, share the
  command in the bug report. The maintainer reproduces by running
  the same line.
