#include "src/integration/FlashFrameParser.h"

#include <cstdint>
#include <utility>

namespace app::integration {

namespace {
/// Field offsets inside a frame, counted from the start byte.
constexpr std::size_t kTypeOffset = 1;
constexpr std::size_t kSeqOffset  = 2;
constexpr std::size_t kLenOffset  = 4;
constexpr unsigned    kBitsPerByte = 8U;

/// Read a little-endian 16-bit field at `offset`. Combined as `unsigned`,
/// because a std::uint16_t operand would be promoted to signed int before
/// the shift (hicpp-signed-bitwise).
[[nodiscard]] std::uint16_t readLittleEndian(const std::vector<std::byte>& buf,
                                             std::size_t offset) {
    const auto low  = std::to_integer<unsigned>(buf[offset]);
    const auto high = std::to_integer<unsigned>(buf[offset + 1]);
    return static_cast<std::uint16_t>(low | (high << kBitsPerByte));
}
}  // namespace

FlashStreamOutput FlashFrameParser::consume(std::span<const std::byte> bytes) {
    FlashStreamOutput out;

    // The held bytes, if any, are one unfinished frame. New bytes go after.
    std::vector<std::byte> work = std::move(buffer_);
    buffer_.clear();
    work.insert(work.end(), bytes.begin(), bytes.end());

    std::size_t pos = 0;
    while (pos < work.size()) {
        if (work[pos] != kFlashStartByte) {
            // Outside a frame every byte is telemetry text.
            out.text.push_back(static_cast<char>(work[pos]));
            ++pos;
            continue;
        }

        const std::size_t available = work.size() - pos;
        if (available < kFlashHeaderBytes) {
            break;  // header not complete yet: wait for the next chunk
        }
        const std::size_t len = readLittleEndian(work, pos + kLenOffset);
        if (len > kFlashMaxPayloadBytes) {
            // Impossible length: this 0xA5 was not a frame start. Drop it
            // and look again from the very next byte (never skip LEN).
            ++falseStarts_;
            ++pos;
            continue;
        }
        const std::size_t total = kFlashHeaderBytes + len + kFlashTrailerBytes;
        if (available < total) {
            break;  // payload or CRC not complete yet
        }

        const std::size_t crcOffset = pos + kFlashHeaderBytes + len;
        const std::uint16_t received = readLittleEndian(work, crcOffset);
        // The CRC covers everything after the start byte, up to itself.
        const std::uint16_t computed = crc16CcittFalse(
            std::span<const std::byte>(work).subspan(pos + kTypeOffset,
                                                     crcOffset - pos - 1));
        if (received != computed) {
            ++falseStarts_;  // same resync rule as an impossible LEN
            ++pos;
            continue;
        }

        FlashFrame frame;
        frame.type = std::to_integer<std::uint8_t>(work[pos + kTypeOffset]);
        frame.seq  = readLittleEndian(work, pos + kSeqOffset);
        const auto payloadBegin =
            work.begin() + static_cast<std::ptrdiff_t>(pos + kFlashHeaderBytes);
        frame.payload.assign(payloadBegin,
                             payloadBegin + static_cast<std::ptrdiff_t>(len));
        out.frames.push_back(std::move(frame));
        pos += total;
    }

    // Whatever is left starts with 0xA5 and is one frame still arriving.
    buffer_.assign(work.begin() + static_cast<std::ptrdiff_t>(pos), work.end());
    return out;
}

}  // namespace app::integration
