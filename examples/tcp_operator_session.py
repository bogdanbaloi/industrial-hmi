#!/usr/bin/env python3
"""Live scenario: a long-running TCP operator session.

Holds a TCP socket open against the HMI's `network.tcp` server and
behaves like an attentive operator on the shop floor:

  * polls `status` every `--interval-status` seconds (default 5)
    so the connection stays warm and the operator has a steady
    snapshot of the line.
  * toggles a random equipment every `--interval-toggle` seconds
    (default 30) so the SYSTEM badge + equipment switches keep
    moving on the dashboard.
  * restarts production every `--interval-restart` seconds
    (default 120) -- `production stop` followed by `production
    start` -- so the run loop cycles too.

The TCP pill in the I/O panel stays green CONNECTED for the
entire session, which is exactly what a customer wants to see
during a live demo.

Honours the shared `--duration`, `--seed`, `--readonly` flags
documented in `examples/README.md`. Stdlib only -- no
`pip install` required.

Run alongside the HMI:

    Terminal 1:  ./build/with-backends/industrial-hmi
    Terminal 2:  python examples/tcp_operator_session.py
"""

from __future__ import annotations

import argparse
import random
import socket
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Optional


# Equipment slots SimulatedModel exposes by default. Override on the
# CLI if a deployment ships a different count.
DEFAULT_EQUIPMENT_COUNT = 3


@dataclass
class Schedule:
    """Per-action cadence + the deadline of the next firing. We tick
    the loop at a fixed wakeup interval and let each action check
    whether its own next-fire deadline has passed -- avoids juggling
    threads or asyncio for a script this small."""

    every_s: float
    next_fire_s: float


def now_label() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def send_command(sock: socket.socket, cmd: str) -> str:
    """Write one MQTT-line-style command + read whatever the server
    sends back within a short read timeout. The HMI replies are
    typically one short line per command (`OK`, `BYE`, or a JSON
    snapshot for `status`), so a 1 s timeout is plenty."""

    sock.sendall((cmd + "\n").encode("ascii"))
    sock.settimeout(1.0)
    try:
        reply = sock.recv(4096).decode("utf-8", errors="replace").strip()
    except socket.timeout:
        reply = "(no reply within 1s)"
    return reply


def restart_production(sock: socket.socket) -> None:
    print(f"{now_label()}  >>> production stop / start (cycle)")
    print(f"                  {send_command(sock, 'production stop')}")
    # Tiny pause so the dashboard's IDLE state is visible to the
    # human watching before we go back to RUNNING.
    time.sleep(2.0)
    print(f"                  {send_command(sock, 'production start')}")


def toggle_equipment(sock: socket.socket,
                     rng: random.Random,
                     equipment_count: int,
                     enabled: list[bool]) -> None:
    """Flip a random equipment slot's enabled bit and tell the HMI.
    We track the bit locally so the next pick can flip away from it
    (otherwise back-to-back picks of the same slot would no-op)."""

    eq_id = rng.randrange(equipment_count)
    enabled[eq_id] = not enabled[eq_id]
    direction = "on" if enabled[eq_id] else "off"
    cmd = f"eq {eq_id} {direction}"
    print(f"{now_label()}  >>> {cmd}")
    print(f"                  {send_command(sock, cmd)}")


def poll_status(sock: socket.socket) -> None:
    print(f"{now_label()}  ... status")
    print(f"                  {send_command(sock, 'status')}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1",
                        help="HMI TCP backend host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=5555,
                        help="HMI TCP backend port (default: 5555)")
    parser.add_argument("--equipment-count", type=int,
                        default=DEFAULT_EQUIPMENT_COUNT,
                        help="Number of equipment slots the model "
                             "exposes (default: 3)")
    parser.add_argument("--interval-status", type=float, default=5.0,
                        help="Seconds between `status` polls (default: 5)")
    parser.add_argument("--interval-toggle", type=float, default=30.0,
                        help="Seconds between equipment toggles "
                             "(default: 30)")
    parser.add_argument("--interval-restart", type=float, default=120.0,
                        help="Seconds between production stop/start "
                             "cycles (default: 120)")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-exit after N seconds. "
                             "Default: run until ctrl-C.")
    parser.add_argument("--seed", type=int, default=None,
                        help="Deterministic random seed for "
                             "reproducible scenarios "
                             "(screenshots / bug reports).")
    parser.add_argument("--readonly", action="store_true",
                        help="Only poll `status`; do NOT toggle "
                             "equipment or restart production.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.seed)

    try:
        sock = socket.create_connection((args.host, args.port),
                                         timeout=5.0)
    except OSError as exc:
        print(f"FAIL  Could not reach {args.host}:{args.port}: {exc}",
              file=sys.stderr)
        print("      Is the HMI running with "
              "network.tcp.enabled = true?",
              file=sys.stderr)
        return 1

    print(f"OK    connected to {args.host}:{args.port}")
    print(f"      readonly={args.readonly}, "
          f"duration={args.duration}, seed={args.seed}")
    print(f"      ctrl-C to stop\n")

    start_ts = time.monotonic()
    schedules = {
        "status":  Schedule(args.interval_status,  args.interval_status),
        "toggle":  Schedule(args.interval_toggle,  args.interval_toggle),
        "restart": Schedule(args.interval_restart, args.interval_restart),
    }
    # Track equipment state locally so toggle picks the opposite
    # direction next time. We assume the server starts each slot
    # enabled (matches SimulatedModel's defaults for A/B-LINE; C
    # starts offline, but it converges after the first toggle).
    enabled = [True] * args.equipment_count

    try:
        while True:
            elapsed = time.monotonic() - start_ts
            if args.duration is not None and elapsed >= args.duration:
                print(f"\nDONE  --duration reached ({args.duration:.0f}s)")
                break

            if elapsed >= schedules["status"].next_fire_s:
                poll_status(sock)
                schedules["status"].next_fire_s = (
                    elapsed + schedules["status"].every_s)

            if (not args.readonly
                    and elapsed >= schedules["toggle"].next_fire_s):
                toggle_equipment(sock, rng, args.equipment_count, enabled)
                schedules["toggle"].next_fire_s = (
                    elapsed + schedules["toggle"].every_s)

            if (not args.readonly
                    and elapsed >= schedules["restart"].next_fire_s):
                restart_production(sock)
                schedules["restart"].next_fire_s = (
                    elapsed + schedules["restart"].every_s)

            time.sleep(0.2)
    except KeyboardInterrupt:
        print("\nDONE  ctrl-C")

    sock.sendall(b"quit\n")
    try:
        sock.settimeout(1.0)
        _ = sock.recv(1024)
    except socket.timeout:
        pass
    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
