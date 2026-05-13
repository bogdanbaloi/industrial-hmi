// Tests for app::integration::modbus::ModbusPdu free functions.
//
// Pure encode/decode -- no socket, no thread. Each test is an array
// of bytes built by hand against the Modbus spec, fed through the
// codec, then asserted on. The byte values are the receipts: a
// reviewer can cross-check against section 4.1 / 6.3 / 6.4 / 7 of
// Modbus Application Protocol v1.1b3 without running anything.

#include "src/integration/modbus/ModbusPdu.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using app::integration::modbus::DecodedReadResponse;
using app::integration::modbus::DecodeError;
using app::integration::modbus::decodeReadResponse;
using app::integration::modbus::encodeReadRequest;
using app::integration::modbus::encodeReadRequestChecked;
using app::integration::modbus::ExceptionCode;
using app::integration::modbus::FunctionCode;
using app::integration::modbus::kExceptionFlag;
using app::integration::modbus::kMaxRegistersPerRead;
using app::integration::modbus::ResponseContext;

namespace {

// Tiny helper -- build a byte buffer from an initialiser list of
// uint8_t. Keeps test bodies dense and readable.
std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> raw) {
    std::vector<std::byte> out;
    out.reserve(raw.size());
    for (auto b : raw) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

}  // namespace

// ----- encode -----------------------------------------------------

TEST(ModbusPduTest, EncodeFc03ReadHoldingRegistersMatchesSpec) {
    // Read 10 holding registers starting at 0x0000, unit 17 (0x11),
    // TID 0x0001. Reference frame from the spec walk-through.
    const auto adu = encodeReadRequest(0x0001, 0x11,
                                       FunctionCode::ReadHoldingRegisters,
                                       0x0000, 0x000A);
    const auto expected = bytes({
        0x00, 0x01,        // Transaction ID
        0x00, 0x00,        // Protocol ID
        0x00, 0x06,        // Length = unit + PDU = 1 + 5
        0x11,              // Unit ID
        0x03,              // FC
        0x00, 0x00,        // Start address
        0x00, 0x0A,        // Quantity
    });
    EXPECT_EQ(adu, expected);
}

TEST(ModbusPduTest, EncodeFc04ReadInputRegistersUsesFunctionCode04) {
    const auto adu = encodeReadRequest(0xFFFE, 0x07,
                                       FunctionCode::ReadInputRegisters,
                                       0x1234, 0x0001);
    // FC byte is at offset 7.
    EXPECT_EQ(static_cast<std::uint8_t>(adu[7]), 0x04U);
    // Start address big-endian at offset 8..9.
    EXPECT_EQ(static_cast<std::uint8_t>(adu[8]), 0x12U);
    EXPECT_EQ(static_cast<std::uint8_t>(adu[9]), 0x34U);
}

TEST(ModbusPduTest, EncodeRequestIsAlways12Bytes) {
    // FC03/FC04 read request ADU is always 7 (MBAP) + 5 (PDU).
    EXPECT_EQ(encodeReadRequest(1, 1, FunctionCode::ReadHoldingRegisters,
                                0, 1).size(),
              12U);
    EXPECT_EQ(encodeReadRequest(0xFFFF, 247,
                                FunctionCode::ReadInputRegisters,
                                0xFFFF, kMaxRegistersPerRead).size(),
              12U);
}

TEST(ModbusPduTest, EncodeCheckedRejectsZeroQuantity) {
    auto r = encodeReadRequestChecked(1, 1,
                                      FunctionCode::ReadHoldingRegisters,
                                      0, 0);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::InvalidQuantity);
}

TEST(ModbusPduTest, EncodeCheckedRejectsQuantityAbove125) {
    auto r = encodeReadRequestChecked(1, 1,
                                      FunctionCode::ReadHoldingRegisters,
                                      0, 126);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::InvalidQuantity);
}

TEST(ModbusPduTest, EncodeCheckedAcceptsQuantityAt125) {
    auto r = encodeReadRequestChecked(1, 1,
                                      FunctionCode::ReadHoldingRegisters,
                                      0, kMaxRegistersPerRead);
    ASSERT_TRUE(r.isOk());
    EXPECT_EQ(r.unwrap().size(), 12U);
}

// ----- decode happy paths ----------------------------------------

TEST(ModbusPduTest, DecodeFc03TwoRegistersHappyPath) {
    // Response to "read 2 holding regs". Registers = {0xAA55, 0x1234}.
    const auto adu = bytes({
        0x00, 0x01,             // TID
        0x00, 0x00,             // Protocol ID
        0x00, 0x07,             // Length = 1 (unit) + 1 (fc) + 1 (cnt) + 4 (regs)
        0x11,                   // Unit ID
        0x03,                   // FC echo
        0x04,                   // Byte count = 4
        0xAA, 0x55,             // reg[0]
        0x12, 0x34,             // reg[1]
    });
    ResponseContext ctx;
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 2;

    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isOk()) << "errorMessage=" << r.errorMessage();
    const auto& payload = r.unwrap();
    ASSERT_EQ(payload.registers.size(), 2U);
    EXPECT_EQ(payload.registers[0], 0xAA55U);
    EXPECT_EQ(payload.registers[1], 0x1234U);
}

