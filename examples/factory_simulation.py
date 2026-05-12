#!/usr/bin/env python3
"""Orchestrator: run every live scenario in parallel.

Spawns three live scenarios as subprocesses against the HMI in
parallel:

  * `tcp_operator_session.py`   -- TCP, keeps the operator pill green
  * `mqtt_sensor_loop.py`       -- MQTT subscriber side, drives switches
  * `opcua_dashboard.py`        -- OPC-UA polling SCADA mirror

One command, three protocols hitting the HMI at once. The dashboard's
I/O panel shows three green pills (TCP, MQTT, OPC-UA) within seconds.
Ideal for customer demos and end-to-end smoke tests.

Honours the shared `--duration` and `--seed` flags; both propagate to
every child. `--readonly` does the same so an auditor can watch the
HMI under load without modifying state.

ctrl-C terminates all children cleanly. Each child's stdout is
prefixed so a single tail conveys what each protocol is doing.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import signal
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent


# Map a label to the script path + the flags that label needs over
# and above the shared common set. The shared set (--duration, --seed,
# --readonly, and shared protocol targets) gets layered on top by
# `build_command` so we don't repeat it three times.
SCENARIOS = {
    "TCP ": HERE / "tcp_operator_session.py",
    "MQTT": HERE / "mqtt_sensor_loop.py",
    "OPCU": HERE / "opcua_dashboard.py",
}


def build_command(script: Path, args: argparse.Namespace) -> list[str]:
    cmd: list[str] = [sys.executable, str(script)]
    if args.duration is not None:
        cmd += ["--duration", str(args.duration)]
    if args.seed is not None:
        cmd += ["--seed", str(args.seed)]
    if args.readonly:
        cmd += ["--readonly"]
    # Per-protocol overrides land on every scenario that accepts the
    # matching flag. Each script ignores flags it doesn't recognise
    # only when we explicitly omit them; argparse otherwise rejects
    # unknown flags, so we filter here per script type.
    name = script.name
    if name in {"tcp_operator_session.py"}:
        cmd += ["--host", args.tcp_host, "--port", str(args.tcp_port)]
    if name in {"mqtt_sensor_loop.py"}:
        cmd += ["--broker", args.mqtt_broker,
                "--port",   str(args.mqtt_port)]
    if name in {"opcua_dashboard.py"}:
        cmd += ["--endpoint", args.opcua_endpoint]
    return cmd


async def tail_child(label: str, process: asyncio.subprocess.Process) -> None:
    """Stream a child's stdout/stderr line-by-line to our own stdout
    with a short prefix so the operator can tell which scenario is
    talking. asyncio.subprocess gives us readable async streams; we
    fan-in stdout and stderr through one helper."""

    assert process.stdout is not None
    assert process.stderr is not None

    async def pump(stream, suffix: str) -> None:
        while True:
            line = await stream.readline()
            if not line:
                break
            text = line.decode("utf-8", errors="replace").rstrip("\n")
            print(f"[{label}{suffix}] {text}", flush=True)

    await asyncio.gather(
        pump(process.stdout, " "),
        pump(process.stderr, "!"),
    )


async def main_async(args: argparse.Namespace) -> int:
    processes: list[tuple[str, asyncio.subprocess.Process]] = []
    for label, script in SCENARIOS.items():
        cmd = build_command(script, args)
        print(f"[{label}*] launching: "
              f"{' '.join(repr(c) if ' ' in c else c for c in cmd)}",
              flush=True)
        process = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        processes.append((label, process))

    # Set up a stop trigger so ctrl-C cleanly terminates every child.
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop_event.set)
        except (NotImplementedError, RuntimeError):
            # Windows ProactorEventLoop: KeyboardInterrupt path still works.
            pass

    async def killer() -> None:
        await stop_event.wait()
        print("\n[ORCH*] ctrl-C; tearing down children ...", flush=True)
        for _label, proc in processes:
            if proc.returncode is None:
                try:
                    if os.name == "nt":
                        proc.terminate()
                    else:
                        proc.send_signal(signal.SIGINT)
                except ProcessLookupError:
                    pass

    tails = [asyncio.create_task(tail_child(label, proc))
             for label, proc in processes]
    waits = [asyncio.create_task(proc.wait()) for _, proc in processes]
    kill_task = asyncio.create_task(killer())

    # Wait for either: every child exited on its own (--duration), or
    # the operator hit ctrl-C and the killer task wound them down.
    await asyncio.gather(*waits, return_exceptions=True)
    stop_event.set()
    await kill_task
    for tail in tails:
        tail.cancel()
    await asyncio.gather(*tails, return_exceptions=True)

    failed = [label for label, proc in processes
              if proc.returncode not in (0, None)]
    if failed:
        print(f"[ORCH*] some scenarios exited non-zero: {failed}",
              file=sys.stderr)
        return 1
    print("[ORCH*] all scenarios finished cleanly", flush=True)
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tcp-host", default="127.0.0.1")
    parser.add_argument("--tcp-port", type=int, default=5555)
    parser.add_argument("--mqtt-broker", default="127.0.0.1")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--opcua-endpoint",
                        default="opc.tcp://127.0.0.1:4840")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-exit every child after N seconds.")
    parser.add_argument("--seed", type=int, default=None,
                        help="Propagated to every child for "
                             "reproducible runs.")
    parser.add_argument("--readonly", action="store_true",
                        help="Propagate --readonly to children that "
                             "support it (TCP, MQTT). OPC-UA is "
                             "already read-only.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return asyncio.run(main_async(args))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
