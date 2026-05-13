#include "src/integration/modbus/ModbusPdu.h"

namespace app::core {

/// Diagnostic strings for ModbusPdu decoder failures. Routed through
/// Result<int, E>::errorToString per the Result<T,E> contract: every
/// Result instantiation over DecodeError delegates here regardless of
/// the success type, so a single definition covers all callers.
template<>
std::string Result<int, app::integration::modbus::DecodeError>::errorToString(
        app::integration::modbus::DecodeError error) {
    using enum app::integration::modbus::DecodeError;
    switch (error) {
        case ShortFrame:
            return "Modbus: ADU shorter than minimum frame size";
        case BadProtocolId:
            return "Modbus: MBAP protocol ID is not zero";
        case LengthMismatch:
            return "Modbus: MBAP length field disagrees with ADU size";
        case TransactionIdMismatch:
            return "Modbus: response transaction ID does not echo request";
        case UnitIdMismatch:
            return "Modbus: response unit ID does not echo request";
        case FunctionCodeMismatch:
            return "Modbus: response function code does not echo request";
        case ByteCountMismatch:
            return "Modbus: PDU byte count does not match requested quantity";
        case ServerException:
            return "Modbus: server returned exception response";
        case InvalidQuantity:
            return "Modbus: register quantity outside [1, 125]";
    }
    return "Modbus: unknown decode error";
}

}  // namespace app::core

namespace app::integration::modbus {

namespace {

/// Big-endian 16-bit write at the given offset. Portable -- no
/// htons / byteswap intrinsics so the codec compiles cleanly on every
/// platform the project supports.
void writeBe16(std::vector<std::byte>& buf,
               std::size_t offset,
               std::uint16_t value) {
    buf[offset    ] = static_cast<std::byte>((value >> 8) & 0xFFU);
    buf[offset + 1] = static_cast<std::byte>(value & 0xFFU);
}

/// Big-endian 16-bit read at the given offset. Caller has already
/// bounds-checked.
[[nodiscard]] std::uint16_t readBe16(std::span<const std::byte> buf,
                                     std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(buf[offset    ]) << 8) |
         static_cast<std::uint16_t>(buf[offset + 1]));
}

/// Map a raw exception byte to the typed enum, falling through to
/// Other for anything outside the four common codes. Keeps the
/// decoder's switch / table small.
[[nodiscard]] ExceptionCode mapExceptionCode(std::uint8_t raw) {
    using enum ExceptionCode;
    switch (raw) {
        case 0x01: return IllegalFunction;
        case 0x02: return IllegalDataAddress;
        case 0x03: return IllegalDataValue;
        case 0x04: return SlaveDeviceFailure;
        default:   return Other;
    }
}

}  // namespace

std::vector<std::byte> encodeReadRequest(std::uint16_t transactionId,
                                         std::uint8_t  unitId,
                                         FunctionCode  fc,
                                         std::uint16_t startAddress,
                                         std::uint16_t quantity) {
    // Total ADU: 7 (MBAP) + 5 (PDU = FC + start + qty) = 12 bytes.
    constexpr std::size_t kReadRequestPduSize  = 5;
    constexpr std::size_t kReadRequestAduSize  = kMbapHeaderSize +
                                                 kReadRequestPduSize;
    // MBAP length field counts unit ID + PDU = 1 + 5 = 6.
    constexpr std::uint16_t kReadRequestLength = 6;

    std::vector<std::byte> adu(kReadRequestAduSize);

    // MBAP header.
    writeBe16(adu, 0, transactionId);
    writeBe16(adu, 2, kModbusTcpProtocolId);
    writeBe16(adu, 4, kReadRequestLength);
    adu[6] = static_cast<std::byte>(unitId);

    // PDU.
    adu[7] = static_cast<std::byte>(static_cast<std::uint8_t>(fc));
    writeBe16(adu,  8, startAddress);
    writeBe16(adu, 10, quantity);

    return adu;
}

app::core::Result<std::vector<std::byte>, DecodeError>
encodeReadRequestChecked(std::uint16_t transactionId,
                         std::uint8_t  unitId,
                         FunctionCode  fc,
                         std::uint16_t startAddress,
                         std::uint16_t quantity) {
    using Res = app::core::Result<std::vector<std::byte>, DecodeError>;
    if (quantity == 0 || quantity > kMaxRegistersPerRead) {
        return Res(app::core::Err, DecodeError::InvalidQuantity);
    }
    return Res(app::core::Ok,
               encodeReadRequest(transactionId, unitId, fc,
                                 startAddress, quantity));
}

