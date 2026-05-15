#pragma once

#include "src/auth/UserRepository.h"
#include "src/core/LoggerBase.h"

#include <mutex>
#include <string>

struct sqlite3;

namespace app::auth {

/// SQLite-backed UserRepository. Single connection guarded by a mutex.
/// Lives in its own file (default `data/auth.sqlite`) rather than in
/// the existing products DB so the two lifecycles stay independent --
/// products are bulk-imported, users are managed separately.
///
/// Schema:
/// @code
///   CREATE TABLE users (
///       id            INTEGER PRIMARY KEY AUTOINCREMENT,
///       username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,
///       password_hash TEXT    NOT NULL,
///       role          INTEGER NOT NULL,    -- Role enum value
///       enabled       INTEGER NOT NULL DEFAULT 1,
///       created_at    TEXT    NOT NULL
///   );
/// @endcode
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
    /// cannot run without auth once the feature is wired).
    [[nodiscard]] bool initialize();

    // UserRepository
    [[nodiscard]] std::optional<User>
    findByUsername(std::string_view username) override;
    [[nodiscard]] std::vector<User> listAll() override;
    [[nodiscard]] std::optional<User> create(const User& user) override;
    bool update(const User& user) override;
    [[nodiscard]] std::size_t count() const override;

private:
    Config              config_;
    sqlite3*            db_{nullptr};
    mutable std::mutex  mutex_;
    app::core::Logger*  logger_{nullptr};
};

}  // namespace app::auth
