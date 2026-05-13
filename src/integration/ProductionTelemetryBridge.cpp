#include "src/integration/ProductionTelemetryBridge.h"

#include "src/integration/JsonTelemetryFormatter.h"
#include "src/integration/PlainTextTelemetryFormatter.h"

#include <utility>

namespace app::integration {

namespace {

/// Build the default formatter list from Config flags. Caller-supplied
/// formatters bypass this entirely and pass through the second ctor.
std::vector<std::unique_ptr<TelemetryPayloadFormatter>>
makeDefaultFormatters(const ProductionTelemetryBridge::Config& config) {
    std::vector<std::unique_ptr<TelemetryPayloadFormatter>> formatters;
    if (config.emitPlainText) {
        formatters.push_back(std::make_unique<PlainTextTelemetryFormatter>());
    }
    if (config.emitJson) {
        formatters.push_back(std::make_unique<JsonTelemetryFormatter>());
    }
    return formatters;
}

}  // namespace

ProductionTelemetryBridge::ProductionTelemetryBridge(
        TelemetryPublisher& publisher,
        model::ProductionModel& production,
        Config config)
    : publisher_(publisher),
      production_(production),
      config_(std::move(config)),
      formatters_(makeDefaultFormatters(config_)) {}

ProductionTelemetryBridge::ProductionTelemetryBridge(
        TelemetryPublisher& publisher,
        model::ProductionModel& production,
        Config config,
        std::vector<std::unique_ptr<TelemetryPayloadFormatter>> formatters)
    : publisher_(publisher),
      production_(production),
      config_(std::move(config)),
      formatters_(std::move(formatters)) {}

void ProductionTelemetryBridge::wire() {
    // SystemState changes: fan out to every registered formatter.
    production_.onSystemStateChanged(
        [this](model::SystemState s) {
            for (auto& f : formatters_) {
                f->publishSystemState(publisher_, config_.topicPrefix, s);
            }
        });

    // Equipment status: same fan-out. Each formatter decides whether
    // to split per field (plain text) or consolidate into one JSON
    // document.
    production_.onEquipmentStatusChanged(
        [this](const model::EquipmentStatus& es) {
            for (auto& f : formatters_) {
                f->publishEquipment(publisher_, config_.topicPrefix, es);
            }
        });

    // Quality checkpoints: same fan-out pattern.
    production_.onQualityCheckpointChanged(
        [this](const model::QualityCheckpoint& qc) {
            for (auto& f : formatters_) {
                f->publishQuality(publisher_, config_.topicPrefix, qc);
            }
        });
}

}  // namespace app::integration
