#include "src/mcp/tools/AlarmsSnapshotTool.h"

#include <nlohmann/json.hpp>

namespace app::mcp {

namespace {

// Stable wire strings for the alarm severity and lifecycle state, so an LLM
// (and a human reading the JSON) sees a documented vocabulary rather than a
// raw enum index.
const char* severityName(presenter::AlertSeverity severity) {
    using Severity = presenter::AlertSeverity;
    switch (severity) {
        case Severity::Info:     return "info";
        case Severity::Warning:  return "warning";
        case Severity::Critical: return "critical";
    }
    return "info";
}

const char* stateName(presenter::AlarmState state) {
    using State = presenter::AlarmState;
    switch (state) {
        case State::UnackActive: return "unack_active";
        case State::AckActive:   return "ack_active";
        case State::RtnUnack:    return "rtn_unack";
        case State::Shelved:     return "shelved";
    }
    return "unack_active";
}

}  // namespace

nlohmann::json alarmsSnapshotDescriptor() {
    return {
        {"name", kAlarmsSnapshotTool},
        {"description",
         "Current active alarms (ISA-18.2), most urgent first. Read-only."},
        {"inputSchema",
         {{"type", "object"}, {"properties", nlohmann::json::object()}}},
    };
}

nlohmann::json runAlarmsSnapshot(const presenter::AlertCenter& alerts) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& alert : alerts.snapshot()) {
        array.push_back({
            {"key", alert.key},
            {"severity", severityName(alert.severity)},
            {"state", stateName(alert.state)},
            {"priority", alert.priority},
            {"title", alert.title},
            {"message", alert.message},
            {"timestamp", alert.timestamp},
        });
    }
    return array;
}

}  // namespace app::mcp
