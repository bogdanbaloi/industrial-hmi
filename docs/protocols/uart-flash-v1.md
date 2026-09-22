# UART flash protocol, version 1

**Status: AGREED on 2026-09-22.** One open check: the worst-case bank erase
time, which sets the host's wait after `BEGIN` (section 6, "Timeouts").

**Owners.** The protocol is a contract owned by both sides. industrial-hmi
writes the host side, a C++ update agent on Linux. firmware writes the target
side, the Nucleo-L476RG acting as the ECU being updated.

## 1. The whole idea, in plain words

Picture sending a new book to a friend who works slowly and has a small desk.

- You cannot send the whole book at once, there is nowhere to put it. You send
  one page. Your friend glues it into a notebook and says "glued". Only then
  do you send the next one.
- Every page travels in an envelope with a number on it. If the "glued" reply
  gets lost, you resend page 7. Your friend sees "7, I already have it" and
  does not glue it twice.
- The envelope says how long the page is, so your friend knows where it ends.
- The envelope also carries a checksum of the page. If your friend computes a
  different one, the page was damaged on the way and gets requested again.
- Your friend owns two notebooks: the one being read now, plus an empty one.
  The new book goes into the empty notebook, so the old one is never touched.
- At the end your friend checks the whole book against one big checksum. If it
  matches, reading switches to the new notebook.
- The new book is read on trial. You call and ask "does it work?". If yes, you
  say "keep it". If you do not call within 30 seconds, your friend goes back
  to the old notebook without being told.

That is the whole protocol. Everything below is how the envelope is laid out.

| In the story                          | In the protocol            |
| ------------------------------------- | -------------------------- |
| one page at a time, wait for "glued"  | stop-and-wait              |
| the number on the envelope            | `SEQ`                      |
| how long the page is                  | `LEN`                      |
| the checksum of one page              | `CRC16`                    |
| two notebooks                         | the two flash banks, A/B   |
| the big checksum at the end           | `CRC32` at `COMMIT`        |
| reading the new book on trial         | the `TRIAL` state          |
| "keep it"                             | `CONFIRM`                  |
| going back to the old notebook        | rollback                   |

## 2. Why the link needs a protocol at all

A UART delivers a stream of bytes and nothing else. It does not know where one
message ends and the next begins. It does not notice a corrupted byte. It never
says whether the other side received anything. Each of those has to be built on
top, which is what this document does.

## 3. The envelope, called a frame

Every message travels as one frame:

    A5 | TYPE (1) | SEQ (2) | LEN (2) | PAYLOAD (0..260) | CRC16 (2)

The numbers in brackets are sizes in bytes.

| Field     | Size     | What it is             | Why it is there |
| --------- | -------- | ---------------------- | --------------- |
| `A5`      | 1        | start of frame         | Marks where a message begins. `0xA5` is not a printable character, so it can never be mistaken for the start of a telemetry text line such as `temp,23.5`. |
| `TYPE`    | 1        | which message this is | Tells the receiver how to read the rest. Host messages are `0x01` to `0x06`. Board messages have the top bit set, `0x81` to `0x83`, so the direction shows at a glance. |
| `SEQ`     | 2        | message number         | Lets the board recognise a frame that was sent twice. Lets an `ACK` say which frame it confirms. |
| `LEN`     | 2        | payload length         | Tells the receiver where the payload ends and the checksum starts. |
| `PAYLOAD` | 0 to 260 | the content            | For `DATA`, a 4-byte offset followed by up to 256 bytes of the image. |
| `CRC16`   | 2        | checksum               | Computed over every byte between `A5` and itself. A mismatch means a bit flipped on the wire. |

Numbers wider than one byte are **little-endian**: the low byte goes first, so
`SEQ` 1 is `01 00`. The Cortex-M4 and x86 both store numbers that way, so
neither side has to reorder bytes.

The checksum is **CRC-16/CCITT-FALSE**: polynomial `0x1021`, initial value
`0xFFFF`, no reflection, no final XOR. Its standard check value over the ASCII
string `123456789` is `0x29B1`. An implementation proves it picked the right
variant by reproducing that value.

### A worked example

The host asks the board what it is running, as message number 1:

    A5  01  01 00  00 00  E9 CD

`A5` starts the frame. `01` is `INFO_REQ`. `01 00` is `SEQ` 1. `00 00` says
there is no payload. `E9 CD` is the checksum. The board answers `INFO_REQ`
with an `INFO` frame (section 4). An `ACK` for a frame with `SEQ` 1, the answer
to `DATA`, `COMMIT` or `CONFIRM`, looks like this:

    A5  82  01 00  00 00  EB 01

Both checksums were computed by code that first reproduced `0x29B1`, not
written by hand.

## 4. The messages

