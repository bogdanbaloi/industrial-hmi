#pragma once

#include "src/mcp/McpError.h"
#include "src/core/Result.h"

#include <nlohmann/json.hpp>

namespace app::presenter {
class AlertCenter;
}
namespace app::historian {
class HistoryReader;
}

namespace app::mcp {

// MCP protocol version we implement, and the server identity reported in the
// initialize handshake. Part of the wire contract.
inline constexpr const char* kMcpProtocolVersion = "2024-11-05";
inline constexpr const char* kServerName         = "industrial-hmi-mcp";
inline constexpr const char* kServerVersion      = "1.2.0";

// JSON-RPC version string and the MCP method names we route on (dispatch keys,
// so they are named rather than spelled inline at the call sites).
inline constexpr const char* kJsonRpcVersion   = "2.0";
inline constexpr const char* kMethodInitialize = "initialize";
inline constexpr const char* kMethodToolsList  = "tools/list";
inline constexpr const char* kMethodToolsCall  = "tools/call";

/// `initialize` handshake result: our protocol version, declared capabilities
/// (we serve tools) and server identity. Client params are accepted as-is.
[[nodiscard]] nlohmann::json handleInitialize(const nlohmann::json& params);

/// `tools/list` result: the descriptors of the two read-only tools.
[[nodiscard]] nlohmann::json handleToolsList();

/// `tools/call` dispatch. Routes on `params["name"]` to the matching tool,
/// forwarding `params["arguments"]`. Returns the MCP content result on success,
/// or a boundary error value (ADR-0014): MethodNotFound for an unknown tool,
/// InvalidParams when a tool rejects its arguments.
[[nodiscard]] app::core::Result<nlohmann::json, McpErrorCode>
handleToolsCall(const nlohmann::json& params,
                const presenter::AlertCenter& alerts,
                historian::HistoryReader& reader);

}  // namespace app::mcp
