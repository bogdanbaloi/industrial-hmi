#include "src/presenter/UsersPresenter.h"

#include "src/auth/AuditEvent.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <format>
#include <string>
#include <string_view>

namespace app::presenter {

namespace {

using app::auth::AuditEvent;
using app::auth::Role;
using app::auth::User;
using app::auth::canManageUsers;
using app::auth::roleName;

// Stable verb codes for the audit log. Kept here next to the only
// emitter so adding a new action lands in one place.
constexpr std::string_view kActionCreate         = "CREATE";
constexpr std::string_view kActionUpdate         = "UPDATE";
constexpr std::string_view kActionDelete         = "DELETE";
constexpr std::string_view kActionResetPassword  = "RESET_PASSWORD";
constexpr std::string_view kActionChangePassword = "CHANGE_PASSWORD";
constexpr std::string_view kActionChangeAvatar   = "CHANGE_AVATAR";
constexpr std::string_view kActionClearAvatar    = "CLEAR_AVATAR";

// Policy thresholds. Magic numbers go up here as named constants so a
// future tightening (8 -> 12 chars) is a one-line change + clear in
// the diff what the previous value was.
constexpr std::size_t kMinPasswordLen = 8;
constexpr std::size_t kMinUsernameLen = 3;
constexpr std::size_t kMaxUsernameLen = 32;
constexpr std::size_t kMaxDisplayLen  = 128;

bool isAllowedUsernameChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (std::isalnum(u) != 0) || c == '_' || c == '-' || c == '.';
}

bool isValidUsername(std::string_view s) {
    if (s.size() < kMinUsernameLen || s.size() > kMaxUsernameLen) {
        return false;
    }
    // First character must be a letter -- "_root" / "42alice" look like
    // edge-case usernames that complicate the audit log + on-screen
    // labels. Cheap to enforce, valuable as defence-in-depth.
    if (std::isalpha(static_cast<unsigned char>(s.front())) == 0) {
        return false;
    }
    for (char c : s) {
        if (!isAllowedUsernameChar(c)) return false;
    }
    return true;
}

bool isValidPassword(std::string_view s) {
    return s.size() >= kMinPasswordLen;
}

bool isValidDisplayName(std::string_view s) {
    return s.size() <= kMaxDisplayLen;  // empty is fine
}

/// Lower-case a username so storage normalisation matches what the
/// repository's COLLATE NOCASE enforces. Done in the presenter so the
/// audit row records the canonical form, not whatever the operator
/// typed.
std::string canonicalUsername(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(static_cast<char>(std::tolower(u)));
    }
    return out;
}

/// Build an audit row from a presenter context. Pulls the actor's
/// identity off Session so a presenter call site cannot accidentally
/// attribute an action to the wrong user.
AuditEvent makeEvent(app::auth::Session&  session,
                     std::string_view     action,
                     std::string_view     details,
                     bool                 success) {
    AuditEvent e;
    e.category = app::auth::category::kUser;
    e.action   = std::string{action};
    e.details  = std::string{details};
    e.result   = std::string{success
                                 ? app::auth::result::kSuccess
                                 : app::auth::result::kFailure};
    const auto current = session.currentUser();
    if (current.has_value()) {
        e.username = current->username;
        e.role     = std::string{roleName(current->role)};
    } else {
        e.username = "unknown";
        e.role     = "UNKNOWN";
    }
    // `timestamp` left empty -- the audit logger stamps it on write.
    return e;
}

}  // namespace

UsersPresenter::UsersPresenter(app::auth::UserRepository& users,
                               app::auth::PasswordHasher& hasher,
                               app::auth::Session&        session,
                               app::auth::AuditLogger&    audit)
    : users_(users), hasher_(hasher), session_(session), audit_(audit) {}

std::vector<User> UsersPresenter::list() const {
    const auto me = session_.currentUser();
    if (!me.has_value() || !canManageUsers(me->role)) {
        // No audit on a silent read -- list() is called repeatedly by
        // the page refresh and would flood the log otherwise. The
        // sidebar gating already prevents non-admins from reaching
        // here in the normal flow.
        return {};
    }
    return users_.listAll();
}

std::optional<app::auth::Avatar>
UsersPresenter::getAvatar(std::int64_t id) const {
    // Any authenticated role can read avatars -- the badge widget on
    // every page calls this for the currently-signed-in user, and the
    // UsersPage row renderer (admin only by sidebar gating) calls it
    // per row. No audit on read.
    if (!session_.isAuthenticated()) return std::nullopt;
    return users_.getAvatar(id);
}

