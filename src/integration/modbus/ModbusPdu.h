#pragma once

#include "src/core/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace app::integration::modbus {

/// Pure mechanism: Modbus TCP (MBAP) frame encode + decode.
///
/// Lives at the bottom of the modbus stack -- no socket, no thread, no
/// domain. Just bytes in, bytes out. The transport layer
/// (ModbusClient) drives sockets and calls these; the domain layer
/// (ModbusIngestBridge) never touches this file.
///
/// SOLID:
///   * S -- one job: MBAP/PDU byte layout. No I/O, no error logging
///     beyond returning a typed Result.
///   * O -- new function codes land as new encode / decode overloads;
///     existing ones stay binary-stable on the wire.
///   * D -- the rest of the stack depends on these free functions
///     (no abstraction needed -- byte layout is fixed by the spec).
///
/// Wire layout (Modbus Application Protocol v1.1b3, section 4.1 + 6):
///
///   ADU = MBAP header (7 bytes) || PDU (1..253 bytes)
///
///   MBAP header
///     [0..1]  Transaction ID  (big-endian, echoed in response)
///     [2..3]  Protocol ID     (always 0x0000 for Modbus TCP)
///     [4..5]  Length          (bytes after this field, includes unit
///                             ID + PDU)
///     [6]     Unit ID         (slave address, 1..247; 0 = broadcast,
///                             not used by this codec)
///
///   PDU (read request)
///     [0]     Function code   (0x03 = read holding, 0x04 = read input)
///     [1..2]  Start address   (big-endian)
///     [3..4]  Quantity        (big-endian, 1..125)
///
///   PDU (read response)
///     [0]     Function code   (echoed)
///     [1]     Byte count      (N * 2)
///     [2..]   Register values (each 16-bit big-endian)
///
///   PDU (exception response)
///     [0]     Function code | 0x80
///     [1]     Exception code  (0x01..0x04 most common)
///
/// All multi-byte fields are big-endian (network order). This codec
/// uses portable shift/mask, not platform byte-swap intrinsics.

/// MBAP header size in bytes.
inline constexpr std::size_t kMbapHeaderSize = 7;

/// Maximum PDU size per the Modbus TCP spec (section 4.1).
inline constexpr std::size_t kMaxPduSize = 253;

/// Maximum ADU size: MBAP header + max PDU.
inline constexpr std::size_t kMaxAduSize = kMbapHeaderSize + kMaxPduSize;

/// Protocol ID for Modbus TCP. Always zero per section 4.1.
inline constexpr std::uint16_t kModbusTcpProtocolId = 0x0000;

/// Top-bit flag OR'd onto the function code in an exception response
/// (section 7). Strips out to the original FC by AND with 0x7F.
inline constexpr std::uint8_t kExceptionFlag = 0x80;

/// Spec-mandated upper bound on registers per read (section 6.3 / 6.4).
inline constexpr std::uint16_t kMaxRegistersPerRead = 125;

/// Subset of Modbus function codes this codec handles. Outbound (FC06
/// write single register) lives in a follow-up; the inbound MVP only
/// needs the two read codes.
enum class FunctionCode : std::uint8_t {
    ReadHoldingRegisters = 0x03,  ///< FC03 -- RW registers
    ReadInputRegisters   = 0x04,  ///< FC04 -- R-only registers
};

/// Modbus exception codes (section 7). Only the four common ones are
/// listed -- anything else falls through to `Other` so the decoder
/// can still produce a meaningful error without owning a complete
/// table.
enum class ExceptionCode : std::uint8_t {
    IllegalFunction     = 0x01,
    IllegalDataAddress  = 0x02,
    IllegalDataValue    = 0x03,
    SlaveDeviceFailure  = 0x04,
    Other               = 0xFF,
};

/// Decoder failure modes. Each maps to a specific class of bug or
/// remote misbehaviour so logs and tests can assert on the kind, not
/// on a free-form string.
enum class DecodeError : std::uint8_t {
    ShortFrame,             ///< ADU smaller than MBAP header / PDU min
    BadProtocolId,          ///< MBAP protocol ID != 0
    LengthMismatch,         ///< MBAP length field disagrees with ADU
    TransactionIdMismatch,  ///< Response TID != request TID
    UnitIdMismatch,         ///< Response unit != request unit
    FunctionCodeMismatch,   ///< Response FC != request FC (and not
                            ///< an exception)
    ByteCountMismatch,      ///< PDU byte count != 2 * expected qty
    ServerException,        ///< Remote returned exception PDU; see
                            ///< the out-param for the code
    InvalidQuantity,        ///< Request quantity 0 or > 125
};

/// Encode a Read Holding Registers (FC03) or Read Input Registers
/// (FC04) request as a full ADU ready to write to the socket.
///
/// @param transactionId  Echoed in the response. Caller picks per
///                       outstanding request so multiple in-flight
///                       requests can be demultiplexed.
/// @param unitId         Slave address (1..247 typical; 0 = broadcast
///                       which makes no sense for reads).
/// @param fc             Read function code.
/// @param startAddress   First register address.
/// @param quantity       Register count (1..125 per spec).
///
/// Pre: quantity in [1, kMaxRegistersPerRead]. The function does not
/// validate -- callers go through `encodeReadRequestChecked` for that.
[[nodiscard]] std::vector<std::byte> encodeReadRequest(
    std::uint16_t transactionId,
    std::uint8_t  unitId,
    FunctionCode  fc,
    std::uint16_t startAddress,
    std::uint16_t quantity);

/// Same as encodeReadRequest, but validates the quantity range and
/// returns InvalidQuantity instead of producing a malformed frame.
/// Use this on the call path from configuration; use the unchecked
/// variant in hot loops where the caller already validated.
[[nodiscard]] app::core::Result<std::vector<std::byte>, DecodeError>
encodeReadRequestChecked(std::uint16_t transactionId,
                         std::uint8_t  unitId,
                         FunctionCode  fc,
                         std::uint16_t startAddress,
                         std::uint16_t quantity);

/// Context the decoder needs to validate a response. Captured from
/// the matching request so the codec is stateless and reentrant.
struct ResponseContext {
    std::uint16_t expectedTransactionId{0};
    std::uint8_t  expectedUnitId{0};
    FunctionCode  expectedFunctionCode{FunctionCode::ReadHoldingRegisters};
    std::uint16_t expectedQuantity{0};
};

/// Decoder output. On ServerException, `registers` is empty and the
/// exception code lives in `exceptionCode`.
struct DecodedReadResponse {
    std::vector<std::uint16_t> registers;
    ExceptionCode              exceptionCode{ExceptionCode::Other};
};

/// Parse a complete ADU. Validates MBAP framing, length, transaction
/// ID echo, unit ID echo, function code, byte count, then extracts
/// the register payload. On exception response, returns
/// ServerException and populates `exceptionCode` via the out-param.
[[nodiscard]] app::core::Result<DecodedReadResponse, DecodeError>
decodeReadResponse(std::span<const std::byte> adu,
                   const ResponseContext& ctx);

}  // namespace app::integration::modbus
