#include "src/integration/StatusJson.h"

#include "src/model/ProductionModel.h"
#include "src/model/ProductionTypes.h"

#include <nlohmann/json.hpp>

namespace app::integration {

namespace {

/// Map SystemState to a stable wire-protocol string. Lower-case + no
/// whitespace so the value reads cleanly as a JSON string field. Shared
/// by the TCP `status` command and the HTTP `/status` route through
/// buildStatusJson.
const char* systemStateName(model::SystemState state) {
    using enum model::SystemState;
    switch (state) {
        case IDLE:        return "idle";
        case RUNNING:     return "running";
        case ERROR:       return "error";
        case CALIBRATION: return "calibration";
    }
    return "unknown";
}

}  // namespace

nlohmann::json buildStatusJson(const model::ProductionModel& production) {
    const auto state = production.getState();
    return nlohmann::json{
        {"state", systemStateName(state)},
        {"running", state == model::SystemState::RUNNING},
    };
}

}  // namespace app::integration
