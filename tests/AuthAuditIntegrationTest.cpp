// [utest->req~auth-003~1]
// [utest->req~auth-006~1]
// Covers REQ-AUTH-003 (audit log for sensitive actions),
//        REQ-AUTH-006 (generic bad-credentials feedback; audit still
//        distinguishes the two failure modes for investigation).
//
// INTEGRATION test for the login -> audit path with every component
// real: AuthService + real SqliteUserRepository (in-memory) + real
// Argon2PasswordHasher + real SqliteAuditLogger (in-memory). No mocks.
//
// AuthServiceTest already covers the auth decision against a real repo
// + hasher, but it stops at the LoginResult -- it does NOT wire an
// audit sink, so the audit side of REQ-AUTH-003/006 was untested
// end-to-end. This fills that gap: it asserts the rows the service
// actually persists, and proves the security-relevant invariant that
// "unknown user" and "wrong password" look identical to the caller yet
// are distinguishable in the audit trail.

#include "src/auth/AuthService.h"
#include "src/auth/Argon2PasswordHasher.h"
#include "src/auth/SqliteUserRepository.h"
#include "src/auth/SqliteAuditLogger.h"
#include "src/auth/AuditLogger.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using app::auth::Argon2PasswordHasher;
using app::auth::AuditQuery;
using app::auth::AuthService;
using app::auth::LoginResult;
using app::auth::Role;
using app::auth::Session;
using app::auth::SqliteAuditLogger;
using app::auth::SqliteUserRepository;
using app::auth::User;

/// Everything real and in-memory. The audit logger is wired into the
/// service exactly as the composition root does it.
struct AuthAuditFixture {
    std::unique_ptr<SqliteUserRepository> repo;
    std::unique_ptr<SqliteAuditLogger>    audit;
    Argon2PasswordHasher                  hasher;
    Session                               session;
    std::unique_ptr<AuthService>          service;

    AuthAuditFixture() {
        repo = std::make_unique<SqliteUserRepository>(
            SqliteUserRepository::Config{.dbPath = ":memory:"});
        EXPECT_TRUE(repo->initialize());

        audit = std::make_unique<SqliteAuditLogger>(
            SqliteAuditLogger::Config{.dbPath = ":memory:"});
        EXPECT_TRUE(audit->initialize());

        service = std::make_unique<AuthService>(*repo, hasher, session);
        service->setAuditLogger(*audit);
    }

    void seedUser(const std::string& username,
                  const std::string& password,
                  Role               role) {
        User u;
        u.username     = username;
        u.role         = role;
        u.enabled      = true;
        u.passwordHash = hasher.hash(password);
        ASSERT_TRUE(repo->create(u).has_value());
    }
};

TEST(AuthAuditIntegrationTest, SuccessfulLoginWritesSuccessRow) {
    AuthAuditFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);

    ASSERT_EQ(fx.service->login("alice", "secret"), LoginResult::Success);

    AuditQuery q;
    q.action = "LOGIN";
    const auto rows = fx.audit->query(q);

    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].username, "alice");
    EXPECT_EQ(rows[0].category, "AUTH");
    EXPECT_EQ(rows[0].result, "SUCCESS");
}

TEST(AuthAuditIntegrationTest,
     UnknownUserAndWrongPasswordAreIndistinguishableToCallerButNotInAudit) {
    AuthAuditFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);

    // Both failures must return the SAME result to the caller -- no
    // username enumeration leak (REQ-AUTH-006).
    const auto unknown = fx.service->login("nobody", "secret");
    const auto badPass = fx.service->login("alice", "wrong");
    EXPECT_EQ(unknown, LoginResult::InvalidCredentials);
    EXPECT_EQ(badPass, LoginResult::InvalidCredentials);

    // ...but the audit trail distinguishes them for an investigator.
    AuditQuery q;
    q.action = "LOGIN";
    q.result = "FAILURE";
    const auto rows = fx.audit->query(q);

    ASSERT_EQ(rows.size(), 2U);
    // query returns rows; collect the details regardless of order.
    const bool hasUnknownUser =
        rows[0].details == "unknown user" || rows[1].details == "unknown user";
    const bool hasBadPassword =
        rows[0].details == "bad password" || rows[1].details == "bad password";
    EXPECT_TRUE(hasUnknownUser)
        << "audit must record the 'unknown user' failure mode";
    EXPECT_TRUE(hasBadPassword)
        << "audit must record the 'bad password' failure mode";
}

TEST(AuthAuditIntegrationTest, FailureOnlyQueryExcludesSuccessfulLogins) {
    AuthAuditFixture fx;
    fx.seedUser("alice", "secret", Role::Admin);

    ASSERT_EQ(fx.service->login("alice", "secret"), LoginResult::Success);
    ASSERT_EQ(fx.service->login("alice", "wrong"),
              LoginResult::InvalidCredentials);

    AuditQuery failuresOnly;
    failuresOnly.result = "FAILURE";
    EXPECT_EQ(fx.audit->query(failuresOnly).size(), 1U);

    // Total spans both the success and the failure row.
    EXPECT_EQ(fx.audit->totalEvents(), 2U);
}

}  // namespace
