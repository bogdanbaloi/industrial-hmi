#pragma once

#include "src/auth/User.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::auth {

/// Avatar payload returned by `UserRepository::getAvatar`. Pairs the
/// raw bytes with the MIME so the renderer knows which decoder to feed.
struct Avatar {
    std::vector<std::uint8_t> bytes;
    std::string               mime;     // "image/png" or "image/jpeg"
};

/// Maximum bytes accepted by `setAvatar` -- a hard ceiling enforced at
/// the persistence boundary so an oversized upload never reaches SQLite.
/// 256 KiB comfortably fits a 256x256 PNG photo at typical compression
/// while keeping the row small enough that `listAll` (no blobs) stays
/// the only call that ever touches a large payload.
inline constexpr std::size_t kMaxAvatarBytes = 256 * 1024;

/// Storage abstraction for User rows.
///
/// SOLID:
///   * S -- persistence only. No hashing, no policy, no audit.
///   * O -- new backends (Postgres, LDAP, OAuth claim cache) plug in
///     without touching AuthService or UsersPresenter.
///   * I -- avatar payload is split off the User struct + the CRUD
///     methods; a backend that ignores avatars implements them as a
///     no-op / empty without paying the cost elsewhere.
///   * D -- AuthService and UsersPresenter depend on this interface;
///     tests inject an in-memory fake.
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
    ///
    /// Does NOT fetch the avatar blob -- callers that need it
    /// (UsersPage row renderer, badge widget) issue a follow-up
    /// `getAvatar(user.id)` so the login hot path stays cheap.
    [[nodiscard]] virtual std::optional<User>
    findByUsername(std::string_view username) = 0;

    /// Lookup by primary key. Same "no blob" contract as
    /// `findByUsername`. Used by the management UI when an action
    /// refers to a row by id (Edit / Delete / Reset Password).
    [[nodiscard]] virtual std::optional<User>
    findById(std::int64_t id) = 0;

    /// Enumerate every row, enabled or not. Used by the user management
    /// page; not on the login hot path. No avatar blobs (see contract
    /// above).
    [[nodiscard]] virtual std::vector<User> listAll() = 0;

    /// Insert a fresh row. Returns the populated User (with `id`,
    /// `createdAt`, `updatedAt` filled in) on success, `nullopt` on
    /// duplicate username. The hash + role must already be set by the
    /// caller; the repository does not derive them. Avatar is set
    /// separately via `setAvatar` after the row exists (id required).
    [[nodiscard]] virtual std::optional<User> create(const User& user) = 0;

    /// Replace an existing row's mutable fields (password hash,
    /// display name, role, enabled flag). Refreshes `updatedAt`.
    /// Returns false when the id is not found. Username changes are
    /// NOT supported by design -- a renamed user breaks the audit
    /// trail's denormalised username column.
    virtual bool update(const User& user) = 0;

    /// Hard-delete a row. The audit trail keeps its denormalised
    /// username + role columns so historical entries remain readable
    /// even after the live user is gone. Returns false when the id is
    /// not found.
    ///
    /// Callers (UsersPresenter) MUST refuse to delete the currently
    /// signed-in account themselves -- the repository does not know
    /// who is logged in. Avatar blob is removed implicitly with the row.
    virtual bool remove(std::int64_t id) = 0;

    /// Replace the avatar blob for a user. `mime` must be one of
    /// "image/png" or "image/jpeg"; `bytes.size()` must be > 0 and
    /// <= `kMaxAvatarBytes`. Returns false on validation failure or
    /// when the id is not found. Also bumps `updatedAt` so the row
    /// shows fresh in "recently modified" sorts.
    virtual bool setAvatar(std::int64_t id,
                           const std::vector<std::uint8_t>& bytes,
                           std::string_view mime) = 0;

    /// Clear the avatar blob for a user. Subsequent `findById` calls
    /// will return an empty `avatarMime`, telling the UI to render
    /// generated initials instead. Returns false when the id is not
    /// found.
    virtual bool clearAvatar(std::int64_t id) = 0;

    /// Fetch the avatar payload. Returns `nullopt` when the id is not
    /// found OR when the user has no avatar uploaded. Callers that
    /// already know the avatar is absent (via `User::avatarMime`
    /// empty) can skip the round trip.
    [[nodiscard]] virtual std::optional<Avatar>
    getAvatar(std::int64_t id) = 0;

    /// Total row count. Used at startup to decide whether to seed the
    /// demo user set.
    [[nodiscard]] virtual std::size_t count() const = 0;

protected:
    UserRepository() = default;
};

}  // namespace app::auth
