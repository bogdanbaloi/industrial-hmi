#include "src/mcp/McpProtocol.h"

#include "src/mcp/tools/AlarmsSnapshotTool.h"
#include "src/mcp/tools/HistorianQueryTool.h"
#include "src/mcp/tools/ProductionMetricsTool.h"

#include <string>

namespace app::mcp {

namespace {

// Wrap a tool's JSON payload in the MCP tools/call content envelope. MCP
// returns a content array; a single pretty-printed text block is what an LLM
// consumes.
nlohmann::json toolResult(const nlohmann::json& data) {
    return {{"content", {{{"type", "text"}, {"text", data.dump(2)}}}}};
}

}  // namespace

nlohmann::json handleInitialize(const nlohmann::json& /*params*/) {
    return {
        {"protocolVersion", kMcpProtocolVersion},
        {"capabilities", {{"tools", nlohmann::json::object()}}},
        {"serverInfo", {{"name", kServerName}, {"version", kServerVersion}}},
    };
}

nlohmann::json handleToolsList() {
    return {{"tools",
             {alarmsSnapshotDescriptor(), historianQueryDescriptor(),
              productionMetricsDescriptor()}}};
}

app::core::Result<nlohmann::json, McpErrorCode>
handleToolsCall(const nlohmann::json& params,
                const presenter::AlertCenter& alerts,
                historian::HistoryReader& reader,
                const model::ProductionModel& production) {
    using Res = app::core::Result<nlohmann::json, McpErrorCode>;

    const std::string name = params.value("name", std::string{});
    const nlohmann::json arguments = params.contains("arguments")
                                         ? params.at("arguments")
                                         : nlohmann::json::object();

    if (name == kAlarmsSnapshotTool) {
        return Res{app::core::Ok, toolResult(runAlarmsSnapshot(alerts))};
    }
    if (name == kProductionMetricsTool) {
        return Res{app::core::Ok, toolResult(runProductionMetrics(production))};
    }
    if (name == kHistorianQueryTool) {
        auto parsed = parseHistorianArgs(arguments);
        if (parsed.isErr()) {
            return Res{app::core::Err, parsed.error()};
        }
        return Res{app::core::Ok,
                   toolResult(runHistorianQuery(reader, parsed.unwrap()))};
    }
    return Res{app::core::Err, McpErrorCode::MethodNotFound};
}

}  // namespace app::mcp