| Code   | Direction     | Name       | Payload                                        | Meaning |
| ------ | ------------- | ---------- | ---------------------------------------------- | ------- |
| `0x01` | host to board | `INFO_REQ` | empty                                          | What are you running? |
| `0x02` | host to board | `BEGIN`    | size `u32`, image CRC32 `u32`, version `u32`   | A new image is coming, prepare the empty bank. |
| `0x03` | host to board | `DATA`     | offset `u32`, then up to 256 bytes of image    | Write these bytes at this offset. |
| `0x04` | host to board | `COMMIT`   | empty                                          | Everything is sent. Verify it, then switch banks. |
| `0x05` | host to board | `CONFIRM`  | empty                                          | The new image works, keep it. |
| `0x06` | host to board | `ABORT`    | empty                                          | Cancel. Leave the running image alone. |
| `0x81` | board to host | `INFO`     | version `u32`, active bank `u8`, state `u8`    | The answer to `INFO_REQ`. State `0` is `CONFIRMED`, `1` is `TRIAL`. |
| `0x82` | board to host | `ACK`      | empty                                          | Done. The `SEQ` in the header says which frame. |
| `0x83` | board to host | `NAK`      | error code `u8`                                | Refused. The code says why. |

Every `DATA` frame carries a multiple of 8 image bytes, because the L4 flash is
programmed 8 bytes at a time. The last frame is padded with `0xFF`, the value of
erased flash. The CRC32 announced in `BEGIN` covers the image size only, never
the padding.

The largest image is **522240 bytes**: one 512 KB bank minus its last 2 KB
page, which holds the `CONFIRMED` record (section 9, item 4). A `BEGIN` above
that is refused with `TOO_LARGE`.

The image checksum is **CRC-32/ISO-HDLC**, the one zlib and Ethernet use:
reflected polynomial `0xEDB88320` (`0x04C11DB7` unreflected), initial value
`0xFFFFFFFF`, input and output reflected, final XOR `0xFFFFFFFF`. Its check
value over the ASCII string `123456789` is `0xCBF43926`. Pinned on
2026-09-22, because several CRC-32s share the polynomial and differ only in
reflection and final XOR. A mismatch would give a plausible number that
matches nothing. `COMMIT` would then fail with `VERIFY_FAILED` on a perfect
transfer. Both sides prove the variant by reproducing the check value.

Error codes carried by `NAK`:

| Code   | Name            | When |
| ------ | --------------- | ---- |
| `0x01` | `BAD_CRC`       | The frame checksum did not match. |
| `0x02` | `BAD_STATE`     | The message is not allowed right now, for example `DATA` before `BEGIN`. |
| `0x03` | `TOO_LARGE`     | The announced image does not fit in one bank. |
| `0x04` | `BAD_OFFSET`    | The offset leaves a gap or points outside the image. |
| `0x05` | `FLASH_ERROR`   | Erasing or programming the flash failed. |
| `0x06` | `VERIFY_FAILED` | At `COMMIT`, the CRC32 of the flash does not match the one from `BEGIN`. |
| `0x07` | `BAD_MESSAGE`   | The frame arrived intact but its content is malformed: an unknown `TYPE`, a payload of the wrong length for its type, a `BEGIN` announcing zero bytes, or `DATA` whose image bytes are empty or not a multiple of 8. Added on 2026-09-22. |

The board checks every message in a fixed order: shape first (`BAD_MESSAGE`),
then whether it is allowed now (`BAD_STATE`), then its content (the specific
code). So a `NAK` always names the first thing that is wrong.

## 5. One update, step by step

0. The board is in normal mode, sending telemetry lines. The agent first checks
   that an update is allowed at all, using the button state the telemetry
   already reports. In a car this means parked with the engine off. Here the
   button stands in for that signal.
1. `INFO_REQ`, answered by `INFO`. The agent learns which version runs and from
   which bank.
2. `BEGIN` with the size, the CRC32 and the new version. The board checks that
   the image fits in one bank, erases the empty bank, then answers `ACK`. The
   erase takes time, which is why the `ACK` only comes after it.
3. `DATA`, repeated. One frame, wait for its `ACK`, then the next frame. The
   board answers only once the bytes are actually in flash.
4. `COMMIT`. The board computes the CRC32 of what it wrote and compares it with
   the one announced in `BEGIN`. If they match, it marks the new bank as the
   one to boot from, answers `ACK`, then resets itself.
5. The board starts from the new bank in `TRIAL`. The agent sends `INFO_REQ`,
   checks that the reported version is the new one, then sends `CONFIRM`. The
   board moves to `CONFIRMED`.
6. If `CONFIRM` does not arrive within the confirmation window, the board goes
   back to the old bank by itself. That is the rollback.

Once the board has entered an update session it stops sending telemetry until
the session ends. The host therefore never has to separate text lines from
binary frames inside one session.

## 6. When things go wrong

