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
///   0 = offline, 1 = online, 2 = processing, 3 = error.
/// We publish the named state directly rather than collapsing it
/// onto a coarse ok/fault bit -- a SCADA consumer typically wants to
/// distinguish "idle but ready" from "actively running" from
/// "stopped", not just "alarm vs no alarm".
const char* equipmentStatusName(int status) {
    switch (status) {
        case 0: return "offline";
        case 1: return "online";
        case 2: return "processing";
        case 3: return "error";
    }
    return "unknown";
}

/// QualityCheckpoint.status convention from ProductionTypes.h:
///   0 = passing, 1 = warning, 2 = critical.
const char* qualityStatusName(int status) {
    switch (status) {
        case 0: return "passing";
        case 1: return "warning";
        case 2: return "critical";
    }
    return "unknown";
}

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

    // Equipment status -- per equipment id, current state. Publishes
    // the full status name (offline/online/processing/error) plus a
    // separate supply-level topic so subscribers can track inventory
    // without subscribing to a wildcard.
    production_.onEquipmentStatusChanged(
        [this](const model::EquipmentStatus& es) {
            const auto stateTopic = std::format(
                "{}/equipment/{}/state",
                config_.topicPrefix, es.equipmentId);
            publisher_.publish(stateTopic, equipmentStatusName(es.status));

            const auto supplyTopic = std::format(
                "{}/equipment/{}/supply",
                config_.topicPrefix, es.equipmentId);
            publisher_.publish(supplyTopic, std::to_string(es.supplyLevel));
        });

    // Quality checkpoints -- per checkpoint id, current pass rate and
    // status. Status is the named severity (passing/warning/critical);
    // rate keeps its numeric format so downstream historian plots
    // continue to work unchanged.
    production_.onQualityCheckpointChanged(
        [this](const model::QualityCheckpoint& qc) {
            const auto rateTopic = std::format(
                "{}/quality/{}/rate",
                config_.topicPrefix, qc.checkpointId);
            publisher_.publish(rateTopic,
                               std::format("{:.1f}", qc.passRate));

            const auto statusTopic = std::format(
                "{}/quality/{}/status",
                config_.topicPrefix, qc.checkpointId);
            publisher_.publish(statusTopic, qualityStatusName(qc.status));
        });
}

}  // namespace app::integration
