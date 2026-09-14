#include "src/mcp/tools/ProductionMetricsTool.h"

#include "src/integration/ProductionMetricsJson.h"

#include <nlohmann/json.hpp>

namespace app::mcp {

nlohmann::json productionMetricsDescriptor() {
    return {
        {"name", kProductionMetricsTool},
        {"description",
         "Live production KPIs: throughput (units/hour), OEE (percent) and the "
         "derived minutes per unit. Read-only."},
        {"inputSchema",
         {{"type", "object"}, {"properties", nlohmann::json::object()}}},
    };
}

nlohmann::json runProductionMetrics(const model::ProductionModel& production) {
    // Delegate to the shared builder so the LLM-facing tool and the HTTP
    // `GET /production` route serve one identical shape and cannot drift
    // (REQ-INTEGRATION-008).
    return app::integration::buildProductionMetricsJson(production);
}

}  // namespace app::mcp
