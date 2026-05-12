#!/usr/bin/env python3
"""Live scenario: a console SCADA dashboard backed by OPC-UA polling.

Polls every monitored node on the HMI's OPC-UA server once per
`--interval` seconds and redraws a small in-place dashboard using
ANSI escape codes. Mirrors what an external SCADA / data historian
sees on the same wire that drives the GTK dashboard, so you can put
this script in one window and the HMI GUI in another and watch them
agree in real time.

Read-only by design: the script never writes to a node. The
`--readonly` flag from the common CLI is accepted but no-op (kept
for orchestrator uniformity).

Requires `asyncua`. Honours the shared `--interval`, `--duration`,
`--seed` flags documented in `examples/README.md`. The `--seed`
flag is accepted but has no effect here -- the script is fully
deterministic given the HMI state.

Run alongside the HMI:

    Terminal 1:  ./build/with-backends/industrial-hmi
    Terminal 2:  python examples/opcua_dashboard.py
"""

from __future__ import annotations

import argparse
import asyncio
import signal
import sys
import time
from datetime import datetime

try:
    from asyncua import Client, ua
except ImportError:
    print("FAIL  asyncua is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


# Same encoding `ProductionTypes.h` documents. Mapping done locally
# (not over the wire) so the dashboard label stays human-readable.
EQUIPMENT_STATUS_LABELS = {
    0: "OFFLINE",
    1: "ONLINE",
    2: "PROCESSING",
    3: "ERROR",
}
QUALITY_STATUS_LABELS = {
    0: "PASSING",
    1: "WARNING",
    2: "CRITICAL",
}
SYSTEM_STATE_LABELS = {
    0: "IDLE",
    1: "RUNNING",
    2: "PAUSED",
    3: "CALIBRATING",
    4: "ERROR",
}

NAMESPACE_INDEX = 1  # ns=1 in our address space

# Group nodes by section so the dashboard layout is stable. Each
# tuple is (display label, browse path under Objects/).
SYSTEM_NODES = [
    ("System state",    ["Factory", "State"]),
]
EQUIPMENT_NODES = [
    ("A-LINE", [
        ("status", ["Factory", "EquipmentLines", "Line0", "Status"]),
        ("supply", ["Factory", "EquipmentLines", "Line0", "SupplyLevel"]),
    ]),
    ("B-LINE", [
        ("status", ["Factory", "EquipmentLines", "Line1", "Status"]),
        ("supply", ["Factory", "EquipmentLines", "Line1", "SupplyLevel"]),
    ]),
    ("C-LINE", [
        ("status", ["Factory", "EquipmentLines", "Line2", "Status"]),
        ("supply", ["Factory", "EquipmentLines", "Line2", "SupplyLevel"]),
    ]),
]
QUALITY_NODES = [
    ("Weight Check",   [
        ("status", ["Factory", "QualityCheckpoints", "Checkpoint0", "Status"]),
        ("rate",   ["Factory", "QualityCheckpoints", "Checkpoint0", "PassRate"]),
    ]),
    ("Hardness Test",  [
        ("status", ["Factory", "QualityCheckpoints", "Checkpoint1", "Status"]),
        ("rate",   ["Factory", "QualityCheckpoints", "Checkpoint1", "PassRate"]),
    ]),
    ("Final Inspection", [
        ("status", ["Factory", "QualityCheckpoints", "Checkpoint2", "Status"]),
        ("rate",   ["Factory", "QualityCheckpoints", "Checkpoint2", "PassRate"]),
    ]),
]
WORK_UNIT_NODES = [
    ("Completed", ["Factory", "WorkUnit", "CompletedOperations"]),
    ("Total",     ["Factory", "WorkUnit", "TotalOperations"]),
]


# ANSI escape sequences. We restrict ourselves to widely-supported
# ones so this works on every terminal that runs the GTK build (cmd,
# Windows Terminal, mintty, xterm, gnome-terminal, etc.).
ANSI_CLEAR_SCREEN = "\x1b[2J\x1b[H"
ANSI_RESET        = "\x1b[0m"
ANSI_BOLD         = "\x1b[1m"
ANSI_GREEN        = "\x1b[32m"
ANSI_YELLOW       = "\x1b[33m"
ANSI_RED          = "\x1b[31m"
ANSI_DIM          = "\x1b[2m"


def colored(value, status_code: int, palette: dict) -> str:
    """Wrap `value` in the ANSI colour appropriate to `status_code`.
    Falls back to plain text if the code isn't in the palette."""

    color = palette.get(status_code, "")
    return f"{color}{value}{ANSI_RESET}" if color else str(value)


# Per-section status -> colour mapping.
EQUIPMENT_PALETTE = {
    0: ANSI_DIM,   # OFFLINE
    1: ANSI_GREEN, # ONLINE
    2: ANSI_GREEN, # PROCESSING
    3: ANSI_RED,   # ERROR
}
QUALITY_PALETTE = {
    0: ANSI_GREEN,  # PASSING
    1: ANSI_YELLOW, # WARNING
    2: ANSI_RED,    # CRITICAL
}
SYSTEM_PALETTE = {
    0: ANSI_DIM,
    1: ANSI_GREEN,
    2: ANSI_YELLOW,
    3: ANSI_YELLOW,
    4: ANSI_RED,
}


async def resolve_nodes(client: Client, paths: list[list[str]]):
    nodes = []
    for path in paths:
        qualified = [f"{NAMESPACE_INDEX}:{name}" for name in path]
        try:
            node = await client.nodes.objects.get_child(qualified)
            nodes.append(node)
        except (ua.UaStatusCodeError, Exception) as exc:  # noqa: BLE001
            print(f"WARN  could not resolve {'/'.join(path)}: {exc}",
                  file=sys.stderr)
            nodes.append(None)
    return nodes


async def read_or_none(node):
    if node is None:
        return None
    try:
        return await node.read_value()
    except Exception:  # noqa: BLE001
        return None


def render(values, started_at: float) -> str:
    elapsed = time.monotonic() - started_at
    ts = datetime.now().strftime("%H:%M:%S")
    lines = []
    lines.append(f"{ANSI_BOLD}Industrial HMI -- "
                 f"OPC-UA dashboard{ANSI_RESET}    "
                 f"{ts}   uptime {elapsed:7.1f}s")
    lines.append("-" * 72)

    # System
    sys_state = values["system_state"]
    sys_label = SYSTEM_STATE_LABELS.get(sys_state, f"<{sys_state}>")
    sys_value = colored(sys_label, sys_state or 0, SYSTEM_PALETTE)
    lines.append(f"  System state    : {sys_value}")
    lines.append("")

    # Equipment
    lines.append(f"{ANSI_BOLD}  Equipment lines{ANSI_RESET}")
    for label, status, supply in values["equipment"]:
        status_label = EQUIPMENT_STATUS_LABELS.get(status, f"<{status}>")
        status_color = colored(f"{status_label:<11}", status or 0,
                               EQUIPMENT_PALETTE)
        supply_str = f"{supply:>3d}%" if supply is not None else "  -%"
        lines.append(f"    {label:<10}  {status_color}  supply {supply_str}")
    lines.append("")

    # Quality
    lines.append(f"{ANSI_BOLD}  Quality checkpoints{ANSI_RESET}")
    for label, status, rate in values["quality"]:
        status_label = QUALITY_STATUS_LABELS.get(status, f"<{status}>")
        status_color = colored(f"{status_label:<9}", status or 0,
                               QUALITY_PALETTE)
        rate_str = f"{rate:5.1f}%" if rate is not None else "  -.--%"
        lines.append(f"    {label:<18}  {status_color}  pass rate {rate_str}")
    lines.append("")

    # Work unit
    completed, total = values["work_unit"]
    if completed is not None and total is not None:
        progress = f"{completed}/{total}"
    else:
        progress = "-/-"
    lines.append(f"  Work unit       : {progress}")
    lines.append("")
    lines.append(f"{ANSI_DIM}  ctrl-C to stop{ANSI_RESET}")

    return ANSI_CLEAR_SCREEN + "\n".join(lines) + "\n"


async def run(endpoint: str, interval: float,
              duration: float | None) -> int:
    print(f"      dialing {endpoint} ...")
    async with Client(endpoint) as client:
        print("OK    session established, building monitored set ...")

        sys_state_node = (await resolve_nodes(
            client, [SYSTEM_NODES[0][1]]))[0]

        equipment_nodes = []
        for label, fields in EQUIPMENT_NODES:
            field_paths = [path for _, path in fields]
            resolved = await resolve_nodes(client, field_paths)
            equipment_nodes.append((label, resolved))

        quality_nodes = []
        for label, fields in QUALITY_NODES:
            field_paths = [path for _, path in fields]
            resolved = await resolve_nodes(client, field_paths)
            quality_nodes.append((label, resolved))

        wu_resolved = await resolve_nodes(
            client, [path for _, path in WORK_UNIT_NODES])

        start_ts = time.monotonic()
        stop_event = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, stop_event.set)
            except (NotImplementedError, RuntimeError):
                # Windows ProactorEventLoop doesn't expose
                # add_signal_handler; ctrl-C still surfaces as
                # KeyboardInterrupt via the outer try/except.
                pass

        while not stop_event.is_set():
            if duration is not None and (
                    time.monotonic() - start_ts) >= duration:
                break

            values = {
                "system_state": await read_or_none(sys_state_node),
                "equipment":    [],
                "quality":      [],
                "work_unit":    (None, None),
            }
            for label, resolved in equipment_nodes:
                status = await read_or_none(resolved[0])
                supply = await read_or_none(resolved[1])
                values["equipment"].append((label, status, supply))
            for label, resolved in quality_nodes:
                status = await read_or_none(resolved[0])
                rate   = await read_or_none(resolved[1])
                values["quality"].append((label, status, rate))
            completed = await read_or_none(wu_resolved[0])
            total     = await read_or_none(wu_resolved[1])
            values["work_unit"] = (completed, total)

            sys.stdout.write(render(values, start_ts))
            sys.stdout.flush()

            try:
                await asyncio.wait_for(stop_event.wait(),
                                        timeout=interval)
            except asyncio.TimeoutError:
                pass

        print("\nDONE  closing session")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint",
                        default="opc.tcp://127.0.0.1:4840",
                        help="OPC-UA endpoint URL "
                             "(default: opc.tcp://127.0.0.1:4840)")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="Seconds between dashboard refreshes "
                             "(default: 1.0)")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-exit after N seconds.")
    parser.add_argument("--seed", type=int, default=None,
                        help="(accepted for orchestrator uniformity; "
                             "this script is deterministic given "
                             "the HMI state).")
    parser.add_argument("--readonly", action="store_true",
                        help="(accepted for orchestrator uniformity; "
                             "this script is already read-only).")
    args = parser.parse_args()
    try:
        return asyncio.run(run(args.endpoint, args.interval, args.duration))
    except KeyboardInterrupt:
        print("\nDONE  ctrl-C")
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL  {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
