// [utest->req~integration-006~1]
// Covers REQ-INTEGRATION-006 (wire parsers robust to adversarial input).
//
// Fuzz harness: app::integration::mqtt::decodeRemainingLength
//
// The variable-byte length encoding (MQTT 3.1.1 sec 2.2.3) is the
// smallest-surface, highest-frequency parser in the codebase. Every
// MQTT packet hits it first. A bug here corrupts every subsequent
// parser's notion of "how many bytes follow"; worst case it lets a
// truncated frame read past the buffer.
//
// We feed arbitrary bytes + an arbitrary starting cursor (taken from
// the input's last byte modulo size) so the fuzzer explores both
// "fresh decode" and "decode that starts mid-buffer" -- the cursor
// is also an output parameter, so we never use it after the call.

#include "src/integration/MqttPacket.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mqtt = app::integration::mqtt;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    if (size == 0) return 0;
    const std::vector<std::uint8_t> bytes(data, data + size);

    // Pick a cursor inside the buffer. The decoder spec allows up to
    // 4 bytes of continuation, so any starting cursor in [0, size-1]
    // exercises a realistic decode start.
    std::size_t cursor = bytes.back() % size;

    try {
        const std::uint32_t value =
            mqtt::decodeRemainingLength(bytes, cursor);
        (void)value;
        (void)cursor;
    } catch (const std::exception&) {
        // Throws on >4 bytes with continuation bit still set, or on
        // truncated buffer. Expected; ignored.
    } catch (...) {
        // see fuzz_mqtt_publish for the rationale.
    }
    return 0;
}
