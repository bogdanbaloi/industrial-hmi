#pragma once

#include "src/historian/HistoryReader.h"
#include "src/mcp/McpError.h"
#include "src/core/Result.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>

namespace app::mcp {

// MCP tool name, part of the wire contract.
inline constexpr const char* kHistorianQueryTool = "historian_query";

/// Validated arguments for a historian query. A plain struct so parsing (which
/// can fail) is separated from execution (which cannot, given valid args).
struct HistorianQueryArgs {
    historian::FieldKind field{historian::FieldKind::QualityPassRate};
    std::uint32_t        entityId{0};
    historian::QueryRange range{};
};

/// The MCP tool descriptor for `tools/list`.
[[nodiscard]] nlohmann::json historianQueryDescriptor();

/// Parse and validate raw tool arguments. Returns `InvalidParams` for a
/// non-object, a missing/unknown `field`, and clamps `limit` to the store cap.
/// Error is a value at the boundary (ADR-0014), never an exception.
[[nodiscard]] app::core::Result<HistorianQueryArgs, McpErrorCode>
parseHistorianArgs(const nlohmann::json& args);

/// Execute the query and return the samples as a JSON array. A thin wrapper
/// over `HistoryReader::query()`.
[[nodiscard]] nlohmann::json
runHistorianQuery(historian::HistoryReader& reader, const HistorianQueryArgs& args);

}  // namespace app::mcp
