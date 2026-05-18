#pragma once

#include "src/auth/AuditLogger.h"
#include "src/auth/PasswordHasher.h"
#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/auth/User.h"
#include "src/auth/UserRepository.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::presenter {

/// Outcome of every UsersPresenter mutation. A single enum so the UI
/// can switch on it for distinct messages, and so audit rows record a
/// canonical reason rather than a free-form string.
///
/// `Forbidden` is the RBAC "you're not allowed" verdict; it is
/// returned BEFORE the action runs and is recorded as a FAILURE audit
/// entry so the operator + the admin both see the rejection.
enum class UsersStatus : std::uint8_t {
    /// Mutation succeeded.
    Ok,

    /// Caller's role isn't enough for this action. UI surfaces a
    /// generic "permission denied" toast; the audit entry quotes the
    /// attempted verb so an admin can spot escalation attempts.
    Forbidden,

    /// New username collides with an existing row (case-insensitive).
    DuplicateUsername,

    /// Target id doesn't exist (update/delete/resetPassword on a row
    /// that was just removed by another admin -- rare but possible).
    NotFound,

    /// Input failed the presenter-side validation: empty fields,
    /// password under the minimum length, invalid characters, oversize
    /// avatar, unknown MIME. The UI is expected to validate in advance
    /// so the user sees an inline hint -- this status is the
    /// belt-and-braces fallback.
    ValidationFailed,

    /// Refused: cannot delete or disable yourself. Prevents an admin
    /// from accidentally locking the binary out of its only account.
    SelfMutationRefused,

    /// Old password supplied to `changeOwnPassword` didn't verify.
    /// Distinct from ValidationFailed so the UI can say "current
    /// password incorrect" specifically.
    WrongPassword,

    /// Argon2 hasher threw. Treated as a failure but logged + audited
    /// so an admin sees something went wrong at the crypto layer.
    HasherFailure,

    /// Storage layer refused (write failed, db corrupt). Logged.
    StorageFailure,
};

/// Façade for the user management UI. Pure C++ -- no GTK.
///
/// SOLID:
///   * S -- policy + RBAC + audit. Persistence stays in the repo;
///     hashing stays in the hasher; this layer composes them.
///   * O -- adding a new audit action is one new line in the
///     auditCreate/Update/... helpers; no method signatures change.
///   * I -- the public surface only exposes the verbs the UI needs
///     (list / CRUD / change own password / avatar). No "raw" hash
///     setter that would let a caller bypass policy.
///   * D -- depends on UserRepository / PasswordHasher / Session /
///     AuditLogger interfaces; tests inject in-memory fakes.
///
/// Threading: callers run on the GTK main thread (UsersPage uses
/// synchronous responses, no idle-marshalling). The repository's mutex
/// covers the storage path; Session's mutex covers reads of the
/// current user. No additional locking needed here.
class UsersPresenter {
public:
    UsersPresenter(app::auth::UserRepository& users,
                   app::auth::PasswordHasher& hasher,
                   app::auth::Session&        session,
                   app::auth::AuditLogger&    audit);

    UsersPresenter(const UsersPresenter&)            = delete;
    UsersPresenter& operator=(const UsersPresenter&) = delete;
    UsersPresenter(UsersPresenter&&)                 = delete;
    UsersPresenter& operator=(UsersPresenter&&)      = delete;
    ~UsersPresenter()                                = default;

    // --- read surface ---------------------------------------------------

    /// Enumerate every user row for the management table. Admin only.
    /// Returns empty vector when the caller doesn't have the role
    /// (defence-in-depth on top of the sidebar gating).
    [[nodiscard]] std::vector<app::auth::User> list() const;

    /// Fetch the avatar payload for a user. Any authenticated role can
    /// read avatars (the badge + the UsersPage row renderer both need
    /// to display them). Returns nullopt for "no avatar uploaded" OR
    /// for an unknown id.
    [[nodiscard]] std::optional<app::auth::Avatar>
    getAvatar(std::int64_t id) const;

    /// Snapshot of the currently signed-in user. Convenience proxy
    /// over Session so view widgets (UserBadge, ProfileDialog) can
    /// depend on a single object rather than juggling presenter +
    /// session refs. Returns nullopt when no one is signed in.
    [[nodiscard]] std::optional<app::auth::User> currentUser() const;

    // --- admin mutations ------------------------------------------------

    /// Create a new account. Admin only. Hashes the password before
    /// handing the row to the repository so the plaintext never
    /// touches storage. Records a USER/CREATE audit entry on success
    /// and a USER/CREATE failure on every reject reason (forbidden,
    /// validation, duplicate).
    UsersStatus create(std::string_view username,
                       std::string_view password,
                       app::auth::Role  role,
                       std::string_view displayName = {});

    /// Update display name + role + enabled flag for an existing user.
    /// Admin only. Password is NOT changed here -- callers use
    /// `resetPassword` for that so the audit trail records a distinct
    /// action. Refuses to disable the currently signed-in account
    /// (returns SelfMutationRefused).
    UsersStatus update(std::int64_t            id,
                       app::auth::Role         role,
                       std::string_view        displayName,
                       bool                    enabled);

    /// Hard-delete a user. Admin only. Refuses to delete the
    /// currently signed-in account (would lock the binary out of
    /// admin). The audit trail keeps the denormalised username so
    /// historical entries remain readable.
    UsersStatus remove(std::int64_t id);

    /// Replace a user's password (admin-initiated reset). Admin only.
    /// Distinct from `changeOwnPassword` in that the caller does NOT
    /// need to supply the existing password -- this is the "user
    /// forgot, admin resets" flow. Audited as USER/RESET_PASSWORD.
    UsersStatus resetPassword(std::int64_t     id,
                              std::string_view newPassword);

    // --- self-service mutations (any authenticated role) ----------------

    /// Change your own password. Verifies the old password against the
    /// stored hash before persisting the new one; returns
    /// WrongPassword on mismatch. Audited as USER/CHANGE_PASSWORD.
    UsersStatus changeOwnPassword(std::string_view oldPassword,
                                  std::string_view newPassword);

    /// Upload / replace your own avatar. Repository enforces the
    /// 256 KiB ceiling + the MIME whitelist; ValidationFailed bubbles
    /// up on either. Audited as USER/CHANGE_AVATAR.
    UsersStatus setOwnAvatar(const std::vector<std::uint8_t>& bytes,
                             std::string_view mime);

    /// Drop your own avatar -- the badge falls back to generated
    /// initials on the next render. Audited as USER/CLEAR_AVATAR.
    UsersStatus clearOwnAvatar();

private:
    app::auth::UserRepository& users_;
    app::auth::PasswordHasher& hasher_;
    app::auth::Session&        session_;
    app::auth::AuditLogger&    audit_;
};

/// Free-function helper for the UI to render a Status as a localised
/// message. Kept here (rather than in the view) so the mapping stays
/// next to the enum; the view just passes the result through gettext.
[[nodiscard]] std::string_view statusMessage(UsersStatus s);

}  // namespace app::presenter
