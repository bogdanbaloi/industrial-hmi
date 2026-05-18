// Tests for SqliteUserRepository.
//
// All cases run against `:memory:` so the suite stays hermetic; the
// same code paths execute on a file-backed deployment. Covers schema
// bootstrap idempotency, the unique-username constraint (case-
// insensitive), CRUD round-trips, the count() helper used by the
// seeder, plus the v2 surface (display name, delete, avatar BLOB with
// size + MIME validation).

#include "src/auth/SqliteUserRepository.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace {
using app::auth::Avatar;
using app::auth::kMaxAvatarBytes;
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

/// Tiny PNG-ish payload. We don't decode it -- the repository just
/// stores BLOB bytes -- so any non-empty buffer with a recognised
/// MIME is fine for round-trip tests.
std::vector<std::uint8_t> fakePng(std::size_t bytes = 64) {
    std::vector<std::uint8_t> v(bytes);
    for (std::size_t i = 0; i < bytes; ++i) {
        v[i] = static_cast<std::uint8_t>(i & 0xFFU);
    }
    return v;
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
    EXPECT_GT(out->id, 0);                  // auto-increment populated
    EXPECT_FALSE(out->createdAt.empty());   // timestamp set
    EXPECT_FALSE(out->updatedAt.empty());   // v2: also stamped
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

TEST(SqliteUserRepositoryTest, FindByIdRoundTrip) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    const auto fetched = repo->findById(created->id);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->id, created->id);
    EXPECT_EQ(fetched->username, "alice");
}

TEST(SqliteUserRepositoryTest, FindByIdMissingIsNullopt) {
    auto repo = makeRepo();
    EXPECT_FALSE(repo->findById(9999).has_value());
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

TEST(SqliteUserRepositoryTest, UpdatePersistsDisplayName) {
    auto repo = makeRepo();
    auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    created->displayName = "Alice Bloomberg";
    EXPECT_TRUE(repo->update(*created));

    const auto refreshed = repo->findById(created->id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_EQ(refreshed->displayName, "Alice Bloomberg");
}

TEST(SqliteUserRepositoryTest, UpdateBumpsUpdatedAt) {
    // The repository must refresh updated_at on every successful
    // UPDATE so the management UI can sort "recently modified". The
    // test doesn't compare timestamps directly (clock granularity is
    // 1s, can collide) -- it just asserts the field becomes non-empty
    // and survives the round trip.
    auto repo = makeRepo();
    auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    created->role = Role::Admin;
    EXPECT_TRUE(repo->update(*created));

    const auto refreshed = repo->findById(created->id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_FALSE(refreshed->updatedAt.empty());
}

TEST(SqliteUserRepositoryTest, UpdateMissingIdReturnsFalse) {
    auto repo = makeRepo();
    User phantom;
    phantom.id           = 9999;
    phantom.passwordHash = "anything";
    EXPECT_FALSE(repo->update(phantom));
}

TEST(SqliteUserRepositoryTest, RemoveDropsRow) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    EXPECT_TRUE(repo->remove(created->id));
    EXPECT_EQ(repo->count(), 0U);
    EXPECT_FALSE(repo->findById(created->id).has_value());
}

TEST(SqliteUserRepositoryTest, RemoveMissingIdReturnsFalse) {
    auto repo = makeRepo();
    EXPECT_FALSE(repo->remove(9999));
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
    // throw or fail -- the CREATE TABLE IF NOT EXISTS guard plus the
    // migration ladder's version check make it a no-op.
    EXPECT_TRUE(repo.initialize());
}

// --- avatar BLOB --------------------------------------------------------

TEST(SqliteUserRepositoryTest, NoAvatarByDefault) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());
    EXPECT_TRUE(created->avatarMime.empty());
    EXPECT_FALSE(repo->getAvatar(created->id).has_value());
}

TEST(SqliteUserRepositoryTest, SetAvatarRoundTrip) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    const auto bytes = fakePng();
    EXPECT_TRUE(repo->setAvatar(created->id, bytes, "image/png"));

    // Avatar MIME shows on subsequent finds so the UI knows to fetch.
    const auto refreshed = repo->findById(created->id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_EQ(refreshed->avatarMime, "image/png");

    const auto avatar = repo->getAvatar(created->id);
    ASSERT_TRUE(avatar.has_value());
    EXPECT_EQ(avatar->mime, "image/png");
    EXPECT_EQ(avatar->bytes, bytes);
}

TEST(SqliteUserRepositoryTest, SetAvatarRejectsEmptyPayload) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    EXPECT_FALSE(repo->setAvatar(created->id, {}, "image/png"));
    EXPECT_FALSE(repo->getAvatar(created->id).has_value());
}

TEST(SqliteUserRepositoryTest, SetAvatarRejectsOversizedPayload) {
    // 256 KiB ceiling enforced at the repo boundary -- a malicious UI
    // path cannot push a multi-megabyte BLOB into SQLite.
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    std::vector<std::uint8_t> huge(kMaxAvatarBytes + 1U, 0xFFU);
    EXPECT_FALSE(repo->setAvatar(created->id, huge, "image/png"));
}

TEST(SqliteUserRepositoryTest, SetAvatarRejectsUnknownMime) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    EXPECT_FALSE(repo->setAvatar(created->id, fakePng(),
                                 "image/gif"));     // not on the list
    EXPECT_FALSE(repo->setAvatar(created->id, fakePng(),
                                 "text/html"));     // hostile
}

TEST(SqliteUserRepositoryTest, SetAvatarAcceptsJpegToo) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    EXPECT_TRUE(repo->setAvatar(created->id, fakePng(), "image/jpeg"));
    EXPECT_EQ(repo->getAvatar(created->id)->mime, "image/jpeg");
}

TEST(SqliteUserRepositoryTest, ClearAvatarReturnsToInitialsState) {
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());

    ASSERT_TRUE(repo->setAvatar(created->id, fakePng(), "image/png"));
    EXPECT_TRUE(repo->clearAvatar(created->id));

    const auto refreshed = repo->findById(created->id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_TRUE(refreshed->avatarMime.empty());
    EXPECT_FALSE(repo->getAvatar(created->id).has_value());
}

TEST(SqliteUserRepositoryTest, RemoveAlsoDropsAvatar) {
    // The avatar BLOB lives in the same row, so DELETE drops it
    // automatically -- no orphan blobs left behind after a user is
    // removed.
    auto repo = makeRepo();
    const auto created = repo->create(sampleUser("alice"));
    ASSERT_TRUE(created.has_value());
    ASSERT_TRUE(repo->setAvatar(created->id, fakePng(), "image/png"));

    EXPECT_TRUE(repo->remove(created->id));
    EXPECT_FALSE(repo->getAvatar(created->id).has_value());
}