This is where an update chain is judged. A happy path proves little.

| What happens                          | What the protocol does |
| ------------------------------------- | ---------------------- |
| A frame is corrupted on the wire      | Inside a session the board answers `NAK BAD_CRC` and the host resends the same frame. Outside a session the board stays silent: a corrupted frame there is almost always line noise. A `NAK` would put binary bytes into the telemetry for nothing. The host's own 2 s timeout covers it. |
| A message arrives malformed           | The board answers `NAK BAD_MESSAGE`. Resending the same bytes cannot help, so the host reports a bug instead of retrying. |
| An `ACK` is lost                      | The host times out and resends. The board recognises the `SEQ`, does not write again, answers `ACK`. |
| The host dies in the middle           | The board gives up after 10 seconds of silence (see "Timeouts" below) and returns to normal mode. The running image was never touched. |
| Power is lost during the transfer     | Same outcome. Only the empty bank was being written. The running bank is intact, the update simply restarts. |
| The image arrives but is wrong        | `COMMIT` fails with `VERIFY_FAILED`. The board never switches banks. |
| Flash fails, or the image fails its check | `FLASH_ERROR` and `VERIFY_FAILED` end the session. The board returns to normal mode and the host restarts from `BEGIN` (no resume, section 8). |
| A `BEGIN` arrives during a session    | The session starts over: the board erases the empty bank again. |
| The new image boots but misbehaves    | No `CONFIRM` arrives, so the board rolls back. |
| The new image crashes before it runs  | The watchdog resets the board and the old image comes back. See section 9, item 1. |

### Timeouts

Pinned on 2026-09-22. Each side waits only while it is waiting for the other
one, never while it is busy itself.

| Who waits | For what | Limit | Then |
| --------- | -------- | ----- | ---- |
| Board | the next frame, counted from its own last answer (`ACK`, `NAK` or `INFO`) | 10 s | Ends the session, back to normal mode. The host restarts from `BEGIN`. |
| Host | the answer to `INFO_REQ`, `DATA`, `COMMIT`, `CONFIRM` or `ABORT` | 2 s | Resends the same frame with the same `SEQ`, at most 3 times, then gives up. |
| Host | the answer to `BEGIN` | worst-case erase time plus 2 s | Same retry rule. The erase time is the one open check (see the status line). |

Why "counted from the board's last answer": at `BEGIN` the board erases a
whole bank before it answers. That time is the board's own work and must not
count against the host. The board's clock starts only when it hands the turn
back.

Why the numbers fit together: a healthy host answers within milliseconds.
Three resends of 2 s each take 6 s, still inside the board's 10 s, so a lost
frame is recovered before the board gives up. A resend is harmless because
the board recognises the `SEQ` and the offset (section 7, decision 4).

## 7. Decisions, each with the option rejected

1. **Stop-and-wait: one frame, then wait for its `ACK`.** The Nucleo's link has
   no hardware flow control. Erasing flash keeps the board busy long enough for
   hundreds of bytes to arrive at 115200 baud with nowhere to go. Answering only
   after the bytes are in flash makes an overrun impossible by construction.
   *Rejected:* a sliding window, which needs buffering and flow control this
   link does not have.
2. **Binary frames, not text.** The image is binary. Encoding it as hex text
   would double its size and the transfer time. *Rejected:* a text protocol,
   easier to read in a terminal but twice the bytes.
3. **A length prefix, not a delimiter.** The telemetry ends each line with
   `\n`, which works because a text value never contains `\n`. An image can
   contain any byte, including `0x0A`, so a delimiter would cut it in the wrong
   place. *Rejected:* byte stuffing such as COBS. It removes the delimiter
   problem but adds encoding code on both sides, for no gain at this size.
4. **Both `SEQ` and an offset.** `SEQ` makes a resent frame harmless: the board
   acknowledges it without writing twice. The offset says exactly where the
   bytes go, so the board can reject a gap instead of writing the rest of the
   image one chunk off.
5. **Two checksums, two questions.** `CRC16` per frame asks "did this arrive
   intact?". `CRC32` over the image asks "is what now sits in flash exactly the
   image that was announced?". A frame can arrive intact and still be written
   to the wrong place.
6. **The host confirms, the board does not judge itself.** A board that has
   booted cannot tell that it works properly. The agent checks, then confirms.
   Silence means rollback. This is how a telematics unit behaves as update
   master for the ECUs behind it.
7. **The erase happens once, at `BEGIN`.** Erasing the whole empty bank up front
   means `DATA` only ever programs, which is fast and predictable. *Rejected:*
   erasing page by page during `DATA`, which would make some `ACK`s arrive far
   later than others.

## 8. What version 1 deliberately does not do

- **No signature check on the board.** The agent verifies the image signature
  before sending anything, the way a TCU authenticates a downloaded package. The
  board checks integrity with CRC32 only. A production ECU would also verify the
  signature itself, through secure boot.
