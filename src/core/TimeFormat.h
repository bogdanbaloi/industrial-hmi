#pragma once

#include <string>
#include <string_view>

namespace app::core {

/// Convert an ISO 8601 UTC stamp ("YYYY-MM-DDTHH:MM:SSZ") into a
/// friendlier local-time display ("YYYY-MM-DD HH:MM:SS" in the host's
/// local timezone).
///
/// Why local for display while keeping UTC in storage: audit + history
/// trails are archival + may cross deployment timezones, so the DB
/// stamps UTC for cross-site consistency. The UI shows local because
/// operators read it against the sidebar clock + their own watch.
///
/// Falls back to the raw input on parse failure -- a corrupted row
/// still renders something visible so operators notice data-integrity
/// issues rather than seeing a blank cell.
///
/// Pure-mechanism utility -- no logging, no policy, no allocations
/// beyond the returned string. Lives in `core` so both the audit page
/// and the user management page (any future timestamp-rendering
/// surface) consume the same implementation rather than duplicating
/// the parsing dance.
[[nodiscard]] std::string formatIso8601Local(std::string_view iso8601Utc);

}  // namespace app::core
