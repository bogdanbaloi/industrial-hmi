#!/usr/bin/env python3
"""Minimal MCP client for industrial-hmi-mcp -- no third-party deps.

Spawns the MCP server, speaks the Model Context Protocol over stdio
(JSON-RPC 2.0), and exercises the read-only tools the way an LLM client
(Claude Desktop, MCP Inspector) would. Use it to test the server without
installing Node/npx.

The server writes its logs to stderr and the JSON-RPC responses to stdout,
so we read stdout only and send the server's stderr to the terminal (pass
--quiet to discard it).

Usage:
    python examples/mcp_client.py [--exe PATH] [--quiet]

Default exe path is build-qt/industrial-hmi-mcp.exe (the MSYS2 build); pass
--exe for another build dir. Build it first with:
    cmake --build build-qt --target industrial-hmi-mcp   # -DBUILD_MCP_SERVER=ON
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

DEFAULT_EXE = "build-qt/industrial-hmi-mcp.exe"


def rpc(request_id: int, method: str, params: dict | None = None) -> str:
    """Encode one JSON-RPC 2.0 request as a single newline-terminated line."""
    message: dict = {"jsonrpc": "2.0", "id": request_id, "method": method}
    if params is not None:
        message["params"] = params
    return json.dumps(message) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=DEFAULT_EXE,
                        help=f"path to the MCP server binary (default {DEFAULT_EXE})")
    parser.add_argument("--quiet", action="store_true",
                        help="discard the server's stderr log output")
    args = parser.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"error: {exe} not found -- build it with "
              f"'cmake --build build-qt --target industrial-hmi-mcp'",
              file=sys.stderr)
        return 1

    # One request per tool, in the order an MCP client uses them.
    requests = [
        rpc(1, "initialize", {}),
        rpc(2, "tools/list"),
        rpc(3, "tools/call", {"name": "alarms_snapshot"}),
        rpc(4, "tools/call",
            {"name": "historian_query",
             "arguments": {"field": "quality", "entityId": 1}}),
    ]

    # Run the server from the binary's own directory so it finds its config /
    # historian / locale layout, the same as launching it directly from there.
    # stdout is captured (the JSON-RPC channel); stderr (the server's logs) is
    # discarded with --quiet or left to flow to the terminal otherwise.
    proc = subprocess.run(
        [str(exe.resolve())],
        input="".join(requests),
        stdout=subprocess.PIPE,
        stderr=(subprocess.DEVNULL if args.quiet else None),
        text=True,
        cwd=str(exe.resolve().parent),
        timeout=15,
    )

    # stdout is one JSON-RPC response per line. Pretty-print each.
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            print(json.dumps(json.loads(line), indent=2, ensure_ascii=False))
        except json.JSONDecodeError:
            print(f"(non-JSON on stdout) {line}", file=sys.stderr)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
