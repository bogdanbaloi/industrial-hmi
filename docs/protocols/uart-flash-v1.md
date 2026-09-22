# UART flash protocol, version 1

**Status: PROPOSED, not agreed.** Neither side writes code against this until
firmware has answered on the board.

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
there is no payload. `E9 CD` is the checksum. The board confirms with the same
number:

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

Error codes carried by `NAK`:

| Code   | Name            | When |
| ------ | --------------- | ---- |
| `0x01` | `BAD_CRC`       | The frame checksum did not match. |
| `0x02` | `BAD_STATE`     | The message is not allowed right now, for example `DATA` before `BEGIN`. |
| `0x03` | `TOO_LARGE`     | The announced image does not fit in one bank. |
| `0x04` | `BAD_OFFSET`    | The offset leaves a gap or points outside the image. |
| `0x05` | `FLASH_ERROR`   | Erasing or programming the flash failed. |
| `0x06` | `VERIFY_FAILED` | At `COMMIT`, the CRC32 of the flash does not match the one from `BEGIN`. |

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
| A frame is corrupted on the wire      | The board answers `NAK BAD_CRC`. The host resends the same frame. |
| An `ACK` is lost                      | The host times out and resends. The board recognises the `SEQ`, does not write again, answers `ACK`. |
| The host dies in the middle           | The board gives up after a silence timeout and returns to normal mode. The running image was never touched. |
| Power is lost during the transfer     | Same outcome. Only the empty bank was being written. The running bank is intact, the update simply restarts. |
| The image arrives but is wrong        | `COMMIT` fails with `VERIFY_FAILED`. The board never switches banks. |
| The new image boots but misbehaves    | No `CONFIRM` arrives, so the board rolls back. |
| The new image crashes before it runs  | **Not solved by this protocol.** See open question 1 in section 9. |

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

## 9. Open questions for firmware

These are firmware's decisions. The protocol only needs the answers.

1. **Who rolls back an image that crashes before it can count?** The
   confirmation window runs inside the new image. An image that faults at
   startup never reaches that code. Worse, the current fault handler blinks a
   code on the LED forever. A watchdog plus a boot counter is the usual answer.
   How to build it is firmware's call.
2. **The bank switch.** Proposed: the L476 dual-bank flash with the `BFB2`
   option bit. Firmware to confirm, including that images are linked for
   `0x08000000` and fit in 512 KB.
3. **Where the `TRIAL` or `CONFIRMED` state survives a reset.**
4. **The confirmation window.** Proposed: 30 seconds.
5. **How the board receives.** Today the UART only transmits, see section 10.
   Interrupt-driven reception is recommended, given the 4 MHz clock.
6. **Does resetting the board keep the host's serial port open?** Assumed yes,
   because the USB side belongs to the ST-Link chip, not to the target. Not
   verified.

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
