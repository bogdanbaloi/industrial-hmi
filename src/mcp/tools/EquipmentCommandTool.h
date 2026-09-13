#pragma once

#include "src/mcp/McpError.h"
#include "src/core/Result.h"

#include <nlohmann/json_fwd.hpp>

namespace app {
class DashboardPresenter;  // forward decl -- the operator handlers we reuse
}
namespace app::auth {
class Session;  // forward decl -- supplies the agent role for the auth gate
}

namespace app::mcp {

// MCP tool name, part of the wire contract (the LLM calls it by this string).
inline constexpr const char* kEquipmentCommandTool = "equipment_command";

/// The state-changing operations the tool exposes. Each maps 1:1 to an existing
/// `DashboardPresenter` operator handler -- the same entry point a human button
/// drives -- so the tool forks no control logic (ADR-0024).
enum class EquipmentCommand {
    Start,         ///< onStartClicked   -- Operator+
    Stop,          ///< onStopClicked    -- Operator+
    ResetRestart,  ///< onResetRestartClicked -- Maintenance+
};

/// The MCP tool descriptor (name + description + input schema) for `tools/list`.
[[nodiscard]] nlohmann::json equipmentCommandDescriptor();

/// Parse and validate raw tool arguments. Returns `InvalidParams` for a
/// non-object or a missing/unknown `command`. Error is a value at the boundary
/// (ADR-0014), never an exception.
[[nodiscard]] app::core::Result<EquipmentCommand, McpErrorCode>
parseEquipmentCommandArgs(const nlohmann::json& args);

/// Execute the command through the presenter, behind an explicit authorization
/// pre-check against the agent `session` (ADR-0024).
///
/// The presenter's own role gate (`checkRole`) treats a null session as
/// auth-disabled and passes through, and refuses a permitted-but-unauthorised
/// action with a silent `void` return. A write tool must not rely on that:
///   * a session with no authenticated agent is refused with `Unauthorized`
///     *before* the presenter is touched, closing the null-session pass-through;
///   * a role that lacks the permission is routed through the presenter (which
///     audits the FAILURE via the human path) and then reported as a structured
///     `Unauthorized` JSON-RPC error, never a misleading success.
/// On success the presenter performs the action and audits SUCCESS, and the tool
/// returns an acknowledgement payload.
[[nodiscard]] app::core::Result<nlohmann::json, McpErrorCode>
runEquipmentCommand(DashboardPresenter& presenter, const auth::Session& session,
                    EquipmentCommand command);

}  // namespace app::mcp
