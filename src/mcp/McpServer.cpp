#include "src/mcp/McpServer.h"

#include "src/mcp/McpError.h"
#include "src/mcp/McpProtocol.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace app::mcp {

namespace {

void writeResponse(std::ostream& output, const nlohmann::json& response) {
    output << response.dump() << '\n';
    output.flush();
}

nlohmann::json successEnvelope(const nlohmann::json& id, nlohmann::json result) {
    return {{"jsonrpc", kJsonRpcVersion},
            {"id", id},
            {"result", std::move(result)}};
}

nlohmann::json errorEnvelope(const nlohmann::json& id, int code,
                             std::string_view message) {
    return {{"jsonrpc", kJsonRpcVersion},
            {"id", id},
            {"error", {{"code", code}, {"message", std::string(message)}}}};
}

void processLine(const std::string& line, const presenter::AlertCenter& alerts,
                 historian::HistoryReader& reader, DashboardPresenter& presenter,
                 const auth::Session& session, bool writeEnabled,
                 std::ostream& output) {
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error&) {
        // Malformed JSON: reply with a spec parse error (id null), never silent.
        writeResponse(output, errorEnvelope(nullptr, kJsonRpcParseError,
                                            defaultMessage(McpErrorCode::ParseError)));
        return;
    }

    // A request with no id is a notification: JSON-RPC forbids a response.
    if (!request.contains("id")) {
        return;
    }

    const nlohmann::json id     = request.at("id");
    const std::string    method = request.value("method", std::string{});
    const nlohmann::json params = request.contains("params")
                                      ? request.at("params")
                                      : nlohmann::json::object();

    if (method == kMethodInitialize) {
        writeResponse(output, successEnvelope(id, handleInitialize(params)));
        return;
    }
    if (method == kMethodToolsList) {
        writeResponse(output, successEnvelope(id, handleToolsList(writeEnabled)));
        return;
    }
    if (method == kMethodToolsCall) {
        auto result = handleToolsCall(params, alerts, reader, presenter, session,
                                      writeEnabled);
        if (result.isOk()) {
            writeResponse(output, successEnvelope(id, result.unwrap()));
        } else {
            const McpErrorCode code = result.error();
            writeResponse(output,
                          errorEnvelope(id, jsonRpcCode(code), defaultMessage(code)));
        }
        return;
    }

    writeResponse(output, errorEnvelope(id, kJsonRpcMethodNotFound,
                                        defaultMessage(McpErrorCode::MethodNotFound)));
}

}  // namespace

McpServer::McpServer(const presenter::AlertCenter& alerts,
                     historian::HistoryReader& reader,
                     DashboardPresenter& presenter,
                     const auth::Session& session, bool writeEnabled)
    : alerts_(alerts),
      reader_(reader),
      presenter_(presenter),
      session_(session),
      writeEnabled_(writeEnabled) {}

int McpServer::run(std::istream& input, std::ostream& output) {
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        processLine(line, alerts_, reader_, presenter_, session_, writeEnabled_,
                    output);
    }
    return 0;
}

int McpServer::run() { return run(std::cin, std::cout); }

}  // namespace app::mcp
