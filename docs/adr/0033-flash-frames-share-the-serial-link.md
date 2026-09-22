# ADR-0033: Flash frames share the serial link with telemetry (split by start byte)

## Status
Accepted (2026-09-22).

## Context
The serial link now carries two kinds of traffic. The board sends telemetry
as `sensorId,value\n` text lines (ADR-0029). During an update it also answers
the host with binary UART flash protocol frames: `INFO`, `ACK`, `NAK`
(`docs/protocols/uart-flash-v1.md`). ADR-0032 gave the host a way to send
frames. The host now has to receive them, on the same wire as the text.

Today the read side only knows text. A binary frame would reach
`SerialFrameParser` and be skipped as a malformed line. Worse, a frame may
contain `0x0A`, which the text parser would read as the end of a line.

The frame format already offers what is needed. Every frame begins with
`0xA5`, a byte that is not printable and so never occurs in telemetry text.
Every frame carries its own length and a CRC-16.

## Decision
Add `FlashFrameParser`, a pure-logic splitter that sits in front of the text
parser, plus `encodeFlashFrame` and `crc16CcittFalse` in `FlashFrame.h`.

- **Split by start byte, no modes.** Every byte outside a frame is text and
  is handed on unchanged. A `0xA5` starts a frame candidate. Telemetry and
  frames may interleave at any time.
- **Read a frame by its `LEN`, never by a delimiter.** The payload is binary
  and may hold `0x0A` or another `0xA5`. Only `LEN` says where it ends.
- **Resync from the next byte.** A candidate whose `LEN` exceeds 260 or whose
  CRC does not match is a false start. Only its `0xA5` is dropped. The scan
  resumes at the very next byte, so a real frame hidden behind the garbage is
  found at once. This is the rule both sides agreed in section 9, item 8 of
  the protocol.
- **Bounded by construction.** The parser holds at most one unfinished frame,
  268 bytes. Text is never held. A long run of text cannot grow the buffer.
- **False starts are counted, not thrown.** `falseStarts()` is a health
  signal for a noisy link. Nothing on the wire can make `consume()` throw.
- **Pure logic, no I/O.** Like `SerialFrameParser`, it is tested by feeding
  bytes directly. The protocol's own worked example and CRC check value are
  test cases, so the code and the agreed contract cannot drift apart
  silently.

Wiring the parser into `SerialBackend` is a separate step. This ADR covers
the split itself.

## Alternatives rejected
- **A mode switch.** The board stops telemetry during an update and the host
  flips its parser to binary. Simpler to read, but it breaks exactly at the
  switch: a telemetry line still in flight when the mode flips is read as a
  frame. A frame arriving before the flip is read as text. On top of that it puts
  a timing rule on the firmware that the start byte makes unnecessary.
- **Skip `LEN` bytes after a bad CRC.** Cheaper on a clean link, but `LEN`
  is exactly the field that cannot be trusted after a CRC failure. A garbage
  value up to 65535 would swallow every real frame behind it.
- **A second serial port for flashing.** Clean separation, but the Nucleo's
  ST-Link exposes one virtual COM port. A second one means extra
  hardware for a problem one byte already solves.
- **Escape `0xA5` inside payloads (byte stuffing, as in SLIP or HDLC).** It
  would let a receiver find frame starts without reading `LEN`. The length
  prefix plus CRC already gives that. Stuffing also makes frame sizes depend
  on content, which complicates the 8-byte `DATA` rule.

## Consequences
- The host can decode every frame the board sends and can encode every frame
  it sends, from one tested codec.
- Covered by `FlashFrameParserTest` (10 cases): the CRC check value `0x29B1`,
  both frames of the protocol's worked example, a frame fed one byte at a
  time, payloads holding `0x0A` and `0xA5`, text around a frame passing
  through unchanged, a bad CRC followed by a good frame, a garbage `LEN` of
  65535 followed by a good frame, a stray `0xA5` before a text line plus an
  unfinished frame held until the rest arrives.
- A false start delays text by at most one frame's worth of bytes, because
  the candidate is held until its CRC can be checked. In the common case, a
  stray `0xA5` before text, the `LEN` bytes are printable characters far
  above 260, so the candidate is rejected at once with no delay.
- Not yet done: `SerialBackend` still feeds every byte to the text parser.
  Putting `FlashFrameParser` in front of it and routing frames to their own
  sink is the next step.
