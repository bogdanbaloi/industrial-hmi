// [utest->req~auth-002~1]
// Covers REQ-AUTH-002 (three-tier RBAC),
//             REQ-AUTH-006 (session: generic bad-credentials feedback).
//
// Tests for AuthService.
//
// We exercise the service against a real SqliteUserRepository (in
// memory) and a real Argon2PasswordHasher -- it covers the actual
// integration without faking the cryptography. Each test creates its
// own fixture so cases are independent.

#include "src/auth/AuthService.h"
#include "src/auth/Argon2PasswordHasher.h"
#include "src/auth/SqliteUserRepository.h"

#include <gtest/gtest.h>

#include <memory>

namespace {
using app::auth::Argon2PasswordHasher;
using app::auth::AuthService;
using app::auth::LoginResult;
using app::auth::Role;
using app::auth::Session;
using app::auth::SqliteUserRepository;
using app::auth::User;

struct AuthFixture {
    std::unique_ptr<SqliteUserRepository> repo;
    Argon2PasswordHasher                  hasher;
    Session                               session;
    std::unique_ptr<AuthService>          service;

    AuthFixture() {
        repo = std::make_unique<SqliteUserRepository>(
            SqliteUserRepository::Config{.dbPath = ":memory:"});
        const bool ok = repo->initialize();
        EXPECT_TRUE(ok);
        service = std::make_unique<AuthService>(*repo, hasher, session);
    }

    /// Seed one user with a known password so the test can attempt
    /// both the success and failure paths.
    void seedUser(const std::string& username,
                  const std::string& password,
                  Role               role,
                  bool               enabled = true) {
        User u;
        u.username     = username;
        u.role         = role;
        u.enabled      = enabled;
        u.passwordHash = hasher.hash(password);
        ASSERT_TRUE(repo->create(u).has_value());
    }
};
}  // namespace

TEST(AuthServiceTest, SuccessfulLoginPopulatesSession) {
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);

    const auto result = fx.service->login("alice", "secret");
    EXPECT_EQ(result, LoginResult::Success);
    EXPECT_TRUE(fx.session.isAuthenticated());
    EXPECT_EQ(fx.session.currentUsername(), "alice");
}

TEST(AuthServiceTest, WrongPasswordRejected) {
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);

    EXPECT_EQ(fx.service->login("alice", "wrong"),
              LoginResult::InvalidCredentials);
    EXPECT_FALSE(fx.session.isAuthenticated());
}

TEST(AuthServiceTest, UnknownUserRejected) {
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);

    // Unknown user must return the SAME LoginResult as wrong password
    // -- user enumeration mitigation. The audit log line still
    // distinguishes them at the logger level for the admin's benefit.
    EXPECT_EQ(fx.service->login("bob", "anything"),
              LoginResult::InvalidCredentials);
    EXPECT_FALSE(fx.session.isAuthenticated());
}

TEST(AuthServiceTest, DisabledAccountRejected) {
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator, /*enabled=*/false);

    EXPECT_EQ(fx.service->login("alice", "secret"),
              LoginResult::AccountDisabled);
    EXPECT_FALSE(fx.session.isAuthenticated());
}

TEST(AuthServiceTest, LoginCanonicalisesUsername) {
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);

    EXPECT_EQ(fx.service->login("ALICE", "secret"), LoginResult::Success);
    EXPECT_EQ(fx.session.currentUsername(), "alice");
}

TEST(AuthServiceTest, LogoutClearsSession) {
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);
    ASSERT_EQ(fx.service->login("alice", "secret"), LoginResult::Success);
    ASSERT_TRUE(fx.session.isAuthenticated());

    fx.service->logout();
    EXPECT_FALSE(fx.session.isAuthenticated());
}

TEST(AuthServiceTest, SeedDefaultsOnlyOnce) {
    AuthFixture fx;
    EXPECT_EQ(fx.repo->count(), 0U);

    const auto first = fx.service->seedDefaultUsersIfEmpty();
    EXPECT_EQ(first, 3U);
    EXPECT_EQ(fx.repo->count(), 3U);

    // Second call is idempotent -- existing rows leave the seeder
    // a no-op so production restarts don't keep inserting.
    const auto second = fx.service->seedDefaultUsersIfEmpty();
    EXPECT_EQ(second, 0U);
    EXPECT_EQ(fx.repo->count(), 3U);
}

TEST(AuthServiceTest, SeededOperatorCanLogIn) {
    AuthFixture fx;
    fx.service->seedDefaultUsersIfEmpty();

    EXPECT_EQ(fx.service->login("operator", "operator"),
              LoginResult::Success);
    ASSERT_TRUE(fx.session.currentUser().has_value());
    EXPECT_EQ(fx.session.currentUser()->role, Role::Operator);
}

TEST(AuthServiceTest, SeededAdminCanLogIn) {
    AuthFixture fx;
    fx.service->seedDefaultUsersIfEmpty();

    EXPECT_EQ(fx.service->login("admin", "admin"), LoginResult::Success);
    ASSERT_TRUE(fx.session.currentUser().has_value());
    EXPECT_EQ(fx.session.currentUser()->role, Role::Admin);
}

TEST(AuthServiceTest, FailedLoginDoesNotOverwriteSession) {
    // If someone is already logged in and a subsequent attempt fails,
    // the existing session must stay intact. This matches what an
    // operator expects from a "switch user" UI flow that cancels.
    AuthFixture fx;
    fx.seedUser("alice", "secret", Role::Operator);
    fx.seedUser("bob",   "supersecret", Role::Admin);

    ASSERT_EQ(fx.service->login("alice", "secret"), LoginResult::Success);
    EXPECT_EQ(fx.session.currentUsername(), "alice");

    ASSERT_EQ(fx.service->login("bob", "wrong"),
              LoginResult::InvalidCredentials);
    EXPECT_TRUE(fx.session.isAuthenticated());
    EXPECT_EQ(fx.session.currentUsername(), "alice");
}
