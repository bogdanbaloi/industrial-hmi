// [utest->req~integration-006~1]
// Covers REQ-INTEGRATION-006 (wire parsers robust to adversarial input).
//
// Fuzz harness: app::integration::mqtt::parsePublish
//
// The PROPERTY under fuzz: an arbitrary byte sequence delivered as a
// pseudo-PUBLISH frame must NOT crash, leak, or produce UB. The parser
// is allowed to throw any exception type (the production caller wraps
// it in a try/catch and downgrades to "drop the frame, log a warning").
// What we are NOT allowed: out-of-bounds reads, signed-overflow UB,
// allocator misuse on truncated length prefixes.
//
// Seed corpus: a small set of valid PUBLISH bytes captured from the
// production MQTT integration tests.

#include "src/integration/MqttPacket.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mqtt = app::integration::mqtt;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    // parsePublish takes std::vector<std::uint8_t> by const-ref. Copy
    // is fine -- libFuzzer's cost model dwarfs a small memcpy, and a
    // const-vector input keeps the fuzz target API exactly what
    // production callers use.
    const std::vector<std::uint8_t> bytes(data, data + size);
    try {
        auto parsed = mqtt::parsePublish(bytes);
        // Touch the output so the optimiser cannot elide the call.
        if (parsed.topic.empty() && parsed.payload.empty()) {
            // no-op
        }
    } catch (const std::exception&) {
        // Expected on malformed input -- production code catches and
        // drops the frame. The bug we're hunting is a SILENT memory
        // defect, which sanitizer-instrumented runs surface as an
        // ASan / UBSan stop, not as a caught exception.
    } catch (...) {
        // Some std lib paths throw non-std exceptions; still acceptable
        // here -- a thrown anything is by definition not a crash.
    }
    return 0;
}
