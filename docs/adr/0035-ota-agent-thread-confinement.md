# ADR-0035: OtaAgent confines OtaSession to one thread with a weak-referenced frame sink

## Status
Accepted (2026-09-24).

## Context
The two halves of the OTA piece are done and neither can run an update on its
own. `OtaSession` decides but touches nothing: no I/O, no thread, no clock,
deliberately, so every failure path is a unit test that moves the clock by
hand (ADR-0034). `SerialBackend` moves bytes but decides nothing: it reads the
port, splits flash frames from telemetry and hands each frame to an injected
sink (ADR-0032, ADR-0033).

Something has to own the pair. That owner is the first place in the chain
where the session's rules meet a real thread and a real clock, so the
questions it answers are threading questions: which thread calls into the
session, what happens to a frame that arrives while the owner is being
destroyed, and how a UI reads progress without reaching into a class that is
not thread-safe.

There is also a gap neither half covers. The session has no event for a link
that dies mid-update, because giving it one would put I/O awareness back into
the class ADR-0034 kept I/O-free. But an operator watching a progress bar
needs to be told that the cable came out, and needs to be told promptly, not
after the session's resend budget has quietly run out.

## Decision
`OtaAgent`: a small owner of one session and one link, with the session
confined to a thread rather than locked.

- **Its own `io_context` and one `jthread`, separate from the transport's.**
  Every call into `OtaSession` happens on that thread: the first frame, each
  decoded answer, each tick. The session is therefore touched by exactly one
  thread for its whole life, which is why it needs no lock and why ADR-0034
  could leave it thread-unsafe on purpose.
- **The transport stays behind a `SendFn`.** The agent takes a
  `std::function<bool(std::span<const std::byte>)>`, which
  `SerialBackend::send` happens to fit. It does not name the backend, so the
  agent is testable with no port and reusable over any byte channel.
- **`frameSink()` and the tick timer hold a `std::weak_ptr` to the agent's
  state.** The state lives in a `shared_ptr<Impl>`, and the sink handed to the
  transport captures a weak reference to it. A frame arriving on the
  transport's thread after the agent is destroyed finds an expired `weak_ptr`
  and is dropped, instead of reaching freed state. The sink locks it to make
  the post safe, then the posted handler locks again rather than carrying the
  shared reference: a `shared_ptr` stored inside the `io_context` that lives
  inside the very object it keeps alive is a cycle nothing could break.
- **Progress is a snapshot under a small mutex.** `progress()` returns a copy
  of an `OtaProgress` that the agent thread republishes after every step. A UI
  polls it from any thread and never touches the session.
- **A dead transport is reported beside the session, not through it.**
  `OtaProgress::transportFailed` latches the instant `SendFn` returns false.
  It is independent of `stage()` and `failure()`: the session keeps ticking
  through its normal resend budget and reaches its own terminal state on its
  own schedule. The agent does not force it there.
- **`start()` and `stop()` are idempotent, and the destructor stops.**
  `stop()` is `noexcept` and safe from any thread; it mirrors
  `SerialBackend`'s shutdown idiom, empty catch and all. One agent runs one
  update: after `stop()` it stays stopped.
- **`stop()` returns only once the agent thread is really gone, for every
  caller and not just the first.** The shutdown lock is held across the
  join rather than only around the latch, and `start()` creates the thread
  under it. A caller is entitled to tear down whatever `send` captured the
  moment `stop()` returns, so a second caller leaving early on the strength
  of "someone else is already stopping it" would hand it a live agent thread
  sitting inside that very send function. The one exception is a `stop()`
  made from inside `onDone`, which runs on the agent thread itself: it can
  neither join that thread nor queue behind a join that is waiting on it, so
  it ends the run and leaves the join to whoever is outside.

## Alternatives rejected
- **Posting onto `SerialBackend`'s own `io_context` instead of owning a second
  thread.** One fewer thread, and the frames would already be on the right
  thread. It couples the agent to one transport and defeats the point of
  `SendFn` being transport-agnostic, and it would let a slow update stall the
  read loop that telemetry shares.
- **Adding a `linkDown` event to `OtaSession`.** The tidier-looking option:
  one place to ask how the update is going. It reintroduces I/O awareness into
  the pure-logic class, and it would make "the cable came out" a state
  transition that every test of the table has to reason about.
- **Having the caller, or the UI, drive `onTick()`.** No thread in the agent
  at all. The header's threading contract promises the caller never touches
  the session, and it would split "who owns time" across two layers, with a UI
  frame rate deciding the protocol's timeout resolution.
- **A mutex around `OtaSession` instead of confining it.** Locking a class
  that was written to be single-threaded invites a caller to hold a reference
  across the lock. Confinement makes the rule structural: there is no way to
  reach the session from outside.
- **A raw `this` in the frame sink, with the transport stopped first.** It
  works while the wiring is correct and is undebuggable when it is not. A
  use-after-free reached from a driver's read thread is the worst kind of bug
  this project can ship, so the seam is a `weak_ptr` and a test proves it.

## Consequences
- The whole update chain is now testable end to end with no hardware:
  `OtaAgentTest` plays the board through the injected `SendFn` and answers
  through the agent's real frame sink, so the test exercises the same seam the
  driver will use.
- Timeout resolution is a poll. The agent ticks the session every 5 ms rather
  than computing the next deadline, because the session does not expose one.
  That costs a few hundred wakeups against the spec's 2 s answer budget, which
  is cheap, and it keeps the agent from duplicating the session's own idea of
  when things are due.
- `transportFailed` latches and never clears. Nothing at this layer can bring
  a link back, and an update that lost its link is over in any case; a
  transient false that cleared itself would be a worse report than a sticky
  true.
- On Windows a target that compiles `OtaAgent.cpp` must link `ws2_32`. The
  agent opens no socket, but Asio's Windows `io_context` is the IOCP one and
  its `winsock_init` calls `WSAStartup` from the context's constructor. The
  comment on `objectsSerial` claiming serial needs no Winsock was true of
  `serial_port` and not of `io_context`, so the library now carries
  `PUBLIC ws2_32 mswsock` itself, the rule `objectsModel`, `objectsTcp`,
  `objectsMqtt` and `objectsHttp` already follow. `test_ota_agent` compiles
  the sources directly rather than linking the library, so it carries its
  own line as well.
- The agent is not yet wired into `IntegrationBootstrap`, and nothing in the
  UI triggers an update. That is the next step, and it is deliberately not
  this one: this piece is the seam, and it is provable on its own.
