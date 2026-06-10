#pragma once

#include <cstdint>
#include <string_view>

namespace app::auth {

/// Coarse-grained access levels modeled on the standard industrial
/// shop-floor hierarchy. Three roles is the sweet spot operators in the
/// field actually use -- finer-grained permission matrices look good on
/// slides but become an HR nightmare to administer.
///
/// Ordered numerically so higher integer == strictly broader access.
/// Permission checks read as a single comparison: `role >= required`.
/// New roles slot in by re-numbering; the schema stores the integer,
/// not the name, so a future "Auditor" between Operator and Maintenance
/// is a one-line migration.
enum class Role : std::uint8_t {
    /// Default for the shift staff: view dashboard, start / stop a
    /// running line, dismiss alerts. Cannot reset the line, recalibrate,
    /// or touch product data.
    Operator    = 0,

    /// Field-technician scope: everything Operator has, plus
    /// calibration, system reset, and product CRUD. Cannot manage
    /// users or audit log retention.
    Maintenance = 1,

    /// Site administrator: every action the binary exposes, including
    /// creating / disabling users and viewing the audit log.
    Admin       = 2,
};

/// Wire / log stable name for a role. Kept short + uppercase so audit
/// rows read cleanly with a fixed-width column. The string form is
/// only for display; persisted state always uses the integer value.
[[nodiscard]] constexpr std::string_view roleName(Role r) noexcept {
    switch (r) {
        case Role::Operator:    return "OPERATOR";
        case Role::Maintenance: return "MAINTENANCE";
        case Role::Admin:       return "ADMIN";
    }
    return "UNKNOWN";
}

/// Parse the name produced by `roleName`. Tolerates unknown strings by
/// returning the lowest privilege -- a corrupted audit row should
/// degrade safely rather than escalate.
[[nodiscard]] constexpr Role parseRole(std::string_view s) noexcept {
    if (s == "ADMIN")       return Role::Admin;
    if (s == "MAINTENANCE") return Role::Maintenance;
    return Role::Operator;
}

/// Permissions are derived from the role's numeric ordering. Each
/// helper documents the operation it gates. Naming follows
/// `can<Verb><Noun>` so call sites read as English at the gating point.

[[nodiscard]] constexpr bool canStartStop(Role r) noexcept {
    return r >= Role::Operator;
}

[[nodiscard]] constexpr bool canCalibrate(Role r) noexcept {
    return r >= Role::Maintenance;
}

[[nodiscard]] constexpr bool canResetSystem(Role r) noexcept {
    return r >= Role::Maintenance;
}

[[nodiscard]] constexpr bool canInjectFault(Role r) noexcept {
    // Fault injection drives the system to the ERROR safe-state; same
    // Maintenance+ threshold as Reset/Calibrate (a deliberate
    // operator-visible disruption, not an everyday Operator action).
    return r >= Role::Maintenance;
}

[[nodiscard]] constexpr bool canEditProducts(Role r) noexcept {
    return r >= Role::Maintenance;
}

[[nodiscard]] constexpr bool canManageUsers(Role r) noexcept {
    return r >= Role::Admin;
}

[[nodiscard]] constexpr bool canViewAuditLog(Role r) noexcept {
    return r >= Role::Admin;
}

[[nodiscard]] constexpr bool canDismissAlerts(Role r) noexcept {
    return r >= Role::Operator;
}

}  // namespace app::auth
