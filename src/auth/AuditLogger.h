#pragma once

#include "src/auth/AuditEvent.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace app::auth {

/// Range query parameters for audit log lookups. Mirrors the historian
/// pattern (from/to/limit) so the future audit page can reuse the same
/// time-range picker widget conceptually.
struct AuditQuery {
    /// ISO 8601 lower bound (inclusive). Empty == no lower bound.
    std::string fromTs;

    /// ISO 8601 upper bound (inclusive). Empty == no upper bound.
    std::string toTs;

    /// Optional category filter. Empty == all categories.
    std::string category;

    /// Optional action filter (e.g. "LOGIN", "CREATE", "RESET_PASSWORD").
    /// Empty == all actions. Compliance walks usually combine
    /// category + action ("USER + RESET_PASSWORD over the last 7
    /// days") so the read side accepts both filters independently
    /// even though most call sites pass one or the other.
    std::string action;

    /// Optional result filter -- "SUCCESS" / "FAILURE". Empty == both.
    /// Failure-only is the typical "show me suspicious activity"
    /// view (failed logins, refused RBAC actions, validation
    /// rejects).
    std::string result;

    /// Optional username filter. Empty == all users.
    std::string username;

    /// Cap on returned rows. The audit page renders into a fixed-row
    /// table; without a cap an admin could pull months of events and
    /// stall the UI thread. 0 == no explicit cap (CSV export path).
    static constexpr std::size_t kDefaultLimit = 500;
    std::size_t limit{kDefaultLimit};
};

/// Sink for operator-attributed actions.
///
/// SOLID:
///   * S -- persist audit rows. Nothing else. Decision of *what* to
///     log lives in the call sites (AuthService, presenters).
///   * O -- new categories / actions land as new strings; no
///     interface change.
///   * I -- write + query split into two paths (Logger writes, the
///     same concrete also exposes the read API for the upcoming
///     audit page). UI consumers will depend on the read surface
///     via a sibling interface in a follow-up if the split becomes
///     painful.
///   * D -- AuthService and presenters depend on this interface; a
///     `nullptr` pointer-to-AuditLogger turns logging into a no-op
///     at the call site so the same code path works whether audit
///     is enabled or not.
class AuditLogger {
public:
    virtual ~AuditLogger() = default;

    AuditLogger(const AuditLogger&)            = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;
    AuditLogger(AuditLogger&&)                 = delete;
    AuditLogger& operator=(AuditLogger&&)      = delete;

    /// Persist an event. If `event.timestamp` is empty the
    /// implementation stamps it before writing. Returns true on
    /// success; a failure is logged at error level but never
    /// thrown -- audit must not crash the application path that
    /// produced the event.
    virtual bool record(const AuditEvent& event) = 0;

    /// Pull a range of events for display / export.
    [[nodiscard]] virtual std::vector<AuditEvent>
    query(const AuditQuery& q) = 0;

    /// Total row count. Used by the audit page footer.
    [[nodiscard]] virtual std::size_t totalEvents() const = 0;

protected:
    AuditLogger() = default;
};

}  // namespace app::auth
