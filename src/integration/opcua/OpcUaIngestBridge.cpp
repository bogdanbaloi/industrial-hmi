#include "src/integration/opcua/OpcUaIngestBridge.h"

#include "src/model/ProductionModel.h"

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace app::integration::opcua {

namespace {

/// EquipmentStatus encoding from `ProductionTypes.h`. The model uses
/// 0 for offline; every other non-negative value is "operating" in
/// some flavour (online, processing, error). The bridge only carries
/// the enabled/disabled bit, so anything `!= 0` reads as enabled.
constexpr std::int32_t kEquipmentStatusOffline = 0;

}  // namespace

OpcUaIngestBridge::OpcUaIngestBridge(OpcUaClient& client,
                                     model::ProductionModel& model)
    : OpcUaIngestBridge(client, model, Config{}) {}

OpcUaIngestBridge::OpcUaIngestBridge(OpcUaClient& client,
                                     model::ProductionModel& model,
                                     Config config)
    : client_(client),
      model_(model),
      config_(std::move(config)) {}

void OpcUaIngestBridge::wire() {
    const auto equipmentBound =
        config_.equipmentCount > kMaxTrackedEquipment
            ? static_cast<std::uint32_t>(kMaxTrackedEquipment)
            : config_.equipmentCount;
    const auto qualityBound =
        config_.qualityCount > kMaxTrackedQuality
            ? static_cast<std::uint32_t>(kMaxTrackedQuality)
            : config_.qualityCount;

    // Per-equipment: subscribe to Status (enabled bit) + Supply
    // (analog supply level). Browse paths mirror what the project's
    // own OPC-UA server publishes via FactoryNodeMap so the in-
    // process loopback case "just works" against itself.
    for (std::uint32_t id = 0; id < equipmentBound; ++id) {
        const auto statusPath = std::format(
            "{}/EquipmentLines/Line{}/Status",
            config_.topicPrefix, id);
        (void)client_.subscribeInt32(
            statusPath,
            [this, id](std::string_view /*nodePath*/,
                       std::int32_t value) {
                onStatusNotification(id, value);
            });

        const auto supplyPath = std::format(
            "{}/EquipmentLines/Line{}/Supply",
            config_.topicPrefix, id);
        (void)client_.subscribeInt32(
            supplyPath,
            [this, id](std::string_view /*nodePath*/,
                       std::int32_t value) {
                onSupplyNotification(id, value);
            });
    }

    // Per-quality-checkpoint: subscribe to PassRate (float percent).
    for (std::uint32_t id = 0; id < qualityBound; ++id) {
        const auto ratePath = std::format(
            "{}/QualityCheckpoints/Checkpoint{}/PassRate",
            config_.topicPrefix, id);
        (void)client_.subscribeFloat(
            ratePath,
            [this, id](std::string_view /*nodePath*/, float value) {
                onPassRateNotification(id, value);
            });
    }
}

void OpcUaIngestBridge::onStatusNotification(std::uint32_t id,
                                             std::int32_t status) {
    if (id >= kMaxTrackedEquipment) return;  // defensive

    const bool enabled = (status != kEquipmentStatusOffline);
    auto& cached = lastEnabled_.at(id);
    if (cached.has_value() && *cached == enabled) {
        // No-op: the enabled bit didn't change since the last
        // notification we propagated. Without this guard, every
        // simulator tick that toggles Online <-> Processing would
        // bounce through the model.
        return;
    }
    cached = enabled;
    model_.setEquipmentEnabled(id, enabled);
}

void OpcUaIngestBridge::onSupplyNotification(std::uint32_t id,
                                             std::int32_t supply) {
    const int value = static_cast<int>(supply);
    if (id < kMaxTrackedEquipment) {
        auto& cached = lastSupplyLevel_.at(id);
        if (cached.has_value() && *cached == value) {
            return;  // dedup: same percent, no model churn
        }
        cached = value;
    }
    // Out-of-range ids skip the cache but still forward; the model
    // owns the clamp + log-and-drop on unknown ids.
    model_.setEquipmentSupplyLevel(id, value);
}

void OpcUaIngestBridge::onPassRateNotification(std::uint32_t id,
                                               float rate) {
    if (id < kMaxTrackedQuality) {
        auto& cached = lastPassRate_.at(id);
        if (cached.has_value() && *cached == rate) {
            return;
        }
        cached = rate;
    }
    model_.setQualityPassRate(id, rate);
}

}  // namespace app::integration::opcua
