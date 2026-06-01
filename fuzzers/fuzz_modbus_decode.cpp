// [utest->req~integration-006~1]
// Covers REQ-INTEGRATION-006 (wire parsers robust to adversarial input).
//
// Fuzz harness: app::integration::modbus::decodeReadResponse
//
// The PROPERTY under fuzz: a misbehaving PLC (or man-in-the-middle on
// the Modbus TCP link) sending arbitrary bytes must NOT crash the HMI.
// The decoder is allowed to return any DecodeError code -- we are
// asserting only the absence of memory-safety / UB defects.
//
// What we mutate:
//   - The full ADU byte stream (`data`)
//   - The ResponseContext expectation (synthesized from the first 5
//     bytes of input, so the fuzzer can explore mismatched
//     transaction IDs / unit IDs / function codes / quantities)
//
// Why we synthesize ctx rather than fixing it: a fuzzer feeding
// arbitrary ADUs against a single fixed ctx never explores the
// `TransactionIdMismatch` / `UnitIdMismatch` decision branches.
// Letting the first 5 bytes drive the ctx covers those branches
// without the harness scripting a state machine.
//
// Seed corpus: `fuzzers/corpus/modbus_decode/` -- a handful of valid
// MBAP/PDU framings derived from the production unit tests.

#include "src/integration/modbus/ModbusPdu.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace mb = app::integration::modbus;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    // Synthesize a ResponseContext from the first 5 bytes so the fuzzer
    // can drive the validation branches; fall back to a fixed ctx for
    // very short inputs (which exercise the ShortFrame path).
    mb::ResponseContext ctx{};
    if (size >= 5) {
        ctx.expectedTransactionId =
            static_cast<std::uint16_t>(data[0]) << 8 | data[1];
        ctx.expectedUnitId       = data[2];
        ctx.expectedFunctionCode =
            (data[3] & 0x01) ? mb::FunctionCode::ReadInputRegisters
                             : mb::FunctionCode::ReadHoldingRegisters;
        // Bound quantity by spec max so the harness doesn't shadow real
        // decoder bugs with InvalidQuantity short-circuits.
        ctx.expectedQuantity =
            static_cast<std::uint16_t>(data[4]) % mb::kMaxRegistersPerRead;
        if (ctx.expectedQuantity == 0) ctx.expectedQuantity = 1;
    }

    // Feed the full input as the ADU. The decoder validates MBAP
    // length, transaction-id echo, unit-id echo, function code, byte
    // count -- any path that doesn't crash / leak / UB is a pass.
    auto result = mb::decodeReadResponse(
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data), size),
        ctx);
    (void)result;
    return 0;
}
