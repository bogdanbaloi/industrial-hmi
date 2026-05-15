// Tests for SqliteUserRepository.
//
// All cases run against `:memory:` so the suite stays hermetic; the
// same code paths execute on a file-backed deployment. Covers schema
// bootstrap idempotency, the unique-username constraint (case-
// insensitive), CRUD round-trips, and the count() helper used by the
// seeder.

#include "src/auth/SqliteUserRepository.h"

#include <gtest/gtest.h>

#include <memory>

namespace {
using app::auth::Role;
using app::auth::SqliteUserRepository;
using app::auth::User;

std::unique_ptr<SqliteUserRepository> makeRepo() {
    auto repo = std::make_unique<SqliteUserRepository>(
        SqliteUserRepository::Config{.dbPath = ":memory:"});
    EXPECT_TRUE(repo->initialize());
    return repo;
}

User sampleUser(const char* name, Role r = Role::Operator) {
    User u;
    u.username     = name;
    u.passwordHash = "$argon2id$dummy";
    u.role         = r;
    u.enabled      = true;
    return u;
}

}  // namespace

TEST(SqliteUserRepositoryTest, EmptyRepoCountsZero) {
    auto repo = makeRepo();
    EXPECT_EQ(repo->count(), 0U);
    EXPECT_TRUE(repo->listAll().empty());
}

TEST(SqliteUserRepositoryTest, CreateReturnsPopulatedRow) {
    auto repo = makeRepo();
    const auto out = repo->create(sampleUser("alice"));
    ASSERT_TRUE(out.has_value());
    EXPECT_GT(out->id, 0);             // auto-increment populated
    EXPECT_FALSE(out->createdAt.empty());  // timestamp set
    EXPECT_EQ(out->username, "alice");
    EXPECT_EQ(out->role, Role::Operator);
}

TEST(SqliteUserRepositoryTest, FindByUsernameSuccess) {
    auto repo = makeRepo();
    ASSERT_TRUE(repo->create(sampleUser("alice", Role::Maintenance))
                    .has_value());

    const auto found = repo->findByUsername("alice");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->username, "alice");
    EXPECT_EQ(found->role, Role::Maintenance);
}

TEST(SqliteUserRepositoryTest, FindByUsernameCaseInsensitive) {
    // Schema COLLATE NOCASE guarantees lookups match across case --
    // a request canonicalised as "alice" still finds a row inserted
    // as "Alice". Defence-in-depth: the service layer normalises too.
    auto repo = makeRepo();
    ASSERT_TRUE(repo->create(sampleUser("Alice")).has_value());
    EXPECT_TRUE(repo->findByUsername("alice").has_value());
    EXPECT_TRUE(repo->findByUsername("ALICE").has_value());
}

TEST(SqliteUserRepositoryTest, FindByUsernameMissingIsNullopt) {
    auto repo = makeRepo();
    EXPECT_FALSE(repo->findByUsername("nobody").has_value());
}

TEST(SqliteUserRepositoryTest, DuplicateUsernameRejected) {
    auto repo = makeRepo();
    EXPECT_TRUE(repo->create(sampleUser("alice")).has_value());
    // Same name, different case -- still hits the UNIQUE constraint.
    EXPECT_FALSE(repo->create(sampleUser("ALICE")).has_value());
    EXPECT_EQ(repo->count(), 1U);
}

TEST(SqliteUserRepositoryTest, ListAllReturnsAllRowsOrdered) {
    auto repo = makeRepo();
    ASSERT_TRUE(repo->create(sampleUser("alice")).has_value());
    ASSERT_TRUE(repo->create(sampleUser("bob", Role::Maintenance))
                    .has_value());
    ASSERT_TRUE(repo->create(sampleUser("carol", Role::Admin))
                    .has_value());

    const auto rows = repo->listAll();
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].username, "alice");
    EXPECT_EQ(rows[1].username, "bob");
    EXPECT_EQ(rows[2].username, "carol");
    EXPECT_EQ(rows[2].role, Role::Admin);
}

TEST(SqliteUserRepositoryTest, UpdateChangesPasswordAndRole) {
    auto repo = makeRepo();
    auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    created->passwordHash = "$argon2id$newer";
    created->role         = Role::Admin;
    EXPECT_TRUE(repo->update(*created));

    const auto refreshed = repo->findByUsername("alice");
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_EQ(refreshed->passwordHash, "$argon2id$newer");
    EXPECT_EQ(refreshed->role, Role::Admin);
}

TEST(SqliteUserRepositoryTest, UpdateMissingIdReturnsFalse) {
    auto repo = makeRepo();
    User phantom;
    phantom.id           = 9999;
    phantom.passwordHash = "anything";
    EXPECT_FALSE(repo->update(phantom));
}

TEST(SqliteUserRepositoryTest, DisabledFlagPreserved) {
    auto repo = makeRepo();
    User u = sampleUser("alice");
    u.enabled = false;
    const auto created = repo->create(u);
    ASSERT_TRUE(created.has_value());
    EXPECT_FALSE(repo->findByUsername("alice")->enabled);
}

TEST(SqliteUserRepositoryTest, InitializeIsIdempotent) {
    SqliteUserRepository repo(
        SqliteUserRepository::Config{.dbPath = ":memory:"});
    EXPECT_TRUE(repo.initialize());
    // Second initialise (e.g. an accidental retry in main) must not
    // throw or fail -- the CREATE TABLE IF NOT EXISTS guard handles it.
    EXPECT_TRUE(repo.initialize());
}
