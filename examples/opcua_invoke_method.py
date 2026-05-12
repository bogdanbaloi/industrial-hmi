#!/usr/bin/env python3
"""Invoke an OPC-UA method or flip a writable variable on the HMI.

Quick check for the inbound control surface FactoryNodeMap exposes
when `network.opcua.server.commands_enabled = true`. Drives the
HMI's `ProductionModel` through a standard OPC-UA client -- the
same wire a real SCADA / DCS would use.

Two sub-commands:

  call    Invoke a parameterless OPC-UA method under
          `Objects/Factory/Commands/`. Default: StartProduction.

  set     Write a Boolean value to a writable variable. Default:
          flip `Factory/EquipmentLines/Line<id>/Enabled` for the
          chosen equipment id.

Requires `asyncua` (`pip install -r examples/requirements.txt`).

Examples:

  python examples/opcua_invoke_method.py call StartProduction
  python examples/opcua_invoke_method.py call StopProduction
  python examples/opcua_invoke_method.py set 0 off
  python examples/opcua_invoke_method.py set 2 on
"""

from __future__ import annotations

import argparse
import asyncio
import sys

try:
    from asyncua import Client, ua
except ImportError:
    print("FAIL  asyncua is not installed.", file=sys.stderr)
    print("      pip install -r examples/requirements.txt",
          file=sys.stderr)
    sys.exit(1)


NAMESPACE_INDEX = 1  # ns=1 matches Open62541Server's application namespace

# Method names FactoryCommandSink routes on. Keep aligned with the
# C++ side -- a typo would surface as BadNoMatch on the wire.
KNOWN_COMMANDS = {
    "StartProduction",
    "StopProduction",
    "ResetSystem",
    "StartCalibration",
}


async def call_method(endpoint: str, command: str) -> int:
    if command not in KNOWN_COMMANDS:
        print(f"WARN  '{command}' is not a built-in command. "
              f"Sending it anyway -- the server will likely "
              f"return BadNoMatch.", file=sys.stderr)

    async with Client(endpoint) as client:
        objects = client.nodes.objects
        parent = await objects.get_child([f"{NAMESPACE_INDEX}:Factory",
                                          f"{NAMESPACE_INDEX}:Commands"])
        method = await parent.get_child(f"{NAMESPACE_INDEX}:{command}")
        try:
            await parent.call_method(method)
        except ua.UaStatusCodeError as exc:
            print(f"FAIL  method call rejected: {exc}",
                  file=sys.stderr)
            return 1
        print(f"OK    invoked Factory/Commands/{command}")
        print("      The dashboard's SYSTEM badge or "
              "calibration state should flip accordingly.")
    return 0


async def write_enabled(endpoint: str,
                        equipment_id: int, enabled: bool) -> int:
    async with Client(endpoint) as client:
        objects = client.nodes.objects
        node = await objects.get_child([
            f"{NAMESPACE_INDEX}:Factory",
            f"{NAMESPACE_INDEX}:EquipmentLines",
            f"{NAMESPACE_INDEX}:Line{equipment_id}",
            f"{NAMESPACE_INDEX}:Enabled",
        ])
        # asyncua's DataValue defaults timestamps + status to None /
        # Good on construction (verified empirically), and the server
        # now grants STATUSWRITE + TIMESTAMPWRITE in addition to
        # READ|WRITE on writable Bool variables. Plain construction
        # is enough.
        data_value = ua.DataValue(
            ua.Variant(enabled, ua.VariantType.Boolean))
        try:
            await node.write_attribute(ua.AttributeIds.Value, data_value)
        except ua.UaStatusCodeError as exc:
            print(f"FAIL  write rejected: {exc}", file=sys.stderr)
            return 1
        label = "on" if enabled else "off"
        print(f"OK    wrote {label} to "
              f"Factory/EquipmentLines/Line{equipment_id}/Enabled")
        print(f"      The matching equipment switch on the dashboard "
              f"should flip {label.upper()}.")
    return 0


def parse_bool(raw: str) -> bool:
    if raw.lower() in {"on", "1", "true"}:
        return True
    if raw.lower() in {"off", "0", "false"}:
        return False
    raise argparse.ArgumentTypeError(
        f"invalid bool {raw!r}: expected on/off/1/0/true/false")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint",
                        default="opc.tcp://127.0.0.1:4840",
                        help="OPC-UA endpoint URL "
                             "(default: opc.tcp://127.0.0.1:4840)")
    subparsers = parser.add_subparsers(dest="action", required=True)

    call_parser = subparsers.add_parser(
        "call", help="Invoke a Factory/Commands/* method")
    call_parser.add_argument(
        "command", choices=sorted(KNOWN_COMMANDS),
        help="Command name to invoke")

    set_parser = subparsers.add_parser(
        "set",
        help="Flip a Factory/EquipmentLines/Line<id>/Enabled bool")
    set_parser.add_argument("equipment_id", type=int,
                            help="Equipment slot 0..N-1")
    set_parser.add_argument("state", type=parse_bool,
                            help="on/off/1/0/true/false")

    args = parser.parse_args()
    try:
        if args.action == "call":
            return asyncio.run(call_method(args.endpoint, args.command))
        return asyncio.run(write_enabled(args.endpoint,
                                          args.equipment_id, args.state))
    except Exception as exc:  # noqa: BLE001
        print(f"FAIL  {exc}", file=sys.stderr)
        print("      Is the HMI running with "
              "network.opcua.server.commands_enabled = true?",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
