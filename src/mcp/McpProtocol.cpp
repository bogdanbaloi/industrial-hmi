#include "src/mcp/McpProtocol.h"

#include "src/mcp/tools/AlarmsSnapshotTool.h"
#include "src/mcp/tools/EquipmentCommandTool.h"
#include "src/mcp/tools/HistorianQueryTool.h"

#include <string>
#include <utility>

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

nlohmann::json handleToolsList(bool writeEnabled) {
    nlohmann::json tools = {alarmsSnapshotDescriptor(), historianQueryDescriptor()};
    if (writeEnabled) {
        tools.push_back(equipmentCommandDescriptor());
    }
    return {{"tools", std::move(tools)}};
}

app::core::Result<nlohmann::json, McpErrorCode>
handleToolsCall(const nlohmann::json& params,
                const presenter::AlertCenter& alerts,
                historian::HistoryReader& reader,
                DashboardPresenter& presenter,
                const auth::Session& session,
                bool writeEnabled) {
    using Res = app::core::Result<nlohmann::json, McpErrorCode>;

    const std::string name = params.value("name", std::string{});
    const nlohmann::json arguments = params.contains("arguments")
                                         ? params.at("arguments")
                                         : nlohmann::json::object();

    if (name == kAlarmsSnapshotTool) {
        return Res{app::core::Ok, toolResult(runAlarmsSnapshot(alerts))};
    }
    if (name == kHistorianQueryTool) {
        auto parsed = parseHistorianArgs(arguments);
        if (parsed.isErr()) {
            return Res{app::core::Err, parsed.error()};
        }
        return Res{app::core::Ok,
                   toolResult(runHistorianQuery(reader, parsed.unwrap()))};
    }
    if (name == kEquipmentCommandTool) {
        // A read-only deployment must be provably unable to write: when writes
        // are off the tool is neither advertised (handleToolsList) nor callable
        // -- it is indistinguishable from an unknown tool (ADR-0024).
        if (!writeEnabled) {
            return Res{app::core::Err, McpErrorCode::MethodNotFound};
        }
        auto parsed = parseEquipmentCommandArgs(arguments);
        if (parsed.isErr()) {
            return Res{app::core::Err, parsed.error()};
        }
        auto ran = runEquipmentCommand(presenter, session, parsed.unwrap());
        if (ran.isErr()) {
            return Res{app::core::Err, ran.error()};
        }
        return Res{app::core::Ok, toolResult(ran.unwrap())};
    }
    return Res{app::core::Err, McpErrorCode::MethodNotFound};
}

}  // namespace app::mcp
