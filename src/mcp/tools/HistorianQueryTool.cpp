#include "src/mcp/tools/HistorianQueryTool.h"

#include <nlohmann/json.hpp>

#include <array>
#include <algorithm>
#include <cstdint>
#include <string>

namespace app::mcp {

namespace {

using app::core::Result;
using historian::FieldKind;

// Single source of truth for the FieldKind <-> wire-name mapping. fieldName,
// fieldFromName and the descriptor's `enum` all derive from this one table so
// the series vocabulary is never spelled out in more than one place.
struct FieldMapping {
    FieldKind   field;
    const char* name;
};
constexpr auto kFieldMappings = std::to_array<FieldMapping>({
    {.field = FieldKind::QualityPassRate, .name = "quality"},
    {.field = FieldKind::EquipmentSupplyLevel, .name = "supply"},
    {.field = FieldKind::SystemState, .name = "state"},
});

const char* fieldName(FieldKind field) {
    for (const auto& mapping : kFieldMappings) {
        if (mapping.field == field) {
            return mapping.name;
        }
    }
    return kFieldMappings.front().name;
}

bool fieldFromName(const std::string& name, FieldKind& out) {
    for (const auto& mapping : kFieldMappings) {
        if (name == mapping.name) {
            out = mapping.field;
            return true;
        }
    }
    return false;
}

nlohmann::json fieldNameEnum() {
    nlohmann::json names = nlohmann::json::array();
    for (const auto& mapping : kFieldMappings) {
        names.push_back(mapping.name);
    }
    return names;
}

}  // namespace

nlohmann::json historianQueryDescriptor() {
    return {
        {"name", kHistorianQueryTool},
        {"description",
         "Bounded time-series read from the historian archive. Read-only."},
        {"inputSchema",
         {{"type", "object"},
          {"properties",
           {{"field",
             {{"type", "string"},
              {"enum", fieldNameEnum()},
              {"description",
               "Series: quality pass rate, equipment supply level, or "
               "system state."}}},
            {"entityId",
             {{"type", "integer"},
              {"description",
               "Checkpoint or equipment id (0 for the global system-state "
               "series)."}}},
            {"fromMs",
             {{"type", "integer"},
              {"description", "Range start, ms since the Unix epoch."}}},
            {"toMs",
             {{"type", "integer"},
              {"description", "Range end, ms since the Unix epoch."}}},
            {"limit",
             {{"type", "integer"},
              {"description", "Max rows, capped at the store default."}}}}},
          {"required", {"field"}}}},
    };
}

Result<HistorianQueryArgs, McpErrorCode>
parseHistorianArgs(const nlohmann::json& args) {
    using Res = Result<HistorianQueryArgs, McpErrorCode>;
    if (!args.is_object()) {
        return Res{app::core::Err, McpErrorCode::InvalidParams};
    }

    HistorianQueryArgs parsed;
    const std::string fieldStr = args.value("field", std::string{});
    if (!fieldFromName(fieldStr, parsed.field)) {
        return Res{app::core::Err, McpErrorCode::InvalidParams};
    }

    parsed.entityId     = args.value("entityId", std::uint32_t{0});
    parsed.range.fromMs = args.value("fromMs", std::int64_t{0});
    parsed.range.toMs   = args.value("toMs", std::int64_t{0});

    const std::size_t requested =
        args.value("limit", historian::QueryRange::kDefaultLimit);
    parsed.range.limit = std::min(requested, historian::QueryRange::kDefaultLimit);

    return Res{app::core::Ok, parsed};
}

nlohmann::json
runHistorianQuery(historian::HistoryReader& reader, const HistorianQueryArgs& args) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& record : reader.query(args.field, args.entityId, args.range)) {
        array.push_back({
            {"timestampMs", record.timestampMs},
            {"field", fieldName(record.field)},
            {"entityId", record.entityId},
            {"value", record.value},
        });
    }
    return array;
}

}  // namespace app::mcp
