#include "src/integration/SerialFrameParser.h"

#include <cstddef>
#include <utility>

namespace app::integration {

namespace {
/// Frame boundary: a reading ends at a newline. Serial carries no message
/// framing of its own, so we impose a line-based one.
constexpr char kFrameDelimiter = '\n';
/// Splits sensor id from value inside one frame ("temp,23.5").
constexpr char kFieldDelimiter = ',';
/// A stray carriage return before the newline (CRLF terminals) is trimmed.
constexpr char kCarriageReturn = '\r';
/// Guard against an endless line with no delimiter: if the buffer grows
/// past this without a frame boundary, drop it rather than grow unbounded.
constexpr std::size_t kMaxBufferBytes = 4096;

/// Parse one complete line into a reading. Returns false (skip) when the
/// line is empty or is not exactly `sensorId,value` with both parts
/// present. Never throws.
[[nodiscard]] bool parseLine(std::string_view line, SerialReading& out) {
    // Tolerate CRLF line endings: drop a trailing carriage return.
    if (!line.empty() && line.back() == kCarriageReturn) {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return false;
    }
    const std::size_t comma = line.find(kFieldDelimiter);
    if (comma == std::string_view::npos) {
        return false;  // no field delimiter
    }
    // The protocol is exactly two fields, so reject a second delimiter.
    if (line.find(kFieldDelimiter, comma + 1) != std::string_view::npos) {
        return false;
    }
    const std::string_view id = line.substr(0, comma);
    const std::string_view value = line.substr(comma + 1);
    if (id.empty() || value.empty()) {
        return false;  // both fields must be present
    }
    out.sensorId.assign(id);
    out.value.assign(value);
    return true;
}
}  // namespace

std::vector<SerialReading> SerialFrameParser::consume(std::string_view bytes) {
    std::vector<SerialReading> readings;
    buffer_.append(bytes);

    std::size_t start = 0;
    std::size_t newline = 0;
    while ((newline = buffer_.find(kFrameDelimiter, start)) != std::string::npos) {
        const std::string_view line(buffer_.data() + start, newline - start);
        SerialReading reading;
        if (parseLine(line, reading)) {
            readings.push_back(std::move(reading));
        }
        start = newline + 1;
    }

    // Drop the consumed lines, keep the trailing partial for the next chunk.
    buffer_.erase(0, start);

    // A partial line that never terminates must not grow forever.
    if (buffer_.size() > kMaxBufferBytes) {
        buffer_.clear();
    }
    return readings;
}

}  // namespace app::integration
