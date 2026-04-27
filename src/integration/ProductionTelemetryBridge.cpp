#include "src/integration/ProductionTelemetryBridge.h"

#include "src/model/ProductionTypes.h"

// Some platforms (Windows wingdi via Boost.Asio in other TUs) define
// ERROR as 0 macro -- keep this TU clean even if it's reordered with
// a transitive include in the future.
#ifdef ERROR
#  undef ERROR
#endif

#include <format>
#include <utility>

namespace app::integration {

namespace {

/// Stable wire-protocol string for SystemState. Mirrors the TCP
/// backend's mapping so subscribers consuming both transports see the
/// same vocabulary.
const char* systemStateName(model::SystemState s) {
    switch (s) {
        case model::SystemState::IDLE:        return "idle";
        case model::SystemState::RUNNING:     return "running";
        case model::SystemState::ERROR:       return "error";
        case model::SystemState::CALIBRATION: return "calibration";
    }
    return "unknown";
}

/// EquipmentStatus.status convention from ProductionTypes.h:
///   0 idle, 1 processing, 3 error.
/// Topics publish a boolean "ok"/"fault" view to keep subscribers
/// from having to learn the integer codes.
constexpr int kEquipmentStatusError = 3;

}  // namespace

ProductionTelemetryBridge::ProductionTelemetryBridge(
        TelemetryPublisher& publisher,
        model::ProductionModel& production,
        Config config)
    : publisher_(publisher),
      production_(production),
      config_(std::move(config)) {}

void ProductionTelemetryBridge::wire() {
    // SystemState changes -- one publish per transition.
    production_.onSystemStateChanged(
        [this](model::SystemState s) {
            publisher_.publish(config_.topicPrefix + "/state",
                               systemStateName(s));
        });

    // Equipment status -- per equipment id, current state.
    production_.onEquipmentStatusChanged(
        [this](const model::EquipmentStatus& es) {
            const auto topic = std::format(
                "{}/equipment/{}/state",
                config_.topicPrefix, es.equipmentId);
            const char* state =
                (es.status == kEquipmentStatusError) ? "fault" : "ok";
            publisher_.publish(topic, state);
        });

    // Quality checkpoints -- per checkpoint id, current pass rate.
    production_.onQualityCheckpointChanged(
        [this](const model::QualityCheckpoint& qc) {
            const auto topic = std::format(
                "{}/quality/{}/rate",
                config_.topicPrefix, qc.checkpointId);
            publisher_.publish(topic,
                               std::format("{:.1f}", qc.passRate));
        });
}

}  // namespace app::integration
