#pragma once

#include "src/auth/UserRepository.h"
#include "src/core/LoggerBase.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace app::auth {

/// SQLite-backed UserRepository. Single connection guarded by a mutex.
/// Lives in its own file (default `data/auth.sqlite`) rather than in
/// the existing products DB so the two lifecycles stay independent --
/// products are bulk-imported, users are managed separately.
///
/// Schema (current = v2):
/// @code
///   CREATE TABLE users (
///       id            INTEGER PRIMARY KEY AUTOINCREMENT,
///       username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,
///       password_hash TEXT    NOT NULL,
///       role          INTEGER NOT NULL,
///       enabled       INTEGER NOT NULL DEFAULT 1,
///       created_at    TEXT    NOT NULL,
///       display_name  TEXT,            -- v2
///       avatar_mime   TEXT,            -- v2 ("image/png" | "image/jpeg")
///       avatar_blob   BLOB,            -- v2 (<= kMaxAvatarBytes)
///       updated_at    TEXT             -- v2
///   );
/// @endcode
///
/// Migration uses `PRAGMA user_version` so the upgrade is idempotent +
/// branch-agnostic (an older deployment booting a newer binary gets
/// the columns added in-place; a fresh DB jumps straight to v2). The
/// migration ladder lives in `applyMigrations()` so adding v3 later is
/// one new `case` rather than a rewrite.
///
/// `username COLLATE NOCASE` makes the UNIQUE constraint case-
/// insensitive at the storage layer, defence-in-depth on top of the
/// service layer normalising to lowercase. Without the COLLATE,
/// "Admin" and "admin" would both insert successfully and the next
/// login attempt would be ambiguous.
class SqliteUserRepository : public UserRepository {
public:
    struct Config {
        /// Filesystem path. `:memory:` for tests; relative paths
        /// resolve against the binary's working directory the same
        /// way the historian + products DB do.
        std::string dbPath{":memory:"};
    };

    explicit SqliteUserRepository(Config config);
    ~SqliteUserRepository() override;

    SqliteUserRepository(const SqliteUserRepository&)            = delete;
    SqliteUserRepository& operator=(const SqliteUserRepository&) = delete;
    SqliteUserRepository(SqliteUserRepository&&)                 = delete;
    SqliteUserRepository& operator=(SqliteUserRepository&&)      = delete;

    void setLogger(app::core::Logger& logger) { logger_ = &logger; }

    /// One-shot schema bootstrap. Idempotent. Returns false when
    /// SQLite refuses to open the file -- caller (composition root)
    /// surfaces the error as a fatal startup condition (the binary
    /// cannot run without auth once the feature is wired). Runs the
    /// migration ladder on every call so an older deployment picks
    /// up new columns automatically.
    [[nodiscard]] bool initialize();

    // UserRepository
    [[nodiscard]] std::optional<User>
    findByUsername(std::string_view username) override;
    [[nodiscard]] std::optional<User> findById(std::int64_t id) override;
    [[nodiscard]] std::vector<User>   listAll() override;
    [[nodiscard]] std::optional<User> create(const User& user) override;
    bool update(const User& user) override;
    bool remove(std::int64_t id) override;
    bool setAvatar(std::int64_t id,
                   const std::vector<std::uint8_t>& bytes,
                   std::string_view mime) override;
    bool clearAvatar(std::int64_t id) override;
    [[nodiscard]] std::optional<Avatar>
    getAvatar(std::int64_t id) override;
    [[nodiscard]] std::size_t count() const override;

private:
    /// Run the migration ladder. Reads PRAGMA user_version, walks each
    /// missing step in order, bumps user_version after each one. New
    /// schema revisions add a `case` to the switch -- never edit a
    /// previous case (a deployed DB may have already applied it).
    [[nodiscard]] bool applyMigrations();

    Config              config_;
    sqlite3*            db_{nullptr};
    mutable std::mutex  mutex_;
    app::core::Logger*  logger_{nullptr};
};

}  // namespace app::auth
