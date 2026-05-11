#include "src/integration/SensorIngestBridge.h"

#include "src/model/ProductionModel.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>

namespace app::integration {

SensorIngestBridge::SensorIngestBridge(TelemetrySubscriber& subscriber,
                                       model::ProductionModel& model)
    : SensorIngestBridge(subscriber, model, Config{}) {}

SensorIngestBridge::SensorIngestBridge(TelemetrySubscriber& subscriber,
                                       model::ProductionModel& model,
                                       Config config)
    : subscriber_(subscriber),
      model_(model),
      config_(std::move(config)) {}

void SensorIngestBridge::wire() {
    // One subscription per equipment slot. We use concrete topics
    // (no `+` wildcard) so the client-side exact-match dispatch in
    // MqttClient finds the right callback regardless of broker
    // wildcard support.
    for (std::uint32_t id = 0; id < config_.equipmentCount; ++id) {
        const auto topic = std::format("{}/equipment/{}/state",
                                       config_.topicPrefix, id);
        subscriber_.subscribe(
            topic,
            [this, id](std::string_view /*topic*/,
                       std::string_view payload) {
                bool enabled = false;
                if (parseOnOffPayload(payload, enabled)) {
                    model_.setEquipmentEnabled(id, enabled);
                }
                // Unknown payloads are dropped; the bridge is fan-in,
                // not a validator. A misbehaving sensor must not be
                // able to crash the HMI by sending garbage.
            });
    }
}

bool SensorIngestBridge::parseOnOffPayload(std::string_view raw, bool& out) {
    // Trim ASCII whitespace from both ends so a sensor that pads with
    // newlines / spaces still parses.
    auto isWs = [](unsigned char c) noexcept { return std::isspace(c) != 0; };
    while (!raw.empty() && isWs(static_cast<unsigned char>(raw.front()))) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && isWs(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    if (raw.empty()) return false;

    // Case-insensitive lower copy. Small alloc; sensor payloads are
    // tiny ASCII strings, not worth a fancier in-place trick.
    std::string lower;
    lower.reserve(raw.size());
    for (char c : raw) {
        lower.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower == "on" || lower == "1" || lower == "true") {
        out = true;
        return true;
    }
    if (lower == "off" || lower == "0" || lower == "false") {
        out = false;
        return true;
    }
    return false;
}

}  // namespace app::integration
