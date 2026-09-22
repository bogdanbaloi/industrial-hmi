# ADR-0034: The OTA update session is a Boost.SML state machine with no I/O

## Status
Accepted (2026-09-22).

## Context
The transport half of the OTA piece is done: the host can send bytes
(ADR-0032), decode the board's frames and keep them apart from telemetry
(ADR-0033). What is missing is the part that decides: ask the version, erase,
send the image chunk by chunk, verify, confirm, plus handle every way each of
those can go wrong.

That logic is the piece an interviewer will ask about. It is also the
piece that is easiest to get wrong. A late answer, a `NAK`, a board that
comes back on the old image after the reset, a board that already runs the
target version: each has one correct response. None of them is on the
happy path.

Two questions had to be answered. Where does the logic live relative to I/O,
and how are the rules written down.

## Decision
`OtaSession`: the rules of one update, as pure logic behind a declarative
transition table.

- **No I/O, no thread, no clock.** The session takes events (`start`, a
  decoded frame, a tick with the current time) and returns the bytes to send.
  The caller owns the port, the thread and the clock. Every failure case is
  then a unit test that moves the clock by hand, with no board attached.
- **The rules are a Boost.SML transition table**, the same choice
  `app::model::SystemStateMachine` made (ADR-0009 vendored the library). One
  table lists every state, event, guard and action, so the behaviour is a
  single auditable artefact rather than branching spread over a file. In
  ASPICE terms that table is the SWE.2 deliverable.
- **The library stays behind a pimpl.** `boost/sml.hpp` is included only in
  the `.cpp`, so no caller pays its compile time, exactly as the model layer
  does.
- **An unhandled event is a protocol error.** The table lists what the
  protocol allows. Anything else, a frame that does not belong at this point,
  makes `process_event` return false and the session stops with a named
  failure instead of ignoring it.
- **Stop-and-wait, one frame in flight.** A resend repeats the same bytes
  with the same `SEQ`, which the board recognises and does not write twice.
  The budget is 3 resends, inside the board's 10 s silence timeout.
- **Failures are named, not one error.** `BoardRefused` carries the `NAK`
  code, `NoAnswer` means silence after the budget, `WrongVersion` means the
  board came back on another image, `ProtocolError` means a frame that does
  not belong. An operator needs the difference.
- **`NAK BAD_CRC` is the one code that retries.** It means the wire damaged
  the bytes. Every other `NAK` is the board deciding, so the same bytes
  would be refused the same way.
- **A board already on the target version is not re-flashed.** Confirmed:
  nothing to do. On trial: send `CONFIRM` only. Sending the image again
  would erase a bank to write what is already there.

## Alternatives rejected
- **A hand-rolled `switch` on an enum.** It was written first and it works,
  but it puts the same rules in prose across a file, while the project already
  argued in `SystemStateMachine` that a formal table is the auditable form.
  Two state machines in one repo written two different ways is the drift that
  ADR-0028's coding-guidelines work exists to prevent.
- **Coroutines (`co_await` on each answer).** The happy path would read like
  a script, which is the appeal. The failure paths would still need the same
  guards, while a coroutine frame holding the state makes "where is this
  session now" harder to inspect from a test or a UI than `stage()`.
- **Letting the session own a timer thread.** It would make the caller
  simpler and every timeout test slow and flaky. The session would also have
  to be thread-safe for no gain.
- **One `Failed` state with a free-text message.** Cheaper, but an operator
  and a retry policy both need to tell "the board refused" from "the board
  went quiet".

## Consequences
- The whole update chain is testable with no hardware: 13 `OtaSessionTest`
  cases cover the happy path byte for byte, the 8-byte padding of the last
  chunk, a board already up to date, a board on trial, a lost answer, silence
  past the budget, `BAD_CRC` versus a real refusal, `FLASH_ERROR` at
  `COMMIT`, a wrong version after the reset, the order of the whole
  conversation, an empty image and a stale `ACK`.
- The session is not wired to `SerialBackend` yet. That is the next step: a
  thin owner that pumps frames in, bytes out and ticks the clock.
- The table is verbose where a `NAK` or a timeout can arrive in six waiting
  states, because each row is listed per state. That verbosity is the point:
  the reader sees exactly which states accept which events.
- Boost.SML's template errors are unfriendly when a row is malformed. The
  pimpl keeps that cost inside one file.
