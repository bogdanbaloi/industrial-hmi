#include "src/mcp/tools/ProductionMetricsTool.h"

#include "src/model/ProductionTypes.h"

#include <nlohmann/json.hpp>

namespace app::mcp {

namespace {

// Minutes in an hour. The throughput signal is completed work units per hour,
// so per-unit minutes is 60 / throughput. Named so the derivation reads as
// intent and passes the magic-number lint (60.0 is not a float allowlisted by
// clang-tidy).
constexpr double kMinutesPerHour = 60.0;

}  // namespace

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
    const double throughputUph = production.getWorkUnit().throughputUnitsPerHour;
    const float  oeePct        = production.oeeSnapshot().oeePct;

    nlohmann::json metrics = {
        {"throughputUph", throughputUph},
        {"oeePct", oeePct},
    };

    // Omit-the-key rather than emit a null / infinite value: a stalled or
    // uninitialised line reports throughput 0, and dividing by it has no
    // meaningful per-unit time. A negative reading is equally guarded.
    if (throughputUph > 0.0) {
        metrics["minutesPerUnit"] = kMinutesPerHour / throughputUph;
    }

    return metrics;
}

}  // namespace app::mcp
