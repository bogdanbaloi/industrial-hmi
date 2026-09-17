#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace app::integration {

/// One decoded telemetry reading from a serial frame: a sensor identifier
/// and its raw value field. The value stays a string here; how it is
/// interpreted (int / float / on-off) is the ingest layer's job, per
/// sensor, so the frame parser holds no sensor-type knowledge. Single
/// responsibility: framing plus field split.
struct SerialReading {
    std::string sensorId;
    std::string value;
};

/// Turns the raw BYTE STREAM arriving on a serial port into whole
/// `sensorId,value` readings.
///
/// @rationale A serial link delivers bytes in arbitrary chunks with no
/// message boundaries: one read may carry half a line, a whole line, or
/// several lines at once. The parser owns a buffer, appends each incoming
/// chunk, and emits a reading only for each COMPLETE line (terminated by
/// the frame delimiter). Any trailing partial line is kept for the next
/// chunk. This is the classic "framing over a stream" problem, the same
/// one TCP has (see TcpBackend).
///
/// @threading Not thread-safe by design. One parser instance is driven by
/// one reader (the serial read loop). It performs no I/O itself, which is
/// what makes it unit-testable with no serial port: a test feeds bytes
/// straight to `consume`.
class SerialFrameParser {
public:
    /// Feed a freshly-read chunk of bytes. Returns every complete reading
    /// that became available (zero or more). Malformed lines are skipped
    /// and never throw. Trailing partial bytes are retained for the next
    /// call.
    [[nodiscard]] std::vector<SerialReading> consume(std::string_view bytes);

private:
    /// Holds bytes seen so far that do not yet form a complete line.
    std::string buffer_;
};

}  // namespace app::integration
