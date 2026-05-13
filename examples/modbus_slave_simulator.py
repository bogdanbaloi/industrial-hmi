#!/usr/bin/env python3
"""Live scenario: a Modbus TCP slave that toggles equipment registers.

Hosts a small Modbus/TCP server on 127.0.0.1:5020 (the unprivileged
default the HMI's app-config.json points at). Exposes one holding
register per equipment slot: address `base + i` mirrors equipment
slot `i`'s enabled bit (0 = OFF, non-zero = ON).

While the server is up, the script periodically flips registers --
one slot every `--interval` seconds, round-robin -- so the HMI's
poll loop sees the values change. The dashboard's A/B/C-LINE
switches react accordingly: the bridge translates `register != 0`
into `setEquipmentEnabled(id, true)` and pushes through MVP into
the UI.

Useful for:

  * end-to-end demos: one command shows the Modbus pill go green
    in the I/O panel and equipment lights flip in lockstep.
  * stress testing ModbusPollLoop / ModbusIngestBridge against a
    steady stream of register changes.
  * pairing with the other live scenarios via
    `factory_simulation.py` so a single orchestrator drives four
    protocols (TCP + MQTT + OPC-UA + Modbus) simultaneously.

Requires `pymodbus`. Honours the shared `--interval`, `--duration`,
`--seed`, `--readonly` flags documented in `examples/README.md`.
"""

from __future__ import annotations

import argparse
import asyncio
import math
import random
import signal
import sys
from datetime import datetime

try:
    from pymodbus.datastore import (ModbusSequentialDataBlock,
                                    ModbusServerContext)
    # pymodbus 3.7 renamed ModbusSlaveContext -> ModbusDeviceContext.
    # Accept either so installations within the supported version
    # window (3.6 up to current 3.x) keep working.
    try:
        from pymodbus.datastore import ModbusDeviceContext as _SlaveContext
    except ImportError:                                         # pragma: no cover
        from pymodbus.datastore import ModbusSlaveContext as _SlaveContext
    from pymodbus.server import StartAsyncTcpServer
