#include "src/integration/PlainTextTelemetryFormatter.h"

// Some platforms (Windows wingdi via Boost.Asio in other TUs) define
// ERROR as 0 macro -- keep this TU clean even if it's reordered with
// a transitive include in the future.
#ifdef ERROR
#  undef ERROR
#endif

#include <format>
#include <string>

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

void PlainTextTelemetryFormatter::publishSystemState(
        TelemetryPublisher& publisher,
        std::string_view topicPrefix,
        model::SystemState state) {
    publisher.publish(std::format("{}/state", topicPrefix),
                      systemStateName(state));
}

void PlainTextTelemetryFormatter::publishEquipment(
        TelemetryPublisher& publisher,
        std::string_view topicPrefix,
        const model::EquipmentStatus& es) {
    publisher.publish(
        std::format("{}/equipment/{}/state", topicPrefix, es.equipmentId),
        equipmentStatusName(es.status));
    publisher.publish(
        std::format("{}/equipment/{}/supply", topicPrefix, es.equipmentId),
        std::to_string(es.supplyLevel));
}

void PlainTextTelemetryFormatter::publishQuality(
        TelemetryPublisher& publisher,
        std::string_view topicPrefix,
        const model::QualityCheckpoint& qc) {
    publisher.publish(
        std::format("{}/quality/{}/rate", topicPrefix, qc.checkpointId),
        std::format("{:.1f}", qc.passRate));
    publisher.publish(
        std::format("{}/quality/{}/status", topicPrefix, qc.checkpointId),
        qualityStatusName(qc.status));
}

}  // namespace app::integration
