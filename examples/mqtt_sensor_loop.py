#!/usr/bin/env python3
"""Live scenario: a continuous stream of sensor events on MQTT.

Publishes `on` / `off` messages on
`<sensor_prefix>/equipment/<id>/state` at a configurable cadence,
cycling across the equipment slots so each one toggles roughly
evenly. Drives `SensorIngestBridge` the way a flaky field sensor or
a maintenance script would in production: not a single message,
but a steady drumbeat.

Useful for:

  * stress-testing `SensorIngestBridge` under realistic continuous
    load (does it leak, does it lock the model, does it drop?)
  * customer demos where the dashboard's A/B/C-LINE switches must
    keep flipping while the operator watches.
  * pairing with `tcp_operator_session.py` and `opcua_dashboard.py`
    in the `factory_simulation.py` orchestrator to populate three
    backends at once.

Requires `paho-mqtt`. Honours the shared `--interval`, `--duration`,
`--seed`, `--readonly` flags documented in `examples/README.md`.
"""

from __future__ import annotations

import argparse
import random
import signal
import sys
import time
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("FAIL  paho-mqtt is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


DEFAULT_EQUIPMENT_COUNT = 3


def now_label() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", default="127.0.0.1",
                        help="MQTT broker host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883,
                        help="MQTT broker port (default: 1883)")
    parser.add_argument("--prefix", default="industrial-hmi-sensors",
                        help="Sensor topic prefix (matches "
                             "network.mqtt.subscriber.topic_prefix; "
                             "default: industrial-hmi-sensors)")
    parser.add_argument("--equipment-count", type=int,
                        default=DEFAULT_EQUIPMENT_COUNT,
                        help="How many equipment slots to cycle "
                             "across (default: 3)")
    parser.add_argument("--interval", type=float, default=5.0,
                        help="Seconds between publishes (default: 5)")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-exit after N seconds. "
                             "Default: run until ctrl-C.")
    parser.add_argument("--seed", type=int, default=None,
                        help="Deterministic random seed.")
    parser.add_argument("--readonly", action="store_true",
                        help="Connect to the broker but do NOT "
                             "publish anything. Useful to confirm "
                             "the broker is reachable without "
                             "moving the dashboard.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.seed)

    # paho-mqtt 2.x exposes a V1/V2 callback toggle; we opt in to V1
    # explicitly so this script works across paho-mqtt minor versions.
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
        client_id="examples-mqtt-sensor-loop")

    try:
        client.connect(args.broker, args.port, keepalive=30)
    except OSError as exc:
        print(f"FAIL  Could not reach broker {args.broker}:{args.port}: "
              f"{exc}", file=sys.stderr)
        print("      Is mosquitto running? `mosquitto -p 1883`",
              file=sys.stderr)
        return 1

    client.loop_start()
    print(f"OK    connected to {args.broker}:{args.port}")
    print(f"      readonly={args.readonly}, "
          f"duration={args.duration}, seed={args.seed}, "
          f"interval={args.interval}s")
    print(f"      ctrl-C to stop\n")

    stop = False

    def on_sigint(_signum, _frame):  # noqa: ANN001
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, on_sigint)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, on_sigint)

    # Track the latest state per slot locally so the next pick
    # alternates and the dashboard actually shows movement.
    state = [True] * args.equipment_count
    start_ts = time.monotonic()
    next_fire = start_ts

    try:
        while not stop:
            now = time.monotonic()
            if args.duration is not None and (now - start_ts) >= args.duration:
                print(f"\nDONE  --duration reached "
                      f"({args.duration:.0f}s)")
                break
            if now < next_fire:
                # Spin lightly -- we want responsive ctrl-C without
                # paying for asyncio just for one publisher.
                time.sleep(0.1)
                continue
            next_fire = now + args.interval

            eq_id = rng.randrange(args.equipment_count)
            state[eq_id] = not state[eq_id]
            payload = "on" if state[eq_id] else "off"
            topic = f"{args.prefix}/equipment/{eq_id}/state"

            if args.readonly:
                print(f"{now_label()}  [dry-run] would publish "
                      f"{payload!r} -> {topic}")
                continue

            info = client.publish(topic, payload, qos=0)
            # qos=0 publish() returns immediately; we still wait
            # briefly so paho's worker has a chance to flush before
            # we move on, which makes log ordering predictable.
            info.wait_for_publish(timeout=1.0)
            print(f"{now_label()}  -> {topic:<55}  {payload}")
    finally:
        client.loop_stop()
        client.disconnect()
        print("DONE  disconnected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