except ImportError:
    print("FAIL  pymodbus is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


DEFAULT_EQUIPMENT_COUNT = 3
DEFAULT_BASE_ADDRESS = 0
DEFAULT_INTERVAL_S = 2.0

# Analog block defaults -- match
# `src/config/config_defaults.h::kModbus{Supply,Quality}BaseAddress`
# so the simulator and the HMI agree by default.
DEFAULT_SUPPLY_BASE_ADDRESS = 0x10
DEFAULT_SUPPLY_SCALE        = 1.0   # raw IS percent
DEFAULT_QUALITY_BASE_ADDRESS = 0x20
DEFAULT_QUALITY_SCALE       = 0.1   # raw 987 -> 98.7%
DEFAULT_QUALITY_COUNT       = 3


def now_label() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def build_server_context(args: argparse.Namespace) -> ModbusServerContext:
    """Build a slave context covering every block the HMI expects:
    - boolean block at `base_address` (EquipmentEnabled, N regs)
    - analog supply block at `supply_base_address` (SupplyLevel, N regs)
    - analog quality block at `quality_base_address` (PassRate, M regs)

    A single contiguous data block backs all three because pymodbus's
    ModbusSequential backing store is one flat array. We size it to
    cover the highest address any block touches; intermediate
    addresses are kept at zero so a curious read on an unmapped
    register returns sane data instead of failing.

    Block size + 1: pymodbus 3.7 ModbusSequentialDataBlock has an
    off-by-one quirk where the last index of a `[0] * N` block is
    not readable through getValues (returns []). Padding by one
    makes the full intended range usable without leaking the
    one-extra slot into the wire surface, since the togglers only
    touch the documented address ranges."""
    highest = max(
        args.base_address + args.equipment_count,
        args.supply_base_address + args.equipment_count,
        args.quality_base_address + args.quality_count,
    )
    block = ModbusSequentialDataBlock(0, [0] * (highest + 1))
    # The HMI uses unit id 1 by default. Mapping a single device
    # under id 1 matches that contract cleanly. The constructor
    # kwarg renamed from `slaves` to `devices` in pymodbus 3.7;
    # try the new name first, fall back for older installs.
    slave = _SlaveContext(hr=block)
    try:
        return ModbusServerContext(devices={1: slave}, single=False)
    except TypeError:                                           # pragma: no cover
        return ModbusServerContext(slaves={1: slave}, single=False)


async def toggler(context: ModbusServerContext, args: argparse.Namespace,
                  stop_event: asyncio.Event) -> None:
    """Round-robin OFF -> ON -> OFF -> ... on each equipment slot.
    Visible signal in the dashboard: the equipment switches keep
    flipping. The HMI's poll loop runs at the configured cadence;
    the toggler runs at `--interval` so the change rate is
    independent of the HMI's poll rate."""

    rng = random.Random(args.seed) if args.seed is not None else random.Random()
    slot = 0
    print(f"[{now_label()}] start  unit=1  base={args.base_address}  "
          f"slots={args.equipment_count}  interval={args.interval}s")

    try:
        while not stop_event.is_set():
            if args.readonly:
                # Just observe -- don't write. Useful when an auditor wants
                # to confirm the HMI behaves without external mutation.
                await asyncio.sleep(args.interval)
                continue

            address = args.base_address + slot
            # Read current value, flip, write back. Going through the
            # context APIs keeps the on-the-wire representation honest
            # (pymodbus internally serialises writes the way a real slave
            # would publish them on any subsequent read).
            slave = context[1]
            # FC03/FC04 holding-register code is 3 on the context API;
            # addresses are passed 0-based here regardless of the wire
            # representation pymodbus emits.
            current = slave.getValues(3, address, count=1)[0]
            new_value = 0 if current else (rng.randint(1, 0xFFFF))
            slave.setValues(3, address, [new_value])

            state = "ON " if new_value else "OFF"
            print(f"[{now_label()}] slot {slot} addr 0x{address:04x} "
                  f"{current:#06x} -> {new_value:#06x}  ({state})")

            slot = (slot + 1) % args.equipment_count
            try:
                await asyncio.wait_for(stop_event.wait(), args.interval)
            except asyncio.TimeoutError:
                pass
    except Exception as exc:                                    # noqa: BLE001
        # Surface any toggler crash explicitly so a silent task
        # cancellation doesn't masquerade as a clean shutdown.
        print(f"[{now_label()}] toggler crashed: {type(exc).__name__}: {exc}",
              file=sys.stderr)
        stop_event.set()
        raise


async def analog_driver(context: ModbusServerContext,
                        args: argparse.Namespace,
                        stop_event: asyncio.Event) -> None:
    """Drive the analog blocks (SupplyLevel + PassRate) with smooth
    sweeps so the dashboard renders moving bars instead of step
    changes. Supply walks a triangular pattern 30..100%; pass rate
    drifts within 92..99% at the configured scale (default 0.1 ->
    raw 920..990).

    Runs at half the boolean toggler's cadence so the dashboard
    receives a couple of analog samples per visible-state flip."""
    if args.readonly:
        # Auditor mode -- analog block stays at zero so the
        # dashboard shows the baseline reading, matching the
        # boolean toggler's --readonly behaviour above.
        return

    interval = max(args.interval / 2.0, 0.1)
    print(f"[{now_label()}] analog start  "
          f"supply_base=0x{args.supply_base_address:04x}  "
          f"quality_base=0x{args.quality_base_address:04x}  "
          f"interval={interval:.2f}s")

    slave = context[1]
    step = 0
    try:
        while not stop_event.is_set():
            # Supply: triangular sweep 30 -> 100 -> 30 per cycle.
            #   scale 1.0  => write raw percent (e.g. 73)
            #   scale 0.1  => write raw 730 to represent 73.0%
            phase = math.sin(step / 8.0) * 0.5 + 0.5   # 0..1
            supply_pct = 30 + int(phase * 70)          # 30..100
            supply_raw = int(round(supply_pct / args.supply_scale))

            # Quality: tight band 92..99% so the dashboard's level
            # bar nudges visibly without driving alarms.
            quality_pct = 92.0 + math.cos(step / 11.0) * 3.5
            quality_raw = int(round(quality_pct / args.quality_scale))

            for i in range(args.equipment_count):
                slave.setValues(3, args.supply_base_address + i,
                                [supply_raw])
            for i in range(args.quality_count):
                slave.setValues(3, args.quality_base_address + i,
                                [quality_raw])

            print(f"[{now_label()}] analog step {step:3d}  "
                  f"supply={supply_pct}%  quality={quality_pct:.1f}%  "
                  f"(raw {supply_raw}/{quality_raw})")
            step += 1
            try:
                await asyncio.wait_for(stop_event.wait(), interval)
            except asyncio.TimeoutError:
                pass
    except Exception as exc:                                    # noqa: BLE001
        print(f"[{now_label()}] analog driver crashed: "
              f"{type(exc).__name__}: {exc}", file=sys.stderr)
        stop_event.set()
        raise


async def duration_watchdog(stop_event: asyncio.Event,
                            duration_s: float | None) -> None:
    """Auto-stop after `--duration` seconds. None == run forever."""
    if duration_s is None:
        return
    try:
        await asyncio.wait_for(stop_event.wait(), duration_s)
    except asyncio.TimeoutError:
        print(f"[{now_label()}] duration {duration_s}s reached -- stopping")
        stop_event.set()


async def main_async(args: argparse.Namespace) -> int:
    context = build_server_context(args)
    stop_event = asyncio.Event()

    # Hook ctrl-C cleanly. On Windows the proactor event loop doesn't
    # support add_signal_handler; fall back to KeyboardInterrupt
    # propagation (handled by the caller).
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop_event.set)
        except (NotImplementedError, RuntimeError):
            pass

    server_task = asyncio.create_task(
        StartAsyncTcpServer(context, address=(args.host, args.port)))
    toggler_task = asyncio.create_task(toggler(context, args, stop_event))
    analog_task = asyncio.create_task(
        analog_driver(context, args, stop_event))
    watchdog_task = asyncio.create_task(
        duration_watchdog(stop_event, args.duration))

    # Wait on the work tasks -- not the server. In modern pymodbus,
    # StartAsyncTcpServer may return early after the listener is up
    # rather than blocking until cancellation; observing
    # FIRST_COMPLETED on it would shut everything down immediately.
    # The toggler / analog driver / watchdog drive lifetime instead.
    await asyncio.wait(
        {toggler_task, analog_task, watchdog_task},
        return_when=asyncio.FIRST_COMPLETED,
    )

    # Signal everyone to wind down; cancel anything still pending.
    stop_event.set()
    for task in (server_task, toggler_task, analog_task, watchdog_task):
        if not task.done():
            task.cancel()
    await asyncio.gather(server_task, toggler_task, analog_task,
                         watchdog_task, return_exceptions=True)
    print(f"[{now_label()}] stopped cleanly")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1",
                        help="Bind address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=5020,
                        help="Bind port (default: 5020 -- "
                             "unprivileged, matches HMI config)")
    parser.add_argument("--equipment-count", type=int,
                        default=DEFAULT_EQUIPMENT_COUNT,
                        help="Number of equipment slots / registers "
                             "to host (default: 3)")
    parser.add_argument("--base-address", type=int,
                        default=DEFAULT_BASE_ADDRESS,
                        help="First holding-register address for "
                             "the EquipmentEnabled block (default: 0)")
    parser.add_argument("--supply-base-address", type=int,
                        default=DEFAULT_SUPPLY_BASE_ADDRESS,
                        help="First holding-register address for "
                             "the EquipmentSupplyLevel block "
                             f"(default: 0x{DEFAULT_SUPPLY_BASE_ADDRESS:x})")
    parser.add_argument("--supply-scale", type=float,
                        default=DEFAULT_SUPPLY_SCALE,
                        help="Multiplier applied to supply percent "
                             "before writing the raw register "
                             f"(default: {DEFAULT_SUPPLY_SCALE})")
    parser.add_argument("--quality-base-address", type=int,
                        default=DEFAULT_QUALITY_BASE_ADDRESS,
                        help="First holding-register address for "
                             "the QualityPassRate block "
                             f"(default: 0x{DEFAULT_QUALITY_BASE_ADDRESS:x})")
    parser.add_argument("--quality-scale", type=float,
                        default=DEFAULT_QUALITY_SCALE,
                        help="Multiplier applied to quality percent "
                             "before writing the raw register "
                             f"(default: {DEFAULT_QUALITY_SCALE})")
    parser.add_argument("--quality-count", type=int,
                        default=DEFAULT_QUALITY_COUNT,
                        help="Number of quality checkpoints / "
                             "passRate registers (default: 3)")
    parser.add_argument("--interval", type=float,
                        default=DEFAULT_INTERVAL_S,
                        help="Seconds between register flips "
                             "(default: 2.0)")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-stop after N seconds. None == "
                             "run until ctrl-C")
    parser.add_argument("--seed", type=int, default=None,
                        help="RNG seed for ON values (deterministic "
                             "runs)")
    parser.add_argument("--readonly", action="store_true",
                        help="Host registers but never flip them. "
                             "Useful for auditors who want to watch "
                             "the HMI without external mutation.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return asyncio.run(main_async(args))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
