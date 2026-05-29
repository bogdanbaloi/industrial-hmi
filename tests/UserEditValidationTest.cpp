// [utest->req~auth-002~1]
// Covers REQ-AUTH-002 (role handling) at the dialog seam: the pure
// username-canonicalisation + role<->dropdown-index helpers the admin
// user-edit dialog uses. Extracted from UserEditDialog's anonymous
// namespace into UserEditValidation.h so they test without GTK.

#include "src/gtk/view/dialogs/UserEditValidation.h"

#include <gtest/gtest.h>

namespace {

using app::auth::Role;
using app::view::useredit::indexFromRole;
using app::view::useredit::roleFromIndex;
using app::view::useredit::toLowerCanonical;

TEST(UserEditValidationTest, CanonicalLowercasesAscii) {
    EXPECT_EQ(toLowerCanonical("Admin"), "admin");
    EXPECT_EQ(toLowerCanonical("OPERATOR"), "operator");
    EXPECT_EQ(toLowerCanonical("aLiCe42"), "alice42");
    EXPECT_EQ(toLowerCanonical(""), "");
}

TEST(UserEditValidationTest, RoleIndexRoundTrips) {
    for (Role r : {Role::Operator, Role::Maintenance, Role::Admin}) {
        EXPECT_EQ(roleFromIndex(indexFromRole(r)), r);
    }
}

TEST(UserEditValidationTest, IndexMatchesDropdownOrder) {
    EXPECT_EQ(indexFromRole(Role::Operator), 0);
    EXPECT_EQ(indexFromRole(Role::Maintenance), 1);
    EXPECT_EQ(indexFromRole(Role::Admin), 2);
}

TEST(UserEditValidationTest, UnknownIndexFallsBackToLeastPrivileged) {
    // Defence in depth: a UI glitch must never grant elevated access.
    EXPECT_EQ(roleFromIndex(99), Role::Operator);
    EXPECT_EQ(roleFromIndex(-1), Role::Operator);
}

}  // namespace
