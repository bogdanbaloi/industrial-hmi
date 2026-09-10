#pragma once

#include "src/presenter/AlertCenter.h"

#include <nlohmann/json_fwd.hpp>

namespace app::mcp {

// MCP tool name, part of the wire contract (the LLM calls it by this string).
inline constexpr const char* kAlarmsSnapshotTool = "alarms_snapshot";

/// The MCP tool descriptor (name + description + input schema) for `tools/list`.
[[nodiscard]] nlohmann::json alarmsSnapshotDescriptor();

/// Execute the tool: the current active alarms as a JSON array. A thin,
/// read-only wrapper over `AlertCenter::snapshot()` -- no logic of its own.
[[nodiscard]] nlohmann::json runAlarmsSnapshot(const presenter::AlertCenter& alerts);

}  // namespace app::mcp
