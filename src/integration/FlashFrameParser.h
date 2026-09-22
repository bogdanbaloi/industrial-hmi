#pragma once

#include "src/integration/FlashFrame.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace app::integration {

/// What one `FlashFrameParser::consume()` call separated out of the stream.
struct FlashStreamOutput {
    /// Whole frames whose CRC matched, in arrival order.
    std::vector<FlashFrame> frames;
    /// Every byte that is not part of a frame, in arrival order. On the
    /// serial link that is the telemetry text, which the caller hands on
    /// to `SerialFrameParser` unchanged.
    std::string text;
};

/// Splits the serial byte stream into flash protocol frames and the
/// telemetry text around them. REQ-INTEGRATION-013, ADR-0033.
///
/// @rationale One wire carries two kinds of traffic: `sensorId,value\n`
/// text lines and binary flash frames. The start byte `0xA5` is what tells
/// them apart. It is not a printable character, so text never contains it.
/// Every byte outside a frame is text. A frame is read by its `LEN`, not
/// by a delimiter, so its binary payload may hold anything, a newline or a
/// second `0xA5` included. No mode switch is needed: telemetry and frames
/// may interleave at any time.
///
/// @logic Resync, the rule both sides of the protocol share: a frame whose CRC
/// does not match, or whose `LEN` exceeds `kFlashMaxPayloadBytes`, is a
/// false start. The parser drops only its `0xA5` and scans again from the
/// very next byte. It never skips `LEN` bytes, because a garbage `LEN` can
/// reach 65535 and would swallow the real frames behind it.
///
/// @capacity The buffer never holds more than one frame, at most
/// `kFlashHeaderBytes + kFlashMaxPayloadBytes + kFlashTrailerBytes` bytes.
/// Text is passed on as soon as it arrives, never held.
///
/// @threading Not thread-safe by design, like `SerialFrameParser`: one
/// instance is driven by one reader. It performs no I/O, so a test feeds it
/// bytes directly, in any chunking.
class FlashFrameParser {
public:
    /// Feed a freshly read chunk. Returns the frames completed by it and
    /// the text it carried. Bytes of an unfinished frame are kept for the
    /// next call. Never throws on any input.
    [[nodiscard]] FlashStreamOutput consume(std::span<const std::byte> bytes);

    /// False starts seen so far: CRC mismatches plus impossible `LEN`
    /// values. A health signal for a noisy link, not an error.
    [[nodiscard]] std::size_t falseStarts() const { return falseStarts_; }

private:
    /// Starts with `0xA5` whenever it is not empty.
    std::vector<std::byte> buffer_;
    std::size_t            falseStarts_ = 0;
};

}  // namespace app::integration
