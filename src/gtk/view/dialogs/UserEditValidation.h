#pragma once

#include "src/auth/Role.h"

#include <cctype>
#include <string>
#include <string_view>

namespace app::view::useredit {

/// Role dropdown indices -- match the append order in
/// UserEditDialog::buildUi(). Extracted here (out of the dialog's
/// anonymous namespace) so the index<->Role mapping is unit-testable
/// without constructing a GTK widget.
inline constexpr int kRoleIndexOperator    = 0;
inline constexpr int kRoleIndexMaintenance = 1;
inline constexpr int kRoleIndexAdmin       = 2;

/// Lower-case canonicalisation used to compare / store usernames so
/// "Admin" and "admin" are the same account. ASCII-only by contract
/// (the username field rejects non-ASCII upstream).
[[nodiscard]] inline std::string toLowerCanonical(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// Map a role dropdown index to the Role enum. Unknown indices fall
/// back to the least-privileged role (Operator) -- defence in depth:
/// a UI glitch must never silently grant elevated access.
[[nodiscard]] inline app::auth::Role roleFromIndex(int idx) {
    using app::auth::Role;
    switch (idx) {
        case kRoleIndexAdmin:       return Role::Admin;
        case kRoleIndexMaintenance: return Role::Maintenance;
        default:                    return Role::Operator;
    }
}

/// Inverse of roleFromIndex -- map a Role back to its dropdown index
/// so the dialog can preselect the current role when editing.
[[nodiscard]] inline int indexFromRole(app::auth::Role r) {
    using app::auth::Role;
    switch (r) {
        case Role::Admin:       return kRoleIndexAdmin;
        case Role::Maintenance: return kRoleIndexMaintenance;
        case Role::Operator:    return kRoleIndexOperator;
    }
    return kRoleIndexOperator;
}

}  // namespace app::view::useredit