app::core::Result<DecodedReadResponse, DecodeError>
decodeReadResponse(std::span<const std::byte> adu,
                   const ResponseContext& ctx) {
    using Res = app::core::Result<DecodedReadResponse, DecodeError>;
    using enum DecodeError;

    // Smallest legal response is an exception PDU: MBAP (7) + FC (1)
    // + exception code (1) = 9 bytes. Anything shorter cannot be
    // interpreted at all.
    constexpr std::size_t kMinResponseSize = kMbapHeaderSize + 2;
    if (adu.size() < kMinResponseSize) {
        return Res(app::core::Err, ShortFrame);
    }

    // --- MBAP header validation ----------------------------------
    const std::uint16_t transactionId = readBe16(adu, 0);
    const std::uint16_t protocolId    = readBe16(adu, 2);
    const std::uint16_t length        = readBe16(adu, 4);
    const std::uint8_t  unitId        = static_cast<std::uint8_t>(adu[6]);

    if (protocolId != kModbusTcpProtocolId) {
        return Res(app::core::Err, BadProtocolId);
    }
    if (transactionId != ctx.expectedTransactionId) {
        return Res(app::core::Err, TransactionIdMismatch);
    }
    if (unitId != ctx.expectedUnitId) {
        return Res(app::core::Err, UnitIdMismatch);
    }

    // MBAP length counts unit ID + PDU. Total ADU bytes after the
    // length field must therefore equal `length`. If less, the frame
    // is truncated; if more, the caller passed extra bytes.
    const std::size_t expectedAduSize =
        kMbapHeaderSize + static_cast<std::size_t>(length) - 1U;
    if (adu.size() != expectedAduSize) {
        return Res(app::core::Err, LengthMismatch);
    }

    // --- PDU dispatch --------------------------------------------
    const std::uint8_t rawFc = static_cast<std::uint8_t>(adu[7]);
    const auto expectedFcRaw =
        static_cast<std::uint8_t>(ctx.expectedFunctionCode);

    // Exception response: top bit set, original FC in the low 7 bits.
    if ((rawFc & kExceptionFlag) != 0) {
        if ((rawFc & 0x7FU) != expectedFcRaw) {
            return Res(app::core::Err, FunctionCodeMismatch);
        }
        DecodedReadResponse out;
        out.exceptionCode = mapExceptionCode(
            static_cast<std::uint8_t>(adu[8]));
        // Build the error result and stash the exception code inside.
        // The exception flavour is delivered via the error tag; the
        // caller checks `result.error() == ServerException` then
        // peeks at the response struct... but Result<T,E> doesn't
        // carry the T on the error path. Decode contract: on
        // ServerException, the *caller* must own the exceptionCode
        // through a different channel. To stay zero-allocation we
        // could thread an out-param; instead we keep the API
        // symmetric and return Ok with empty registers + populated
        // exceptionCode, then signal Err separately on the next
        // check. -- Chosen instead: return the response payload
        // wrapped in Err's sibling channel via a deliberate Ok with
        // empty registers + non-default exceptionCode. The codec
        // contract documents that callers MUST check exceptionCode
        // on every Ok response.
        //
        // Pragmatic choice: ServerException is rare and we want the
        // exception code available without an out-param, so the
        // codec contract is: Err == decoding/framing failure, while
        // a remote-rejected request returns Ok with empty registers
        // and a non-default exceptionCode field. Tests pin this.
        return Res(app::core::Ok, std::move(out));
    }

    if (rawFc != expectedFcRaw) {
        return Res(app::core::Err, FunctionCodeMismatch);
    }

    // Normal response PDU: [FC][byteCount][reg0_hi reg0_lo ...]
    const std::uint8_t byteCount = static_cast<std::uint8_t>(adu[8]);
    const std::uint16_t expectedBytes =
        static_cast<std::uint16_t>(ctx.expectedQuantity * 2U);
    if (byteCount != expectedBytes) {
        return Res(app::core::Err, ByteCountMismatch);
    }
    // The PDU must fit in the ADU: 1 (FC) + 1 (count) + byteCount.
    const std::size_t pduSize = 2U + static_cast<std::size_t>(byteCount);
    if (kMbapHeaderSize + pduSize != adu.size()) {
        return Res(app::core::Err, LengthMismatch);
    }

    DecodedReadResponse out;
    out.registers.reserve(ctx.expectedQuantity);
    for (std::uint16_t i = 0; i < ctx.expectedQuantity; ++i) {
        const std::size_t offset = kMbapHeaderSize + 2U +
                                   static_cast<std::size_t>(i) * 2U;
        out.registers.push_back(readBe16(adu, offset));
    }
    return Res(app::core::Ok, std::move(out));
}

}  // namespace app::integration::modbus
