#pragma once

#include "src/auth/User.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::auth {

/// Storage abstraction for User rows.
///
/// SOLID:
///   * S -- persistence only. No hashing, no policy, no audit.
///   * O -- new backends (Postgres, LDAP, OAuth claim cache) plug in
///     without touching AuthService.
///   * D -- AuthService depends on this interface; tests inject an
///     in-memory fake.
class UserRepository {
public:
    virtual ~UserRepository() = default;

    UserRepository(const UserRepository&)            = delete;
    UserRepository& operator=(const UserRepository&) = delete;
    UserRepository(UserRepository&&)                 = delete;
    UserRepository& operator=(UserRepository&&)      = delete;

    /// Look up by case-insensitive username. Returns `nullopt` when no
    /// row matches; AuthService maps that to a generic "invalid
    /// credentials" response so the caller cannot distinguish "wrong
    /// user" from "wrong password" (user-enumeration mitigation).
    [[nodiscard]] virtual std::optional<User>
    findByUsername(std::string_view username) = 0;

    /// Enumerate every row, enabled or not. Used by the future user
    /// management page; not on the login hot path.
    [[nodiscard]] virtual std::vector<User> listAll() = 0;

    /// Insert a fresh row. Returns the populated User (with `id` and
    /// `createdAt` filled in) on success, `nullopt` on duplicate
    /// username. The hash + role must already be set by the caller;
    /// the repository does not derive them.
    [[nodiscard]] virtual std::optional<User> create(const User& user) = 0;

    /// Replace an existing row's mutable fields. Returns false when
    /// the id is not found. Username changes are NOT supported by
    /// design -- a renamed user breaks the audit trail.
    virtual bool update(const User& user) = 0;

    /// Total row count. Used at startup to decide whether to seed the
    /// demo user set.
    [[nodiscard]] virtual std::size_t count() const = 0;

protected:
    UserRepository() = default;
};

}  // namespace app::auth
