# ADR-0029: Serial telemetry ingest (line-framed protocol, asio transport)

## Status
Accepted (2026-09-17).

## Context
The integration layer has five inbound/outbound backends (TCP, MQTT, Modbus,
OPC-UA, HTTP). None reads from a serial port. A microcontroller on a bench (an
STM32 Nucleo) exposes a USB virtual COM port, and reading its telemetry lets
the HMI demonstrate the software/hardware boundary on real silicon, the same
adjacency the portfolio already claims from automotive and industrial work.

A serial link is a raw byte stream: reads arrive in arbitrary chunks with no
message boundaries. Two decisions follow: how to frame messages on that stream,
and how to do the serial I/O.

## Decision
Add a sixth backend, `SerialIngestBackend`, opt-in behind
`integration.serial.enabled` and compile-gated behind `BUILD_SERIAL_BACKEND`
(default OFF), mirroring the HTTP backend's gating (REQ-INTEGRATION-007).

- **A line-framed ASCII protocol, `sensorId,value\n`.** The microcontroller
  sends one reading per line. A newline delimits frames. ASCII (not binary) is
  chosen so a reading is human-readable in any serial terminal, which makes the
  link debuggable by eye. The value is carried as text and interpreted per
  sensor by the existing ingest path, not by the framing layer.
- **Framing separated from I/O.** `SerialFrameParser` owns only the
  stream-to-frames logic: it buffers incoming bytes, emits a reading per
  complete line, holds the trailing partial for the next chunk, tolerates CRLF,
  skips malformed lines without throwing, and bounds its buffer against a
  delimiter-less stream. It performs no I/O, so it is unit-tested by feeding it
  bytes directly, with no serial port attached.
- **Reuse `boost::asio::serial_port` for the transport.** Boost.Asio is already
  a dependency (TcpBackend, ModbusClient), and its `serial_port` gives
  cross-platform async serial reads with no new dependency. The backend reads
  chunks from the port and feeds them to the parser.

## Alternatives rejected
- **A length-prefixed binary frame.** More efficient on the wire and the right
  choice at high rates, but not human-readable, so it loses the terminal-level
  debuggability that matters for a bench/demo piece. Honest to note the binary
  choice would win in a real high-throughput link.
- **Raw termios / Win32 serial calls behind `#ifdef`.** Works, but hand-rolls
  what `boost::asio::serial_port` already gives portably, and the project
  already speaks asio in its other backends.
- **Parse inside the read loop.** Folding framing into the I/O code would make
  the logic untestable without a real or emulated serial port. Splitting the
  pure parser out is what lets the framing be covered by fast, hardware-free
  unit tests.

## Consequences
- A new `SerialFrameParser` (this commit) plus a `SerialBackend` over
  `boost::asio::serial_port` (next), wired into `IntegrationManager` /
  `IntegrationBootstrap` like the other backends.
- The parser is fully unit-tested with no hardware; the backend is exercised
  against an emulated serial endpoint (a PTY pair on POSIX), so CI needs no
  attached device.
- Honesty rail: this reads a bench microcontroller over a wire, not a fielded
  device over the air. It demonstrates the host side of a device/host link. The
  microcontroller firmware and any real over-the-air transport are separate
  pieces, not claimed by this backend.

## Reference device

The companion firmware
[`hmi-edge-node`](https://github.com/bogdanbaloi/hmi-edge-node) is the reference
device that emits these frames: a bare-metal STM32 Nucleo-L476RG that sends
`equipment/<n>/state,on|off` and `temp,<raw>` over USART2 on a button press. The
two projects are independent and interoperate only through this serial protocol,
so any device that speaks it can feed the backend.
