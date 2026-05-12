#!/usr/bin/env python3
"""Subscribe to live equipment status changes via OPC-UA.

Demonstrates the inbound direction from a customer's perspective:
their SCADA / data historian dials the HMI server, monitors the
`Factory/EquipmentLines/Line<id>/Status` nodes, and prints every
notification the HMI publishes when the simulator (or a connected
TCP / MQTT actor) flips a status value.

Requires `asyncua` (`pip install -r examples/requirements.txt`).

Run alongside the HMI:

    Terminal 1:  ./build/with-backends/industrial-hmi
    Terminal 2:  python examples/opcua_subscribe_equipment.py
    Terminal 3:  (optional) python examples/tcp_control.py
                 -- watch this terminal show the changes in real time
"""

import argparse
import asyncio
import signal
import sys
from datetime import datetime

try:
    from asyncua import Client, ua
except ImportError:
    print("FAIL  asyncua is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


# Default monitored items -- the equipment status nodes our server
# publishes. Override on the command line if a deployment renames
# them.
DEFAULT_PATHS = [
    "Factory/EquipmentLines/Line0/Status",
    "Factory/EquipmentLines/Line1/Status",
    "Factory/EquipmentLines/Line2/Status",
]

NAMESPACE_INDEX = 1  # ns=1 in our address space


class NotificationPrinter:
    """asyncua delivers data-change callbacks through a handler
    object; we just print every notification with a timestamp + the
    originating browse path."""

    def __init__(self, label_for) -> None:
        self._label_for = label_for

    def datachange_notification(self, node, val, _data) -> None:
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        label = self._label_for(node)
        print(f"{ts}  {label:<48}  -> {val}")


async def run(endpoint: str, paths: list[str]) -> int:
    print(f"      dialing {endpoint} ...")
    async with Client(endpoint) as client:
        print("OK    session established")

        # Resolve each browse path under Objects/ to a Node handle.
        nodes = []
        labels = {}
        for path in paths:
            qualified = [f"{NAMESPACE_INDEX}:{name}"
                         for name in path.split("/")]
            try:
                node = await client.nodes.objects.get_child(qualified)
                nodes.append(node)
                labels[node.nodeid] = path
            except ua.UaStatusCodeError as exc:
                print(f"WARN  could not resolve {path}: {exc}",
                      file=sys.stderr)

        if not nodes:
            print("FAIL  no monitored items resolved", file=sys.stderr)
            return 1

        handler = NotificationPrinter(
            label_for=lambda n: labels.get(n.nodeid, str(n.nodeid)))
        subscription = await client.create_subscription(200, handler)
        await subscription.subscribe_data_change(nodes)

        print(f"      subscribed to {len(nodes)} node(s); "
              "ctrl+C to exit\n")

        # Hang on the loop until interrupted. asyncua handles the
        # Publish responses in the background; our handler fires.
        stop_event = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, stop_event.set)
            except NotImplementedError:
                # Windows doesn't support add_signal_handler on the
                # ProactorEventLoop; KeyboardInterrupt still works.
                pass
        try:
            await stop_event.wait()
        except KeyboardInterrupt:
            pass

        print("\nDONE  unsubscribing")
        await subscription.delete()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint",
                        default="opc.tcp://127.0.0.1:4840",
                        help="OPC-UA endpoint URL")
    parser.add_argument("--paths", nargs="*", default=DEFAULT_PATHS,
                        help="Browse paths to monitor "
                             "(default: the three Equipment lines)")
    args = parser.parse_args()
    try:
        return asyncio.run(run(args.endpoint, args.paths))
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL  {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
