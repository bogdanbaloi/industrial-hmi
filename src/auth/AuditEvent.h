#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace app::auth {

/// One row in the audit log.
///
/// Stored fields denormalise the user (username + role string) so the
/// trail survives even if the underlying user is later deleted. A
/// forensic auditor walking the log six months from now does not need
/// the live users table to make sense of an entry.
///
/// `category` is a coarse bucket the UI can filter on (AUTH, PRODUCTION,
/// EQUIPMENT, PRODUCT, USER); `action` is the specific verb within
/// that bucket (LOGIN, START, ENABLE, ADD, etc.). Both are short
/// stable codes -- localisation happens at render time, not at write
/// time, so historical entries don't drift with translation updates.
struct AuditEvent {
    /// ISO 8601 UTC timestamp. Filled in by the logger if left empty
    /// at write time, so callers can construct an Event with just the
    /// semantic fields and let the persistence layer stamp it.
    std::string  timestamp;

    /// Who did it. Pulled from Session at call site; empty when the
    /// action is system-initiated (e.g. a bootstrap seed).
    std::string  username;

    /// Role name at the time of the action (OPERATOR / MAINTENANCE /
    /// ADMIN). Denormalised for the same reason as username.
    std::string  role;

    /// Coarse bucket -- see header docs.
    std::string  category;

    /// Specific verb -- see header docs.
    std::string  action;

    /// Free-form human-readable detail. Examples:
    ///   "equipment id 2 -> ON"
    ///   "product PROD-001 stock 250 -> 280"
    ///   "wrong password"
    std::string  details;

    /// SUCCESS / FAILURE. Two-state on purpose -- a long enum here
    /// would tempt UI filters that the audit page doesn't need.
    std::string  result;
};

/// Convenience: well-known category names. Kept as static constexpr
/// strings rather than an enum so callers can pass them straight to
/// the DTO without an enum-to-string mapping every time.
namespace category {
inline constexpr std::string_view kAuth       = "AUTH";
inline constexpr std::string_view kProduction = "PRODUCTION";
inline constexpr std::string_view kEquipment  = "EQUIPMENT";
inline constexpr std::string_view kProduct    = "PRODUCT";
inline constexpr std::string_view kUser       = "USER";
inline constexpr std::string_view kAlert      = "ALERT";
}  // namespace category

namespace result {
inline constexpr std::string_view kSuccess = "SUCCESS";
inline constexpr std::string_view kFailure = "FAILURE";
}  // namespace result

}  // namespace app::auth
