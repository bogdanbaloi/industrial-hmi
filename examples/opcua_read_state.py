#!/usr/bin/env python3
"""Browse + read core nodes from the HMI's OPC-UA server.

Connects to `opc.tcp://127.0.0.1:4840` (the default
`network.opcua.port`), walks the `Factory/` address space, and prints
the current value of every node `FactoryNodeMap` populates. Useful
for verifying that:

  * the OPC-UA pill in the I/O panel goes green CONNECTED while this
    script holds a session
  * the address space matches the README documentation
  * the values mirror what the dashboard shows

Requires `asyncua` (`pip install -r examples/requirements.txt`).

Run alongside the HMI:

    Terminal 1:  ./build/with-backends/industrial-hmi
    Terminal 2:  python examples/opcua_read_state.py
"""

import argparse
import asyncio
import sys

try:
    from asyncua import Client
except ImportError:
    print("FAIL  asyncua is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    print("      On Windows MSYS2 this build is painful; "
          "run from WSL or Linux/Mac.", file=sys.stderr)
    sys.exit(1)


# Nodes we expect FactoryNodeMap to publish. The browse path is
# slash-separated under Objects/; we walk each node by name in the
# application namespace (ns=1).
TARGETS = [
    ["Factory", "State"],
    ["Factory", "EquipmentLines", "Line0", "Status"],
    ["Factory", "EquipmentLines", "Line0", "SupplyLevel"],
    ["Factory", "EquipmentLines", "Line1", "Status"],
    ["Factory", "EquipmentLines", "Line1", "SupplyLevel"],
    ["Factory", "EquipmentLines", "Line2", "Status"],
    ["Factory", "QualityCheckpoints", "Checkpoint0", "Status"],
    ["Factory", "QualityCheckpoints", "Checkpoint0", "PassRate"],
    ["Factory", "WorkUnit", "CompletedOperations"],
    ["Factory", "WorkUnit", "TotalOperations"],
]

NAMESPACE_INDEX = 1  # `kApplicationNamespace` in Open62541Server.cpp


async def fetch_all(endpoint: str) -> int:
    print(f"      dialing {endpoint} ...")
    async with Client(endpoint) as client:
        print("OK    session established -- OPC-UA pill should be "
              "green CONNECTED on the dashboard now\n")

        max_width = max(len("/".join(t)) for t in TARGETS)
        for path in TARGETS:
            # Resolve under Objects/ using qualified names. ns=1 for
            # every segment because our server registers them in the
            # application namespace.
            qualified = [f"{NAMESPACE_INDEX}:{name}" for name in path]
            try:
                node = await client.nodes.objects.get_child(qualified)
                value = await node.read_value()
            except Exception as exc:  # noqa: BLE001
                value = f"<error: {exc}>"
            label = "/".join(path)
            print(f"  {label:<{max_width}}  {value}")

        print("\nDONE  closing session in 3s ...")
        await asyncio.sleep(3)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint",
                        default="opc.tcp://127.0.0.1:4840",
                        help="OPC-UA endpoint URL "
                             "(default: opc.tcp://127.0.0.1:4840)")
    args = parser.parse_args()
    try:
        return asyncio.run(fetch_all(args.endpoint))
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL  {exc}", file=sys.stderr)
        print("      Is the HMI running with "
              "network.opcua.enabled = true?", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
