#!/usr/bin/env python3
"""Drive the HMI's equipment switches via a sensor MQTT publish.

`SensorIngestBridge` subscribes to
`<sensor_prefix>/equipment/<id>/state` (default
`industrial-hmi-sensors`). Publishing `on` / `off` on one of those
topics flips the corresponding A/B/C-LINE switch on the dashboard --
the same way an external sensor or supervisory PLC would drive the
HMI.

Requires `paho-mqtt` (`pip install -r examples/requirements.txt`).

Examples:

    # Disable A-LINE
    python examples/mqtt_publish_sensor.py 0 off

    # Re-enable B-LINE
    python examples/mqtt_publish_sensor.py 1 on

    # Custom broker (e.g. running on another host)
    python examples/mqtt_publish_sensor.py 0 off --broker 10.0.0.5
"""

import argparse
import sys

try:
    import paho.mqtt.publish as publish
except ImportError:
    print("FAIL  paho-mqtt is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("equipment_id", type=int,
                        help="Equipment slot 0..N-1 (A-LINE=0, "
                             "B-LINE=1, C-LINE=2 in the default config)")
    parser.add_argument("state", choices=["on", "off", "1", "0",
                                          "true", "false"],
                        help="New enabled state for that equipment")
    parser.add_argument("--broker", default="127.0.0.1",
                        help="MQTT broker host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883,
                        help="MQTT broker port (default: 1883)")
    parser.add_argument("--prefix", default="industrial-hmi-sensors",
                        help="Sensor topic prefix (matches "
                             "network.mqtt.subscriber.topic_prefix; "
                             "default: industrial-hmi-sensors)")
    args = parser.parse_args()

    topic = f"{args.prefix}/equipment/{args.equipment_id}/state"
    try:
        publish.single(topic, args.state,
                       hostname=args.broker, port=args.port,
                       client_id="examples-mqtt-publish-sensor")
    except (ConnectionRefusedError, OSError) as exc:
        print(f"FAIL  Could not reach broker "
              f"{args.broker}:{args.port}: {exc}", file=sys.stderr)
        print("      Is mosquitto running? `mosquitto -p 1883`",
              file=sys.stderr)
        return 1

    print(f"OK    published {args.state!r} to {topic!r}")
    print("      Watch the dashboard -- the matching equipment "
          "switch should flip in <1s.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
