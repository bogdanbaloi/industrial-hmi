#include "src/integration/SensorIngestBridge.h"

#include "src/model/ProductionModel.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <system_error>
#include <string>
#include <string_view>

namespace {

/// Trim leading + trailing ASCII whitespace in place on a view.
/// Same shape as parseOnOffPayload's local helper; lifted out so
/// the int / float parsers share it.
void trimAsciiWs(std::string_view& raw) {
    auto isWs = [](unsigned char c) noexcept {
        return std::isspace(c) != 0;
    };
    while (!raw.empty() && isWs(static_cast<unsigned char>(raw.front()))) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && isWs(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
}

}  // namespace

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
    // One subscription per equipment slot for each topic family.
    // Concrete topics (no `+` wildcard) keep the client-side
    // exact-match dispatch in MqttClient simple and avoid relying
    // on broker wildcard support.
    //
    // Three topic vocabularies per equipment id:
    //   <prefix>/equipment/<id>/state    -- on/off/1/0/true/false
    //   <prefix>/equipment/<id>/supply   -- integer 0..100 (percent)
    //
    // Plus per quality checkpoint id:
    //   <prefix>/quality/<id>/rate       -- float 0..100 (percent)
    //
    // All parsers fail-silent on garbage: the bridge is fan-in,
    // not a validator. A misbehaving sensor cannot crash the HMI
    // by sending malformed payloads; the operator notices the
    // missing dashboard update and corrects the publisher.
    for (std::uint32_t id = 0; id < config_.equipmentCount; ++id) {
        const auto stateTopic = std::format("{}/equipment/{}/state",
                                            config_.topicPrefix, id);
        subscriber_.subscribe(
            stateTopic,
            [this, id](std::string_view /*topic*/,
                       std::string_view payload) {
                bool enabled = false;
                if (parseOnOffPayload(payload, enabled)) {
                    model_.setEquipmentEnabled(id, enabled);
                }
            });

        const auto supplyTopic = std::format("{}/equipment/{}/supply",
                                             config_.topicPrefix, id);
        subscriber_.subscribe(
            supplyTopic,
            [this, id](std::string_view /*topic*/,
                       std::string_view payload) {
                int level = 0;
                if (parseIntPayload(payload, level)) {
                    model_.setEquipmentSupplyLevel(id, level);
                }
            });
    }

    for (std::uint32_t id = 0; id < config_.qualityCount; ++id) {
        const auto rateTopic = std::format("{}/quality/{}/rate",
                                           config_.topicPrefix, id);
        subscriber_.subscribe(
            rateTopic,
            [this, id](std::string_view /*topic*/,
                       std::string_view payload) {
                float rate = 0.0F;
                if (parseFloatPayload(payload, rate)) {
                    model_.setQualityPassRate(id, rate);
                }
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

bool SensorIngestBridge::parseIntPayload(std::string_view raw, int& out) {
    trimAsciiWs(raw);
    if (raw.empty()) return false;
    int value = 0;
    const auto* first = raw.data();
    const auto* last  = raw.data() + raw.size();
    const auto result = std::from_chars(first, last, value);
    // Reject partial parses (`"42abc"` should not silently become 42)
    // so a misbehaving publisher does not slip garbage through.
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    out = value;
    return true;
}

bool SensorIngestBridge::parseFloatPayload(std::string_view raw, float& out) {
    trimAsciiWs(raw);
    if (raw.empty()) return false;
    // std::from_chars for float is C++17 but the libstdc++ ship has
    // gaps until very recent toolchains. strtof is bulletproof and
    // already part of <cstdlib>; we accept the locale dependence
    // because the broker delivers UTF-8 ASCII numerics either way.
    std::string buf(raw);
    char* endPtr = nullptr;
    const float value = std::strtof(buf.c_str(), &endPtr);
    if (endPtr == nullptr || endPtr == buf.c_str() ||
        *endPtr != '\0') {
        return false;
    }
    out = value;
    return true;
}

}  // namespace app::integration
