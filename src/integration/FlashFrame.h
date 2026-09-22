#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace app::integration {

/// One message of the UART flash protocol v1
/// (`docs/protocols/uart-flash-v1.md`, section 3). On the wire:
///
///     A5 | TYPE (1) | SEQ (2) | LEN (2) | PAYLOAD (0..260) | CRC16 (2)
///
/// Multi-byte fields are little-endian. The CRC covers every byte between
/// the start byte and itself. REQ-INTEGRATION-013, ADR-0033.
///
/// @rationale The struct holds only what varies per message. The start
/// byte, `LEN` and the CRC are derived by `encodeFlashFrame`, so a caller
/// cannot build a frame whose length or checksum disagrees with its
/// payload.
struct FlashFrame {
    /// Message code, for example `0x01` INFO_REQ or `0x82` ACK. Kept as a
    /// raw byte: naming the codes belongs to the protocol layer above.
    std::uint8_t type = 0;
    /// Message number. An `ACK` echoes the `SEQ` of the frame it confirms.
    std::uint16_t seq = 0;
    std::vector<std::byte> payload;

    friend bool operator==(const FlashFrame&, const FlashFrame&) = default;
};

/// Marks the start of a frame. Not a printable character, so it can never
/// begin, or appear inside, a telemetry text line.
inline constexpr std::byte kFlashStartByte{0xA5};

/// Largest payload a frame may carry: a 4-byte offset plus 256 image bytes
/// in a `DATA` frame. A `LEN` above this marks a false start.
inline constexpr std::size_t kFlashMaxPayloadBytes = 260;

/// Bytes before the payload: start byte, TYPE, SEQ (2), LEN (2).
inline constexpr std::size_t kFlashHeaderBytes = 6;

/// Bytes after the payload: the CRC-16.
inline constexpr std::size_t kFlashTrailerBytes = 2;

/// CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no
/// reflection, no final XOR. Its standard check value over the ASCII
/// string "123456789" is 0x29B1, which the tests reproduce.
[[nodiscard]] std::uint16_t crc16CcittFalse(std::span<const std::byte> bytes);

/// Serialise a frame to the exact bytes that go on the wire.
///
/// @throws std::length_error if the payload is longer than
///         `kFlashMaxPayloadBytes`. That is a programming error in the
///         caller, never a wire condition.
[[nodiscard]] std::vector<std::byte> encodeFlashFrame(const FlashFrame& frame);

}  // namespace app::integration
