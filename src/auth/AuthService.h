#pragma once

#include "src/auth/PasswordHasher.h"
#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/auth/UserRepository.h"
#include "src/core/LoggerBase.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace app::auth {

class AuditLogger;   // forward decl

/// Outcome of a login attempt. Single enum so the UI can switch on it
/// for distinct error messages, and the audit log can record the
/// reason without a free-form string.
enum class LoginResult : std::uint8_t {
    /// Credentials matched and the account is enabled. Session is set.
    Success,

    /// Username not found OR password mismatch -- merged so the UI
    /// cannot reveal which one. User-enumeration mitigation.
    InvalidCredentials,

    /// Account exists, credentials match, but `enabled = false`.
    /// Distinct from InvalidCredentials so the admin who just
    /// disabled the user can see a useful message.
    AccountDisabled,

    /// Rate-limit / lockout (future). Reserved.
    LockedOut,

    /// Hash verification threw -- treat as InvalidCredentials but log
    /// at error level so a corrupted users table doesn't silently
    /// reject everyone.
    HasherFailure,
};

/// Login + session coordinator. Pure C++ -- no GTK.
///
/// Compositional shape (Dependency Inversion at every dependency):
///   UserRepository&    -- where to load + persist users
///   PasswordHasher&    -- how to verify + freshly hash
///   Session&           -- where to record the active principal
///   Logger* (optional) -- diagnostic stream, project-wide pattern
///
/// All injections are references; the composition root keeps the
/// owning objects on the stack and outlives the service. A future
/// JWT / OAuth backend just provides a different UserRepository (one
/// that talks to an IdP) without touching this class.
class AuthService {
public:
    AuthService(UserRepository& users,
                PasswordHasher& hasher,
                Session& session);

    AuthService(const AuthService&)            = delete;
    AuthService& operator=(const AuthService&) = delete;
    AuthService(AuthService&&)                 = delete;
    AuthService& operator=(AuthService&&)      = delete;
    ~AuthService()                             = default;

    void setLogger(app::core::Logger& logger) { logger_ = &logger; }

    /// Inject an audit sink. When set, every login attempt (success
    /// or failure) and every logout records an AUTH event. Null
    /// keeps the service silent on the audit path, matching the
    /// project-wide "auth core works standalone" contract.
    void setAuditLogger(AuditLogger& audit) { audit_ = &audit; }

    /// Attempt a login. On `Success` the Session has the user set; on
    /// any failure the Session is left as-was (logout doesn't happen
    /// implicitly, the caller decides).
    [[nodiscard]] LoginResult
    login(std::string_view username, std::string_view password);

    /// Drop the active session. Called from the menu "Sign out"
    /// action; the LoginDialog re-runs on next startup or via a
    /// dedicated button.
    void logout();

    /// One-time seeding helper. If the user table is empty, inserts a
    /// canonical set of three demo accounts (operator/operator,
    /// maintenance/maintenance, admin/admin) so a fresh database is
    /// immediately usable. Returns the number of accounts created.
    /// Idempotent: a second call with rows already present is a no-op.
    std::size_t seedDefaultUsersIfEmpty();

private:
    UserRepository&      users_;
    PasswordHasher&      hasher_;
    Session&             session_;
    app::core::Logger*   logger_{nullptr};
    AuditLogger*         audit_{nullptr};
};

}  // namespace app::auth
