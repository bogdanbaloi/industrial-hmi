#pragma once

#include "src/integration/SerialFrameParser.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace app::integration {

/// A decoded "set equipment on/off" command from a serial reading.
struct EquipmentStateCommand {
    std::uint32_t equipmentId;
    bool          enabled;
};

/// Map a serial reading to an equipment-state command, mirroring the MQTT
/// SensorIngestBridge vocabulary: a sensorId of `equipment/<n>/state` with a
/// value of on|off|1|0|true|false (case-insensitive). Returns nullopt when
/// the reading is not a well-formed equipment-state frame, so a misbehaving
/// device is dropped rather than crashing the ingest (same fan-in-not-a-
/// validator posture as SensorIngestBridge).
[[nodiscard]] inline std::optional<EquipmentStateCommand>
toEquipmentStateCommand(const SerialReading& reading) {
    constexpr std::string_view kPrefix = "equipment/";
    constexpr std::string_view kSuffix = "/state";

    const std::string_view id = reading.sensorId;
    if (id.size() <= kPrefix.size() + kSuffix.size()) {
        return std::nullopt;  // no room for a non-empty <n> between the fences
    }
    if (id.substr(0, kPrefix.size()) != kPrefix ||
        id.substr(id.size() - kSuffix.size()) != kSuffix) {
        return std::nullopt;
    }

    const std::string_view number =
        id.substr(kPrefix.size(), id.size() - kPrefix.size() - kSuffix.size());
    std::uint32_t equipmentId = 0;
    const auto result =
        std::from_chars(number.data(), number.data() + number.size(),
                        equipmentId);
    if (result.ec != std::errc{} ||
        result.ptr != number.data() + number.size()) {
        return std::nullopt;  // <n> is not a plain non-negative integer
    }

    std::string value(reading.value);
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    if (value == "on" || value == "1" || value == "true") {
        return EquipmentStateCommand{equipmentId, true};
    }
    if (value == "off" || value == "0" || value == "false") {
        return EquipmentStateCommand{equipmentId, false};
    }
    return std::nullopt;  // unrecognised value
}

}  // namespace app::integration