TEST(ModbusPduTest, DecodeFc04SingleRegister) {
    const auto adu = bytes({
        0x00, 0x42,
        0x00, 0x00,
        0x00, 0x05,             // 1 + 1 + 1 + 2
        0x07,
        0x04,                   // FC04 echo
        0x02,                   // 2 bytes
        0x00, 0x2A,             // 42 decimal
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0042;
    ctx.expectedUnitId        = 0x07;
    ctx.expectedFunctionCode  = FunctionCode::ReadInputRegisters;
    ctx.expectedQuantity      = 1;

    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isOk());
    ASSERT_EQ(r.unwrap().registers.size(), 1U);
    EXPECT_EQ(r.unwrap().registers[0], 42U);
}

// ----- decode error paths ----------------------------------------

TEST(ModbusPduTest, DecodeShortFrameRejected) {
    auto r = decodeReadResponse(bytes({0x00, 0x01, 0x00, 0x00}),
                                ResponseContext{});
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::ShortFrame);
}

TEST(ModbusPduTest, DecodeBadProtocolIdRejected) {
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x01,             // protocol != 0
        0x00, 0x05,
        0x11,
        0x03,
        0x02,
        0x00, 0x00,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 1;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::BadProtocolId);
}

TEST(ModbusPduTest, DecodeTransactionIdMismatch) {
    const auto adu = bytes({
        0x00, 0x99,             // wrong TID
        0x00, 0x00,
        0x00, 0x05,
        0x11,
        0x03,
        0x02,
        0x00, 0x00,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::TransactionIdMismatch);
}

TEST(ModbusPduTest, DecodeUnitIdMismatch) {
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x05,
        0x22,                   // wrong unit
        0x03, 0x02, 0x00, 0x00,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::UnitIdMismatch);
}

TEST(ModbusPduTest, DecodeFunctionCodeMismatchOnNormalResponse) {
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x05,
        0x11,
        0x04,                   // FC04 but we asked FC03
        0x02, 0x00, 0x00,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::FunctionCodeMismatch);
}

TEST(ModbusPduTest, DecodeByteCountMismatch) {
    // Asked for 2 regs (expect 4 bytes), got byte count = 2.
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x05,
        0x11,
        0x03,
        0x02,                   // byte count = 2, but qty * 2 = 4
        0x00, 0x00,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 2;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::ByteCountMismatch);
}

TEST(ModbusPduTest, DecodeLengthMismatchOnTruncatedAdu) {
    // Length field says 7 bytes after but we only ship 5.
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x07,             // says 7
        0x11,
        0x03,
        0x02,                   // truncated PDU
        0x00, 0x00,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 2;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::LengthMismatch);
}

// ----- exception response (FC | 0x80) ----------------------------

TEST(ModbusPduTest, DecodeServerExceptionReturnsOkWithExceptionCode) {
    // FC03 request that the slave rejects with IllegalDataAddress.
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x03,             // unit + 2-byte PDU
        0x11,
        static_cast<std::uint8_t>(0x03 | kExceptionFlag),  // 0x83
        0x02,                   // IllegalDataAddress
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    // Codec contract: server exception comes back as Ok with empty
    // registers and a populated exceptionCode. This lets callers
    // handle a remote rejection without conflating it with a
    // local framing error (which IS reported as Err).
    ASSERT_TRUE(r.isOk()) << "errorMessage=" << r.errorMessage();
    const auto& payload = r.unwrap();
    EXPECT_TRUE(payload.registers.empty());
    EXPECT_EQ(payload.exceptionCode, ExceptionCode::IllegalDataAddress);
}

TEST(ModbusPduTest, DecodeExceptionFlagWithWrongFcStillRejected) {
    // 0x84 = FC04 exception, but we sent FC03. Still a mismatch.
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x03,
        0x11,
        0x84,                   // FC04 | exception flag
        0x02,
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), DecodeError::FunctionCodeMismatch);
}

TEST(ModbusPduTest, DecodeUnknownExceptionCodeMapsToOther) {
    const auto adu = bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x03,
        0x11,
        0x83,
        0x99,                   // not in 0x01..0x04
    });
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 1;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isOk());
    EXPECT_EQ(r.unwrap().exceptionCode, ExceptionCode::Other);
}

// ----- round-trip on a 125-register response ---------------------

TEST(ModbusPduTest, DecodeMaxRegistersBoundary) {
    // 125 regs * 2 bytes = 250 bytes payload, plus FC + count = 252
    // PDU bytes. ADU = 7 + 252 = 259 bytes. MBAP length = 1 + 252 = 253.
    std::vector<std::byte> adu;
    adu.reserve(259);
    auto push = [&](std::uint8_t b) {
        adu.push_back(static_cast<std::byte>(b));
    };
    push(0x00); push(0x01);
    push(0x00); push(0x00);
    push(0x00); push(0xFD);                   // length = 253
    push(0x11);
    push(0x03);
    push(0xFA);                                // byte count = 250
    for (int i = 0; i < 125; ++i) {
        push(static_cast<std::uint8_t>((i >> 0) & 0xFFU));
        push(static_cast<std::uint8_t>((i >> 8) & 0xFFU));
    }
    ResponseContext ctx{};
    ctx.expectedTransactionId = 0x0001;
    ctx.expectedUnitId        = 0x11;
    ctx.expectedFunctionCode  = FunctionCode::ReadHoldingRegisters;
    ctx.expectedQuantity      = 125;
    auto r = decodeReadResponse(adu, ctx);
    ASSERT_TRUE(r.isOk()) << "errorMessage=" << r.errorMessage();
    EXPECT_EQ(r.unwrap().registers.size(), 125U);
    // First reg low byte is 0, high byte is 0 -> value 0.
    EXPECT_EQ(r.unwrap().registers[0], 0U);
    // reg[1]: low=1, high=0 -> big-endian read = (1 << 8) | 0 = 256.
    EXPECT_EQ(r.unwrap().registers[1], 256U);
}
