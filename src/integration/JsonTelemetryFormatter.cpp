#include "src/integration/JsonTelemetryFormatter.h"

// Some platforms (Windows wingdi via Boost.Asio in other TUs) define
// ERROR as 0 macro -- keep this TU clean even if it's reordered with
// a transitive include in the future.
#ifdef ERROR
#  undef ERROR
#endif

#include <format>

namespace app::integration {

namespace {

/// Stable wire-protocol string for SystemState. Kept in lockstep with
/// the plain-text formatter so renaming an enum value surfaces on both
/// topics, never on just one.
const char* systemStateName(model::SystemState s) {
    switch (s) {
        case model::SystemState::IDLE:        return "idle";
        case model::SystemState::RUNNING:     return "running";
        case model::SystemState::ERROR:       return "error";
        case model::SystemState::CALIBRATION: return "calibration";
    }
    return "unknown";
}

const char* equipmentStatusName(int status) {
    switch (status) {
        case 0: return "offline";
        case 1: return "online";
        case 2: return "processing";
        case 3: return "error";
    }
    return "unknown";
}

const char* qualityStatusName(int status) {
    switch (status) {
        case 0: return "passing";
        case 1: return "warning";
        case 2: return "critical";
    }
    return "unknown";
}

}  // namespace

void JsonTelemetryFormatter::publishSystemState(
        TelemetryPublisher& publisher,
        std::string_view topicPrefix,
        model::SystemState state) {
    publisher.publish(
        std::format("{}/state/json", topicPrefix),
        std::format(R"({{"state":"{}"}})", systemStateName(state)));
}

void JsonTelemetryFormatter::publishEquipment(
        TelemetryPublisher& publisher,
        std::string_view topicPrefix,
        const model::EquipmentStatus& es) {
    publisher.publish(
        std::format("{}/equipment/{}/json", topicPrefix, es.equipmentId),
        std::format(
            R"({{"id":{},"state":"{}","supply":{}}})",
            es.equipmentId,
            equipmentStatusName(es.status),
            es.supplyLevel));
}

void JsonTelemetryFormatter::publishQuality(
        TelemetryPublisher& publisher,
        std::string_view topicPrefix,
        const model::QualityCheckpoint& qc) {
    publisher.publish(
        std::format("{}/quality/{}/json", topicPrefix, qc.checkpointId),
        std::format(
            R"({{"id":{},"rate":{:.1f},"status":"{}"}})",
            qc.checkpointId,
            qc.passRate,
            qualityStatusName(qc.status)));
}

}  // namespace app::integration