UsersStatus UsersPresenter::create(std::string_view username,
                                   std::string_view password,
                                   Role             role,
                                   std::string_view displayName) {
    const auto me = session_.currentUser();
    if (!me.has_value() || !canManageUsers(me->role)) {
        audit_.record(makeEvent(session_, kActionCreate,
                                std::format("attempted create username={}",
                                            std::string{username}),
                                false));
        return UsersStatus::Forbidden;
    }
    if (!isValidUsername(username) || !isValidPassword(password)
            || !isValidDisplayName(displayName)) {
        audit_.record(makeEvent(session_, kActionCreate,
                                std::format("invalid input username={}",
                                            std::string{username}),
                                false));
        return UsersStatus::ValidationFailed;
    }

    User u;
    u.username    = canonicalUsername(username);
    u.role        = role;
    u.enabled     = true;
    u.displayName = std::string{displayName};
    try {
        u.passwordHash = hasher_.hash(password);
    } catch (const std::exception&) {
        audit_.record(makeEvent(session_, kActionCreate,
                                std::format("hasher failure username={}",
                                            u.username),
                                false));
        return UsersStatus::HasherFailure;
    }

    const auto created = users_.create(u);
    if (!created.has_value()) {
        // Likely the UNIQUE constraint on (lower-cased) username.
        audit_.record(makeEvent(session_, kActionCreate,
                                std::format("duplicate username={}",
                                            u.username),
                                false));
        return UsersStatus::DuplicateUsername;
    }

    audit_.record(makeEvent(session_, kActionCreate,
                            std::format("id={} username={} role={}",
                                        created->id, created->username,
                                        roleName(created->role)),
                            true));
    return UsersStatus::Ok;
}

UsersStatus UsersPresenter::update(std::int64_t     id,
                                   Role             role,
                                   std::string_view displayName,
                                   bool             enabled) {
    const auto me = session_.currentUser();
    if (!me.has_value() || !canManageUsers(me->role)) {
        audit_.record(makeEvent(session_, kActionUpdate,
                                std::format("attempted update id={}", id),
                                false));
        return UsersStatus::Forbidden;
    }
    if (!isValidDisplayName(displayName)) {
        audit_.record(makeEvent(session_, kActionUpdate,
                                std::format("invalid input id={}", id),
                                false));
        return UsersStatus::ValidationFailed;
    }

    auto target = users_.findById(id);
    if (!target.has_value()) {
        audit_.record(makeEvent(session_, kActionUpdate,
                                std::format("not found id={}", id),
                                false));
        return UsersStatus::NotFound;
    }

    // Refuse to disable / demote yourself. Deletion is a separate path
    // (`remove`) so we only need to gate enabled=false here.
    if (target->id == me->id && !enabled) {
        audit_.record(makeEvent(session_, kActionUpdate,
                                std::format("self-disable refused id={}", id),
                                false));
        return UsersStatus::SelfMutationRefused;
    }

    target->role        = role;
    target->displayName = std::string{displayName};
    target->enabled     = enabled;
    if (!users_.update(*target)) {
        audit_.record(makeEvent(session_, kActionUpdate,
                                std::format("storage failure id={}", id),
                                false));
        return UsersStatus::StorageFailure;
    }

    audit_.record(makeEvent(session_, kActionUpdate,
                            std::format("id={} username={} role={} enabled={}",
                                        target->id, target->username,
                                        roleName(target->role),
                                        target->enabled),
                            true));
    return UsersStatus::Ok;
}

UsersStatus UsersPresenter::remove(std::int64_t id) {
    const auto me = session_.currentUser();
    if (!me.has_value() || !canManageUsers(me->role)) {
        audit_.record(makeEvent(session_, kActionDelete,
                                std::format("attempted delete id={}", id),
                                false));
        return UsersStatus::Forbidden;
    }
    if (me->id == id) {
        // Deleting the currently signed-in admin would orphan the
        // session AND potentially leave the binary with no admin at
        // all. UI also guards this; defence-in-depth here.
        audit_.record(makeEvent(session_, kActionDelete,
                                std::format("self-delete refused id={}", id),
                                false));
        return UsersStatus::SelfMutationRefused;
    }

    const auto target = users_.findById(id);
    if (!target.has_value()) {
        audit_.record(makeEvent(session_, kActionDelete,
                                std::format("not found id={}", id),
                                false));
        return UsersStatus::NotFound;
    }
    if (!users_.remove(id)) {
        audit_.record(makeEvent(session_, kActionDelete,
                                std::format("storage failure id={}", id),
                                false));
        return UsersStatus::StorageFailure;
    }

    audit_.record(makeEvent(session_, kActionDelete,
                            std::format("id={} username={} role={}",
                                        target->id, target->username,
                                        roleName(target->role)),
                            true));
    return UsersStatus::Ok;
}

UsersStatus UsersPresenter::resetPassword(std::int64_t     id,
                                          std::string_view newPassword) {
    const auto me = session_.currentUser();
    if (!me.has_value() || !canManageUsers(me->role)) {
        audit_.record(makeEvent(session_, kActionResetPassword,
                                std::format("attempted reset id={}", id),
                                false));
        return UsersStatus::Forbidden;
    }
    if (!isValidPassword(newPassword)) {
        audit_.record(makeEvent(session_, kActionResetPassword,
                                std::format("invalid input id={}", id),
                                false));
        return UsersStatus::ValidationFailed;
    }

    auto target = users_.findById(id);
    if (!target.has_value()) {
        audit_.record(makeEvent(session_, kActionResetPassword,
                                std::format("not found id={}", id),
                                false));
        return UsersStatus::NotFound;
    }

    try {
        target->passwordHash = hasher_.hash(newPassword);
    } catch (const std::exception&) {
        audit_.record(makeEvent(session_, kActionResetPassword,
                                std::format("hasher failure id={}", id),
                                false));
        return UsersStatus::HasherFailure;
    }
    if (!users_.update(*target)) {
        audit_.record(makeEvent(session_, kActionResetPassword,
                                std::format("storage failure id={}", id),
                                false));
        return UsersStatus::StorageFailure;
    }

    audit_.record(makeEvent(session_, kActionResetPassword,
                            std::format("id={} username={}",
                                        target->id, target->username),
                            true));
    return UsersStatus::Ok;
}

