#pragma once

#include <nlohmann/json_fwd.hpp>

namespace app::model {
class ProductionModel;
}

namespace app::integration {

/// Build the production-status JSON snapshot shared by the TCP `status`
/// command and the HTTP `GET /status` route.
///
/// Extracted from TcpBackend's inline status branch so the line protocol
/// and the REST route serve one identical shape and cannot drift. Pure and
/// read-only: it reads `ProductionModel::getState()` and nothing else.
///
/// Output shape:
///   {"state": "idle" | "running" | "error" | "calibration",
///    "running": <bool>}
///
/// `running` is the convenience predicate `state == RUNNING`, kept in the
/// payload so a client does not have to know which state string means the
/// line is live.
[[nodiscard]] nlohmann::json buildStatusJson(const model::ProductionModel& production);

}  // namespace app::integration
