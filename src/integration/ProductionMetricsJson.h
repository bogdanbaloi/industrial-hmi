#pragma once

#include <nlohmann/json_fwd.hpp>

namespace app::model {
class ProductionModel;
}

namespace app::integration {

/// Build the production-KPI JSON snapshot shared by the MCP `production_metrics`
/// tool and the HTTP `GET /production` route.
///
/// Extracted from ProductionMetricsTool so the LLM-facing tool and the REST
/// route serve one identical shape and cannot drift. Pure and read-only: it
/// reads `ProductionModel::getWorkUnit()` and `ProductionModel::oeeSnapshot()`
/// and nothing else.
///
/// Output shape:
///   {"throughputUph": <double>,
///    "oeePct": <float>,
///    "minutesPerUnit": <double>}   // 60 / throughputUph
///
/// `minutesPerUnit` is OMITTED (not null, not infinite) when throughput is
/// non-positive: a stalled or uninitialised line reports throughput 0, and
/// dividing by it has no meaningful per-unit time. A negative reading is
/// guarded the same way.
[[nodiscard]] nlohmann::json buildProductionMetricsJson(
    const model::ProductionModel& production);

}  // namespace app::integration