UsersStatus UsersPresenter::changeOwnPassword(
        std::string_view oldPassword,
        std::string_view newPassword) {
    const auto me = session_.currentUser();
    if (!me.has_value()) {
        // No session means no actor to attribute the action to; refuse
        // outright rather than logging a bogus "unknown" entry.
        return UsersStatus::Forbidden;
    }
    if (!isValidPassword(newPassword)) {
        audit_.record(makeEvent(session_, kActionChangePassword,
                                std::format("invalid input id={}", me->id),
                                false));
        return UsersStatus::ValidationFailed;
    }

    // Re-fetch from storage so the verify step uses the persisted hash,
    // not a stale copy that may have been reset by an admin since the
    // session was minted.
    auto stored = users_.findById(me->id);
    if (!stored.has_value()) {
        audit_.record(makeEvent(session_, kActionChangePassword,
                                std::format("self vanished id={}", me->id),
                                false));
        return UsersStatus::NotFound;
    }

    bool oldOk = false;
    try {
        oldOk = hasher_.verify(oldPassword, stored->passwordHash);
    } catch (const std::exception&) {
        audit_.record(makeEvent(session_, kActionChangePassword,
                                std::format("hasher failure id={}", me->id),
                                false));
        return UsersStatus::HasherFailure;
    }
    if (!oldOk) {
        audit_.record(makeEvent(session_, kActionChangePassword,
                                std::format("wrong old password id={}",
                                            me->id),
                                false));
        return UsersStatus::WrongPassword;
    }

    try {
        stored->passwordHash = hasher_.hash(newPassword);
    } catch (const std::exception&) {
        audit_.record(makeEvent(session_, kActionChangePassword,
                                std::format("hasher failure id={}", me->id),
                                false));
        return UsersStatus::HasherFailure;
    }
    if (!users_.update(*stored)) {
        audit_.record(makeEvent(session_, kActionChangePassword,
                                std::format("storage failure id={}", me->id),
                                false));
        return UsersStatus::StorageFailure;
    }

    audit_.record(makeEvent(session_, kActionChangePassword,
                            std::format("id={} username={}",
                                        me->id, me->username),
                            true));
    return UsersStatus::Ok;
}

UsersStatus UsersPresenter::setOwnAvatar(
        const std::vector<std::uint8_t>& bytes,
        std::string_view                 mime) {
    const auto me = session_.currentUser();
    if (!me.has_value()) return UsersStatus::Forbidden;

    if (!users_.setAvatar(me->id, bytes, mime)) {
        // Repository returned false -- either oversized, bad mime, or
        // the row vanished. We can't tell from a bool, so report
        // ValidationFailed (the UI typically validates first and this
        // is the fallback).
        audit_.record(makeEvent(session_, kActionChangeAvatar,
                                std::format("rejected id={} mime={} bytes={}",
                                            me->id, std::string{mime},
                                            bytes.size()),
                                false));
        return UsersStatus::ValidationFailed;
    }
    audit_.record(makeEvent(session_, kActionChangeAvatar,
                            std::format("id={} mime={} bytes={}",
                                        me->id, std::string{mime},
                                        bytes.size()),
                            true));
    return UsersStatus::Ok;
}

UsersStatus UsersPresenter::clearOwnAvatar() {
    const auto me = session_.currentUser();
    if (!me.has_value()) return UsersStatus::Forbidden;

    if (!users_.clearAvatar(me->id)) {
        audit_.record(makeEvent(session_, kActionClearAvatar,
                                std::format("storage failure id={}", me->id),
                                false));
        return UsersStatus::StorageFailure;
    }
    audit_.record(makeEvent(session_, kActionClearAvatar,
                            std::format("id={} username={}",
                                        me->id, me->username),
                            true));
    return UsersStatus::Ok;
}

std::string_view statusMessage(UsersStatus s) {
    using enum UsersStatus;
    switch (s) {
        case Ok:                  return "OK";
        case Forbidden:           return "Permission denied";
        case DuplicateUsername:   return "Username already exists";
        case NotFound:            return "User not found";
        case ValidationFailed:    return "Invalid input";
        case SelfMutationRefused: return "Cannot perform this on your own account";
        case WrongPassword:       return "Current password is incorrect";
        case HasherFailure:       return "Password hashing failed";
        case StorageFailure:      return "Storage layer refused the change";
    }
    return "Unknown status";
}

}  // namespace app::presenter
