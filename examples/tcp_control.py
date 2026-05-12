#!/usr/bin/env python3
"""Walk the HMI's TCP control surface through a canned scenario.

Connects to `localhost:5555` (the default `network.tcp.port`), runs a
short sequence of commands the dashboard will react to in real time,
and prints every reply the server sent.

Stdlib only -- no `pip install` required.

Run alongside the HMI:

    Terminal 1:  ./build/with-backends/industrial-hmi
    Terminal 2:  python examples/tcp_control.py

Expected dashboard reaction:
  * SYSTEM badge flips RUNNING after `production start`
  * A-LINE switch flips OFF after `eq 0 off`
  * A-LINE switch flips back ON after `eq 0 on`
  * SYSTEM badge returns to IDLE after `production stop`
  * I/O panel `TCP` pill stays green CONNECTED while this script
    holds the socket; reverts to CONNECTING after it exits.
"""

import argparse
import socket
import sys
import time


# Each step is (command sent, human-readable note shown alongside).
# The note explains what the operator should see on the dashboard so
# the script doubles as a verification checklist.
STEPS = [
    ("status",             "snapshot before we change anything"),
    ("production start",   "SYSTEM badge -> RUNNING"),
    ("eq 0 off",           "A-LINE switch -> OFF"),
    ("eq 1 off",           "B-LINE switch -> OFF"),
    ("eq 0 on",            "A-LINE switch -> ON"),
    ("eq 1 on",            "B-LINE switch -> ON"),
    ("production stop",    "SYSTEM badge -> IDLE"),
    ("status",             "snapshot after"),
    ("quit",               "close the connection cleanly"),
]


def run(host: str, port: int, delay: float) -> int:
    try:
        sock = socket.create_connection((host, port), timeout=5.0)
    except (OSError, ConnectionRefusedError) as exc:
        print(f"FAIL  Could not reach {host}:{port}: {exc}",
              file=sys.stderr)
        print("      Is the HMI running? "
              "Is network.tcp.enabled = true in config?",
              file=sys.stderr)
        return 1

    sock.settimeout(2.0)
    print(f"OK    connected to {host}:{port}")
    print(f"      ({len(STEPS)} commands, ~{delay * len(STEPS):.1f}s)")
    print()

    for cmd, note in STEPS:
        print(f">>> {cmd:<20}  # {note}")
        sock.sendall((cmd + "\n").encode("ascii"))
        # Give the HMI a beat to apply the change and ship a reply
        # before we send the next command. The line-oriented protocol
        # would let us pipeline, but staggering makes the dashboard
        # changes visually distinct.
        time.sleep(delay)
        try:
            reply = sock.recv(4096).decode("utf-8", errors="replace")
        except socket.timeout:
            reply = "(no reply within 2s)"
        if reply.strip():
            print(f"    {reply.strip()}")
        print()

    sock.close()
    print("DONE  socket closed; TCP pill should flip back to "
          "CONNECTING in a moment")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1",
                        help="HMI TCP backend host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=5555,
                        help="HMI TCP backend port (default: 5555)")
    parser.add_argument("--delay", type=float, default=0.8,
                        help="Seconds to wait between commands "
                             "(default: 0.8 -- room to watch the UI)")
    args = parser.parse_args()
    return run(args.host, args.port, args.delay)


if __name__ == "__main__":
    sys.exit(main())
