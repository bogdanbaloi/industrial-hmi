#include "src/integration/FlashFrame.h"

#include <stdexcept>
#include <string>

namespace app::integration {

namespace {
// All bit arithmetic below runs on `unsigned`. A std::uint16_t operand is
// promoted to signed int before `<<`, `^` or `&`, which hicpp-signed-bitwise
// rejects, so the register is widened once and masked back to 16 bits.

/// CRC-16/CCITT-FALSE parameters (see the header).
constexpr unsigned kCrcPolynomial = 0x1021U;
constexpr unsigned kCrcInitial    = 0xFFFFU;
/// The CRC register's top bit, tested before each shift.
constexpr unsigned kCrcTopBit = 0x8000U;
/// Keeps the widened register at 16 bits after each shift.
constexpr unsigned kCrc16Mask   = 0xFFFFU;
constexpr unsigned kBitsPerByte = 8U;
/// Mask for the low byte of a 16-bit value.
constexpr unsigned kLowByteMask = 0x00FFU;

/// Append a 16-bit value low byte first, as the protocol requires.
void appendLittleEndian(std::vector<std::byte>& out, std::uint16_t value) {
    const unsigned wide = value;
    out.push_back(static_cast<std::byte>(wide & kLowByteMask));
    out.push_back(static_cast<std::byte>(wide >> kBitsPerByte));
}
}  // namespace

std::uint16_t crc16CcittFalse(std::span<const std::byte> bytes) {
    unsigned crc = kCrcInitial;
    for (const std::byte b : bytes) {
        crc ^= std::to_integer<unsigned>(b) << kBitsPerByte;
        for (unsigned bit = 0; bit < kBitsPerByte; ++bit) {
            crc = (crc & kCrcTopBit) != 0U ? ((crc << 1U) ^ kCrcPolynomial)
                                           : (crc << 1U);
            crc &= kCrc16Mask;
        }
    }
    return static_cast<std::uint16_t>(crc);
}

std::vector<std::byte> encodeFlashFrame(const FlashFrame& frame) {
    if (frame.payload.size() > kFlashMaxPayloadBytes) {
        throw std::length_error(
            "flash frame payload of " + std::to_string(frame.payload.size()) +
            " bytes exceeds the protocol maximum of " +
            std::to_string(kFlashMaxPayloadBytes));
    }
    std::vector<std::byte> out;
    out.reserve(kFlashHeaderBytes + frame.payload.size() + kFlashTrailerBytes);
    out.push_back(kFlashStartByte);
    out.push_back(static_cast<std::byte>(frame.type));
    appendLittleEndian(out, frame.seq);
    appendLittleEndian(out, static_cast<std::uint16_t>(frame.payload.size()));
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    // The CRC covers everything after the start byte, up to itself.
    const std::uint16_t crc =
        crc16CcittFalse(std::span<const std::byte>(out).subspan(1));
    appendLittleEndian(out, crc);
    return out;
}

}  // namespace app::integration
