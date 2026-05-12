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
    const auto upperBound =
        config_.equipmentCount > kMaxTrackedEquipment
            ? static_cast<std::uint32_t>(kMaxTrackedEquipment)
            : config_.equipmentCount;

    for (std::uint32_t id = 0; id < upperBound; ++id) {
        const auto path = std::format(
            "{}/EquipmentLines/Line{}/Status",
            config_.topicPrefix, id);
        (void)client_.subscribeInt32(
            path,
            [this, id](std::string_view /*nodePath*/, std::int32_t value) {
                onStatusNotification(id, value);
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

}  // namespace app::integration::opcua
