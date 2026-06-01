// [utest->req~perf-001~1]
// Covers REQ-PERF-001 (reproducible microbenchmarks on hot paths).
//
// Benchmark: Modbus TCP PDU codec.
//
// Why this matters: a Modbus master polling N slaves at 100ms cycle
// times produces 10N encode + decode pairs per second. At 50 slaves
// and three function-code variants per slave that's ~1.5k codec
// roundtrips/s -- still well below saturation on any modern CPU, but
// the per-call cost has to be small enough that the poll loop's
// budget is dominated by socket I/O, not by the codec.
//
// We benchmark the encode and decode paths separately and at
// three quantity points (1 / 10 / 125) so the cost-per-register
// curve is visible. The checked variant is included to quantify
// the cost of the validation step -- which CI flags as the
// recommended entry point on the config path.

#include <benchmark/benchmark.h>

#include "src/integration/modbus/ModbusPdu.h"

#include <cstdint>
#include <vector>

namespace mb = app::integration::modbus;

namespace {

/// Build a synthetic Read Holding Registers response of the given
/// quantity. Used to feed the decode benchmark a realistic ADU
/// without dragging in a socket.
std::vector<std::byte> makeReadResponse(std::uint16_t transactionId,
                                        std::uint8_t unitId,
                                        std::uint16_t quantity) {
    // MBAP header (7) + FC (1) + byte count (1) + payload (2*qty)
    const std::size_t pduSize = 2U + 2U * quantity;
    const std::size_t total   = mb::kMbapHeaderSize + pduSize;
    std::vector<std::byte> adu(total);

    // MBAP
    adu[0] = std::byte((transactionId >> mb::kBitsPerByte) & mb::kLowByteMask);
    adu[1] = std::byte(transactionId & mb::kLowByteMask);
    adu[2] = std::byte(0);  // protocol id high
    adu[3] = std::byte(0);  // protocol id low
    const std::uint16_t lengthField =
        static_cast<std::uint16_t>(pduSize + 1U);  // +1 for unit id
    adu[4] = std::byte((lengthField >> mb::kBitsPerByte) & mb::kLowByteMask);
    adu[5] = std::byte(lengthField & mb::kLowByteMask);
    adu[6] = std::byte(unitId);

    // PDU
    adu[7] = std::byte(static_cast<std::uint8_t>(
        mb::FunctionCode::ReadHoldingRegisters));
    adu[8] = std::byte(2U * quantity);

    // Payload: ascending register values (0, 1, 2, ...) -- arbitrary
    // but deterministic so the decoder's byte-count + payload path is
    // exercised in full.
    for (std::uint16_t i = 0; i < quantity; ++i) {
        adu[9U + 2U * i]     = std::byte((i >> mb::kBitsPerByte) & mb::kLowByteMask);
        adu[9U + 2U * i + 1U] = std::byte(i & mb::kLowByteMask);
    }
    return adu;
}

}  // namespace

// ---------------------------------------------------------------------
// encodeReadRequest -- unchecked, hot-loop path.
// ---------------------------------------------------------------------
static void BM_Modbus_EncodeReadRequest(benchmark::State& state) {
    const auto qty = static_cast<std::uint16_t>(state.range(0));
    for (auto _ : state) {
        auto adu = mb::encodeReadRequest(
            /*transactionId=*/0x1234,
            /*unitId=*/1,
            mb::FunctionCode::ReadHoldingRegisters,
            /*startAddress=*/0x0100,
            qty);
        benchmark::DoNotOptimize(adu);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("qty=" + std::to_string(qty));
}
BENCHMARK(BM_Modbus_EncodeReadRequest)
    ->Arg(1)->Arg(10)->Arg(125)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5);

// ---------------------------------------------------------------------
// encodeReadRequestChecked -- adds quantity-range validation. Compare
// against the unchecked variant to quantify the cost of the safety net
// at the config-driven entry points.
// ---------------------------------------------------------------------
static void BM_Modbus_EncodeReadRequestChecked(benchmark::State& state) {
    const auto qty = static_cast<std::uint16_t>(state.range(0));
    for (auto _ : state) {
        auto r = mb::encodeReadRequestChecked(
            /*transactionId=*/0x1234,
            /*unitId=*/1,
            mb::FunctionCode::ReadHoldingRegisters,
            /*startAddress=*/0x0100,
            qty);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("qty=" + std::to_string(qty));
}
BENCHMARK(BM_Modbus_EncodeReadRequestChecked)
    ->Arg(1)->Arg(10)->Arg(125)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5);

// ---------------------------------------------------------------------
// decodeReadResponse -- the hot half of the poll-loop budget. Parsing
// scales with payload size, so the curve from qty=1 to qty=125 tells
// you the marginal cost per register.
// ---------------------------------------------------------------------
static void BM_Modbus_DecodeReadResponse(benchmark::State& state) {
    const auto qty = static_cast<std::uint16_t>(state.range(0));
    const auto adu = makeReadResponse(0x1234, 1, qty);
    const mb::ResponseContext ctx{
        /*expectedTransactionId=*/0x1234,
        /*expectedUnitId=*/1,
        mb::FunctionCode::ReadHoldingRegisters,
        /*expectedQuantity=*/qty,
    };
    for (auto _ : state) {
        auto r = mb::decodeReadResponse(adu, ctx);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("qty=" + std::to_string(qty));
}
BENCHMARK(BM_Modbus_DecodeReadResponse)
    ->Arg(1)->Arg(10)->Arg(125)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5);

BENCHMARK_MAIN();
