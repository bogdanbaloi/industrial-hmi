#pragma once

#include "src/model/ProductionModel.h"

#include <nlohmann/json_fwd.hpp>

namespace app::mcp {

// MCP tool name, part of the wire contract (the LLM calls it by this string).
inline constexpr const char* kProductionMetricsTool = "production_metrics";

/// The MCP tool descriptor (name + description + input schema) for `tools/list`.
[[nodiscard]] nlohmann::json productionMetricsDescriptor();

/// Execute the tool: a live snapshot of the line's throughput and OEE, plus a
/// derived minutes-per-unit. A thin, read-only wrapper over the abstract
/// `model::ProductionModel` (DIP: no dependency on the concrete SimulatedModel)
/// -- no side effects of its own. `minutesPerUnit` is omitted when throughput
/// is non-positive, so a stalled line yields a valid snapshot rather than a
/// divide-by-zero or an infinite value.
[[nodiscard]] nlohmann::json
runProductionMetrics(const model::ProductionModel& production);

}  // namespace app::mcp
