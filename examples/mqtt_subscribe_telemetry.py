#!/usr/bin/env python3
"""Tail the HMI's outbound MQTT telemetry.

Subscribes to `<prefix>/#` (default `industrial-hmi/#`) on a local
broker and prints every message the HMI publishes. Use this to verify
that:

  * the MQTT pill in the I/O panel is actually green
  * `ProductionTelemetryBridge` is producing the topics you expect
  * model-event payloads match what a downstream SCADA / Grafana /
    Kafka pipeline will receive

Requires `paho-mqtt` (`pip install -r examples/requirements.txt`).

Run alongside the HMI:

    Terminal 1:  mosquitto -p 1883
    Terminal 2:  ./build/with-backends/industrial-hmi
    Terminal 3:  python examples/mqtt_subscribe_telemetry.py
"""

import argparse
import signal
import sys
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("FAIL  paho-mqtt is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", default="127.0.0.1",
                        help="MQTT broker host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883,
                        help="MQTT broker port (default: 1883)")
    parser.add_argument("--prefix", default="industrial-hmi",
                        help="HMI publish prefix (matches "
                             "network.mqtt.topic_prefix; default: "
                             "industrial-hmi)")
    args = parser.parse_args()
    topic_filter = f"{args.prefix}/#"

    # Callback-driven API. paho-mqtt 2.x defaults to V2 callbacks; we
    # opt into V1 explicitly so the signatures stay stable regardless
    # of which point-release the user has installed.
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
        client_id="examples-mqtt-subscribe")

    def on_connect(_client, _userdata, _flags, rc):  # noqa: ANN001
        if rc == 0:
            print(f"OK    connected to {args.broker}:{args.port}")
            client.subscribe(topic_filter)
            print(f"      subscribed to {topic_filter!r} -- "
                  "ctrl+C to exit")
            print()
        else:
            print(f"FAIL  connect rc={rc}", file=sys.stderr)

    def on_message(_client, _userdata, msg):  # noqa: ANN001
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        payload = msg.payload.decode("utf-8", errors="replace")
        print(f"{ts}  {msg.topic:<48}  {payload}")

    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(args.broker, args.port, keepalive=30)
    except OSError as exc:
        print(f"FAIL  Could not reach broker "
              f"{args.broker}:{args.port}: {exc}", file=sys.stderr)
        print("      Is mosquitto running locally? "
              "`mosquitto -p 1883`", file=sys.stderr)
        return 1

    # paho's loop_forever() handles ctrl+C internally, but we still
    # install a signal handler so the goodbye line prints cleanly.
    def on_sigint(_signum, _frame):  # noqa: ANN001
        print("\nDONE  disconnecting")
        client.disconnect()

    signal.signal(signal.SIGINT, on_sigint)
    client.loop_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