- **No encryption.** The link is a cable on a desk.
- **No resume.** An interrupted transfer starts again from `BEGIN`. The running
  image is never at risk, so restarting costs time only.
- **One image, one board.**

## 9. Answers from the target side

Asked on 2026-09-22, answered the same day. Hardware facts come from ST's
CMSIS header `stm32l476xx.h`, ST's HAL driver source and AN4767.

1. **Who rolls back an image that crashes early: the independent watchdog.**
   A hardware timer that resets the chip unless the program refreshes it.
   Once started it cannot be stopped. In `TRIAL` the new image does not
   refresh it until `CONFIRM` arrives. So a fault, a hang and a missing
   `CONFIRM` all end in the same reset. A flag records "already tried once".
   The second boot of an unconfirmed image rolls back. Limit: that check runs
   in the new image's own early boot, so an image broken in its reset path is
   not saved. Production ECUs add an immutable first-stage bootloader for
   that. Out of scope for version 1.
2. **The watchdog runs in `CONFIRMED` too.** Decided 2026-09-22. The fault
   handler no longer blinks forever. It blinks until the watchdog resets the
   board, about 32 seconds. A device that restarts after a fault is the
   expected behaviour for an ECU.
3. **The bank switch: `BFB2`, confirmed.** Two banks of 512 KB. Both images
   are linked for `0x08000000`, because the active bank is remapped there. So
   one binary, linked once. The linker region shrinks to 512 KB, so an image
   that outgrows a bank fails at link time.
4. **Where the state survives a reset: in flash, recorded the other way
   round.** `CONFIRMED` is a positive record in the last 2 KB page of the
   running bank, written only on `CONFIRM`. No record means unconfirmed. So a
   power cut during a trial can never confirm an image by accident. The
   "already tried once" flag may live in a backup register, because losing it
   is the safe direction.
5. **The confirmation window: about 30 seconds, not exactly.** The window is
   the watchdog period, around 32 seconds. It varies with the internal clock.
   The host must not depend on the exact value.
6. **How the board receives: interrupt driven into a ring buffer.** The frame
   parser and the update logic are pure code with host tests, including the
   CRC-16 check value `0x29B1`. Flash is programmed 8 bytes at a time, which
   matches the 8-byte rule in section 4.
7. **Does a reset keep the host's serial port open: yes, proven.** Tested on
   the real board on 2026-09-22. Across four monitor sessions with resets,
   the port was never lost and never needed a reconnect. The same test found
   that every reset put one stray `0xFF` on the line (46 resets out of 46).
   The cause was the order of two register writes in the TX pin setup, fixed
   on the target side. After the fix: 0 stray bytes in at least 6 resets.
8. **Resync, kept anyway.** Both sides use the same rule. On a CRC-16
   failure, the receiver looks for the next `0xA5` starting from the byte
   after the false start. It never skips `LEN` bytes, because a garbage
   `LEN` can reach 65535 and swallow real frames behind it.

**The one irreversible step.** Switching banks writes the chip's option
bytes. One option byte value, read protection level 2, locks the chip
forever. The target code changes `BFB2` only, with a masked
read-modify-write. It checks that read protection is at level 0 before
applying. The first real option byte write is done by hand, deliberately,
never by a script.

## 10. What both codebases need first

Found by reading both repositories on 2026-09-22, before any design.

| Side           | Today                                                                  | Needed |
| -------------- | ---------------------------------------------------------------------- | ------ |
| firmware       | USART2 only transmits: `PA2`, the `TE` bit, the `TDR` register. No `PA3`, no `RE`, no `RDR`. | Reception. |
| firmware       | The linker script uses the full 1 MB as one region.                    | Link the image for one 512 KB bank. |
| industrial-hmi | `SerialBackend` only reads, through `async_read_some`.                 | A transmit path. |

## 11. Glossary

In the order the terms appear.

- **UART.** A serial link that moves bytes one bit at a time over a wire. It has
  no idea what a message is.
- **Frame.** One message on the wire with a fixed layout, the envelope from
  section 1.
- **Stop-and-wait.** Send one frame, wait for the answer, then send the next.
- **CRC.** Cyclic redundancy check. A checksum that changes if any bit of the
  data changes.
- **Little-endian.** The low byte of a number is sent first.
- **Idempotent.** Doing it twice has the same effect as doing it once. A resent
  `DATA` frame is idempotent because the board recognises its `SEQ`.
- **A/B, dual bank.** Two separate halves of the flash. One runs, the other
  receives the update.
- **Rollback.** Going back to the previous image when the new one is not
  confirmed.
- **ECU.** Electronic control unit, one of the small computers in a car.
- **TCU.** Telematics control unit, the ECU with the network connection. It acts
  as update master for the others.
