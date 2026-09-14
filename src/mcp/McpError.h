#pragma once

#include "src/core/Result.h"

#include <string>
#include <string_view>

namespace app::mcp {

// JSON-RPC 2.0 error codes we surface. The numeric values are mandated by the
// spec, so they live as named constants (clang-tidy's magic-number allowlist
// does not cover them, and naming them documents intent at the call site).
inline constexpr int kJsonRpcParseError     = -32700;
inline constexpr int kJsonRpcMethodNotFound = -32601;
inline constexpr int kJsonRpcInvalidParams  = -32602;
inline constexpr int kJsonRpcInternalError  = -32603;

// Boundary error kind. Tool handlers return these as values (ADR-0014, Result
// at boundaries) rather than throwing across the process boundary; the server
// maps one to a JSON-RPC error object.
enum class McpErrorCode {
    ParseError,      ///< malformed JSON on the wire
    MethodNotFound,  ///< unknown JSON-RPC method or unknown tool name
    InvalidParams,   ///< a handler rejected its arguments
    Unauthorized,    ///< the agent's role may not perform this action (ADR-0024)
    InvalidState,    ///< the action is not valid in the current system state
};

/// Spec-mandated JSON-RPC numeric code for a boundary error kind.
[[nodiscard]] constexpr int jsonRpcCode(McpErrorCode code) noexcept {
    switch (code) {
        case McpErrorCode::ParseError:
            return kJsonRpcParseError;
        case McpErrorCode::MethodNotFound:
            return kJsonRpcMethodNotFound;
        // JSON-RPC 2.0 defines no standard authorization or state code; the
        // reserved codes are transport-level. Unauthorized / InvalidState reuse
        // the invalid-params code with a distinct message (see defaultMessage)
        // rather than invent an application code an MCP client would not
        // understand (ADR-0024).
        case McpErrorCode::InvalidParams:
        case McpErrorCode::Unauthorized:
        case McpErrorCode::InvalidState:
            return kJsonRpcInvalidParams;
    }
    return kJsonRpcInternalError;
}

/// Default human-readable message for a boundary error kind.
[[nodiscard]] constexpr std::string_view defaultMessage(McpErrorCode code) noexcept {
    switch (code) {
        case McpErrorCode::ParseError:
            return "Parse error";
        case McpErrorCode::MethodNotFound:
            return "Method not found";
        case McpErrorCode::InvalidParams:
            return "Invalid params";
        case McpErrorCode::Unauthorized:
            return "Unauthorized: the agent role may not perform this action";
        case McpErrorCode::InvalidState:
            return "Invalid state for the requested action";
    }
    return "Internal error";
}

}  // namespace app::mcp

namespace app::core {

// errorToString specialization for McpErrorCode, required by the Result<T, E>
// contract so Result<*, McpErrorCode>::errorMessage/unwrap link. Same inline
// pattern as the core error enums in ErrorHandling.h.
template <>
inline std::string Result<int, app::mcp::McpErrorCode>::errorToString(
    app::mcp::McpErrorCode error) {
    return std::string(app::mcp::defaultMessage(error));
}

}  // namespace app::core
