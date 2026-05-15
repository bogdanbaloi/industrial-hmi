#include "src/auth/AuthService.h"

#include "src/auth/AuditEvent.h"
#include "src/auth/AuditLogger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <string>

namespace app::auth {

namespace {

/// Lowercased username. Matches the schema's NOCASE collation but
/// keeps the canonicalisation explicit at the service layer so a
/// future non-SQLite repository (LDAP, Postgres) doesn't accidentally
/// drift on case sensitivity.
std::string canonicalUsername(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    std::transform(raw.begin(), raw.end(), std::back_inserter(out),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return out;
}

/// Convenience: build an AUTH audit row. The username always lands in
/// the event even on failed attempts so a forensic walk can correlate
/// a failed login burst to a single account.
AuditEvent makeAuthEvent(std::string_view username,
                         Role             role,
                         std::string_view action,
                         std::string_view details,
                         std::string_view outcome) {
    AuditEvent e;
    e.username = username;
    e.role     = roleName(role);
    e.category = category::kAuth;
    e.action   = action;
    e.details  = details;
    e.result   = outcome;
    return e;
}

}  // namespace

AuthService::AuthService(UserRepository& users,
                         PasswordHasher& hasher,
                         Session& session)
    : users_(users), hasher_(hasher), session_(session) {}

LoginResult AuthService::login(std::string_view username,
                               std::string_view password) {
    const std::string canon = canonicalUsername(username);

    const auto userOpt = users_.findByUsername(canon);
    if (!userOpt.has_value()) {
        if (logger_ != nullptr) {
            logger_->info("Auth: login miss (unknown user '{}')", canon);
        }
        if (audit_ != nullptr) {
            audit_->record(makeAuthEvent(canon, Role::Operator, "LOGIN",
                                         "unknown user", result::kFailure));
        }
        // Important: same failure mode as bad password so timing /
        // error-message attackers cannot enumerate usernames.
        return LoginResult::InvalidCredentials;
    }

    const User& user = *userOpt;

    if (!user.enabled) {
        if (logger_ != nullptr) {
            logger_->warn("Auth: rejected disabled account '{}'", canon);
        }
        if (audit_ != nullptr) {
            audit_->record(makeAuthEvent(canon, user.role, "LOGIN",
                                         "account disabled",
                                         result::kFailure));
        }
        return LoginResult::AccountDisabled;
    }

    bool verified = false;
    try {
        verified = hasher_.verify(password, user.passwordHash);
    } catch (const std::exception& ex) {
        if (logger_ != nullptr) {
            logger_->error("Auth: hasher exception for '{}': {}",
                           canon, ex.what());
        }
        if (audit_ != nullptr) {
            audit_->record(makeAuthEvent(canon, user.role, "LOGIN",
                                         "hasher exception",
                                         result::kFailure));
        }
        return LoginResult::HasherFailure;
    }

    if (!verified) {
        if (logger_ != nullptr) {
            logger_->info("Auth: bad password for '{}'", canon);
        }
        if (audit_ != nullptr) {
            audit_->record(makeAuthEvent(canon, user.role, "LOGIN",
                                         "bad password",
                                         result::kFailure));
        }
        return LoginResult::InvalidCredentials;
    }

    session_.setUser(user);
    if (logger_ != nullptr) {
        logger_->info("Auth: login OK '{}' as {}",
                      canon, roleName(user.role));
    }
    if (audit_ != nullptr) {
        audit_->record(makeAuthEvent(canon, user.role, "LOGIN",
                                     "", result::kSuccess));
    }
    return LoginResult::Success;
}

void AuthService::logout() {
    if (logger_ != nullptr) {
        logger_->info("Auth: logout '{}'", session_.currentUsername());
    }
    if (audit_ != nullptr) {
        // Capture role + username BEFORE clearing the session;
        // otherwise the audit row says "unknown" and we lose the
        // attribution.
        const auto userOpt = session_.currentUser();
        const auto user    = userOpt.value_or(User{});
        audit_->record(makeAuthEvent(user.username, user.role, "LOGOUT",
                                     "", result::kSuccess));
    }
    session_.clear();
}

std::size_t AuthService::seedDefaultUsersIfEmpty() {
    if (users_.count() > 0) return 0;

    // The seed set mirrors the three-role model:
    //   operator / operator       -> Role::Operator
    //   maintenance / maintenance -> Role::Maintenance
    //   admin / admin             -> Role::Admin
    //
    // Hard-coded weak passwords on purpose -- a fresh install hands
    // the keys to whoever boots first, who is expected to rotate them
    // via the user-management page (added in a follow-up). For
    // production deployment the seeder should be skipped entirely
    // and the operator provisions via an out-of-band channel.
    struct SeedEntry {
        const char* username;
        const char* password;
        Role        role;
    };
    constexpr std::array<SeedEntry, 3> kSeed{{
        {"operator",    "operator",    Role::Operator},
        {"maintenance", "maintenance", Role::Maintenance},
        {"admin",       "admin",       Role::Admin},
    }};

    std::size_t created = 0;
    for (const auto& s : kSeed) {
        User u;
        u.username     = s.username;
        u.role         = s.role;
        u.enabled      = true;
        try {
            u.passwordHash = hasher_.hash(s.password);
        } catch (const std::exception& ex) {
            if (logger_ != nullptr) {
                logger_->error("Auth: seed hash failed for '{}': {}",
                               s.username, ex.what());
            }
            continue;
        }
        if (users_.create(u).has_value()) {
            ++created;
        }
    }

    if (logger_ != nullptr && created > 0) {
        logger_->warn("Auth: seeded {} default users -- ROTATE PASSWORDS",
                      created);
    }
    return created;
}

}  // namespace app::auth
