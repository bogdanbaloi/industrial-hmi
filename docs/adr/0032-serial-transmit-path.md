# ADR-0032: Serial transmit path (posted writes, one in flight)

## Status
Accepted (2026-09-22).

## Context
ADR-0029 made `SerialBackend` read-only: it listens to a microcontroller's
telemetry and never talks back. The OTA piece changes that. The UART flash
protocol (`docs/protocols/uart-flash-v1.md`) has the host send binary frames
to the board: `INFO_REQ`, `BEGIN`, `DATA`, `COMMIT`, `CONFIRM`, `ABORT`. So
the backend needs a way to write bytes, before any of the protocol exists.

Three constraints shape it. The backend already runs all its I/O on one
`io_context` thread with no lock. A new path should not break that. The
caller of a write will usually live on another thread, the OTA agent's own.
Boost.Asio allows only one outstanding `async_write` per stream, so two
writes started back to back could interleave their bytes on the wire.

## Decision
Add `SerialBackend::send(std::span<const std::byte>)`, callable from any
thread.

- **Copy, then post.** `send()` copies the bytes on the caller's thread and
  posts them onto the `io_context`. The caller's buffer is free when
  `send()` returns. The write queue is touched only on the `io_context`
  thread, so it needs no lock, which keeps ADR-0029's confinement.
- **One write in flight, chained.** Posted bytes join a queue. If nothing is
  in flight, the first `async_write` starts. Its completion handler pops the
  entry and starts the next. Bytes go out whole and in call order.
- **Raw bytes, no framing.** `send()` does not add a delimiter or a checksum.
  Framing belongs to the flash protocol layer on top, the same split
  ADR-0029 made between `SerialFrameParser` and the transport.
- **Accepted is not delivered.** `send()` returns `true` once the bytes are
  queued and `false` when the backend is not running. Bytes still queued at
  `stop()` are dropped. A failed write drops what was queued behind it, so a
  partial sequence is never sent later. The flash protocol is stop-and-wait
  and learns about delivery from the board's `ACK`, not from this call.
- **On `SerialBackend`, not on `IntegrationBackend`.** The other five
  backends have no raw byte channel, so the base interface stays as it is.
- **A mutex for the pointer only.** `start()` and `stop()` replace the
  `io_` state object. `ioMutex_` makes `send()` read that pointer safely
  while it happens. The lock is never held during I/O.

## Alternatives rejected
- **A synchronous `write()` under a mutex.** Simpler, but it blocks the
  caller for the whole transfer, 256 bytes take about 22 ms at 115200 baud.
  It would also run I/O on two threads, which ADR-0029's lock-free
  confinement avoids.
- **Returning a future per write.** Gives delivery per call, but the flash
  protocol already has a stronger signal, the board's `ACK` after the bytes
  are in flash. Two confirmation paths for one fact would drift apart.
- **A bounded queue with back-pressure.** Worth it for a streaming producer.
  The only planned caller sends one frame and waits for the answer, so the
  queue holds one entry in practice. Left for when a second caller appears.

## Consequences
- `SerialBackend` is now bidirectional. The read loop and the parser are
  unchanged, so the telemetry path of ADR-0029 behaves exactly as before.
- Covered by `SerialBackendTest` over the same PTY pair: the protocol's own
  `INFO_REQ` frame arrives byte for byte, LF, CR, NUL and `0xFF` arrive
  untranslated, 200 back-to-back sends arrive in order. `send()` is
  refused before `start()` and after `stop()`. Run clean under TSan and
  ASan with UBSan.
- Not yet done: receiving the board's binary answers. Today the read side
  only understands `sensorId,value` lines, so a flash response would be
  skipped as a malformed line. Telling the two apart on one link is the
  next step of the OTA piece.
