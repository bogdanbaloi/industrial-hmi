#include "src/auth/SqliteUserRepository.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace app::auth {

namespace {

// Bumped whenever the schema gains a column. Match the value to the
// highest case in applyMigrations() so a fresh DB and a migrated DB
// converge on the same row layout.
constexpr int kCurrentSchemaVersion = 2;

constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
    "  password_hash TEXT    NOT NULL,"
    "  role          INTEGER NOT NULL,"
    "  enabled       INTEGER NOT NULL DEFAULT 1,"
    "  created_at    TEXT    NOT NULL"
    ");";

// SELECT shapes deliberately omit avatar_blob -- the blob is fetched on
// demand via getAvatar(). Keeping it out of the hot path means
// findByUsername() (login) and listAll() (UsersPage) stay light even
// when many users have 256KB photos.
constexpr const char* kSelectByUsernameSql =
    "SELECT id, username, password_hash, role, enabled, created_at, "
    "       display_name, avatar_mime, updated_at "
    "FROM users WHERE username = ? COLLATE NOCASE LIMIT 1";

constexpr const char* kSelectByIdSql =
    "SELECT id, username, password_hash, role, enabled, created_at, "
    "       display_name, avatar_mime, updated_at "
    "FROM users WHERE id = ? LIMIT 1";

constexpr const char* kSelectAllSql =
    "SELECT id, username, password_hash, role, enabled, created_at, "
    "       display_name, avatar_mime, updated_at "
    "FROM users ORDER BY id";

constexpr const char* kInsertSql =
    "INSERT INTO users(username, password_hash, role, enabled, "
    "                  created_at, display_name, updated_at) "
    "VALUES (?, ?, ?, ?, ?, ?, ?)";

constexpr const char* kUpdateSql =
    "UPDATE users "
    "SET password_hash = ?, role = ?, enabled = ?, "
    "    display_name = ?, updated_at = ? "
    "WHERE id = ?";

constexpr const char* kDeleteSql =
    "DELETE FROM users WHERE id = ?";

constexpr const char* kSetAvatarSql =
    "UPDATE users SET avatar_blob = ?, avatar_mime = ?, updated_at = ? "
    "WHERE id = ?";

constexpr const char* kClearAvatarSql =
    "UPDATE users SET avatar_blob = NULL, avatar_mime = NULL, "
    "                 updated_at = ? "
    "WHERE id = ?";

constexpr const char* kGetAvatarSql =
    "SELECT avatar_blob, avatar_mime FROM users WHERE id = ? LIMIT 1";

constexpr const char* kCountSql = "SELECT COUNT(*) FROM users";

// Whitelist of acceptable MIME strings for setAvatar. Keeps the
// renderer simple (it only needs to know two decoders) and prevents an
// attacker-controlled string from reaching the UI label.
constexpr std::string_view kMimePng  = "image/png";
constexpr std::string_view kMimeJpeg = "image/jpeg";

bool isAllowedAvatarMime(std::string_view mime) {
    return mime == kMimePng || mime == kMimeJpeg;
}

/// ISO 8601 UTC stamp. Matches the historian + audit log conventions
/// so a cross-table audit walk reads consistently.
std::string nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/// Read an optional TEXT column as std::string -- sqlite3_column_text
/// returns NULL for SQL NULL, which we must NOT pass to std::string's
/// ctor (UB). Returns empty string for SQL NULL, matching the User
/// struct's "empty == absent" convention.
std::string columnTextOrEmpty(sqlite3_stmt* stmt, int col) {
    const auto* p = reinterpret_cast<const char*>(
                        sqlite3_column_text(stmt, col));
    return p != nullptr ? std::string{p} : std::string{};
}

/// Pull a row out of a positioned sqlite3_stmt. Caller owns step()
/// advancement; we only read the canonical SELECT shape used by
/// findByUsername / findById / listAll (no avatar_blob column).
User extractUser(sqlite3_stmt* stmt) {
    User u;
    u.id            = sqlite3_column_int64(stmt, 0);
    u.username      = columnTextOrEmpty(stmt, 1);
    u.passwordHash  = columnTextOrEmpty(stmt, 2);
    u.role          = static_cast<Role>(sqlite3_column_int(stmt, 3));
    u.enabled       = sqlite3_column_int(stmt, 4) != 0;
    u.createdAt     = columnTextOrEmpty(stmt, 5);
    u.displayName   = columnTextOrEmpty(stmt, 6);
    u.avatarMime    = columnTextOrEmpty(stmt, 7);
    u.updatedAt     = columnTextOrEmpty(stmt, 8);
    return u;
}

/// Read the current PRAGMA user_version. Returns 0 on a fresh DB --
/// the sqlite default. Failure to query (corrupt DB) also returns 0
/// so applyMigrations() then re-runs the full ladder, which is the
/// safer fallback than guessing the user is up to date.
int readSchemaVersion(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr)
            != SQLITE_OK) {
        return 0;
    }
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

bool execSimple(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    sqlite3_free(err);
    return rc == SQLITE_OK;
}

}  // namespace

SqliteUserRepository::SqliteUserRepository(Config config)
    : config_(std::move(config)) {}

SqliteUserRepository::~SqliteUserRepository() {
    const std::scoped_lock lock(mutex_);
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool SqliteUserRepository::initialize() {
    const std::scoped_lock lock(mutex_);

    const int rc = sqlite3_open(config_.dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Auth: sqlite3_open({}) failed: {}",
                           config_.dbPath,
                           db_ != nullptr ? sqlite3_errmsg(db_)
                                          : "(no handle)");
        }
        if (db_ != nullptr) {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        return false;
    }

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, kCreateTableSql, nullptr, nullptr, &errMsg)
            != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Auth: schema create failed: {}",
                           errMsg != nullptr ? errMsg : "(no msg)");
        }
        sqlite3_free(errMsg);
        return false;
    }

    if (!applyMigrations()) {
        if (logger_ != nullptr) {
            logger_->error("Auth: schema migration failed");
        }
        return false;
    }

    if (logger_ != nullptr) {
        logger_->info("Auth: user store initialised at {} (schema v{})",
                      config_.dbPath, kCurrentSchemaVersion);
    }
    return true;
}

bool SqliteUserRepository::applyMigrations() {
    int version = readSchemaVersion(db_);
    if (version >= kCurrentSchemaVersion) return true;

    // Migration ladder. NEVER edit a past case once shipped -- a
    // deployed DB may already have applied it. Add a new case for the
    // next bump.
    while (version < kCurrentSchemaVersion) {
        switch (version) {
            case 0: {
                // v0 (pre-PRAGMA) -> v1: the base CREATE TABLE above
                // already produced v1's columns; nothing structural to
                // add, just stamp the version so future bumps know
                // where we are.
                if (!execSimple(db_, "PRAGMA user_version = 1")) {
                    return false;
                }
                break;
            }
            case 1: {
                // v1 -> v2: user management columns. ALTER TABLE ADD
                // COLUMN is online + cheap in SQLite (no table rewrite).
                // Each column is independently nullable so existing
                // rows remain valid without backfill.
                const char* steps[] = {
                    "ALTER TABLE users ADD COLUMN display_name TEXT",
                    "ALTER TABLE users ADD COLUMN avatar_mime  TEXT",
                    "ALTER TABLE users ADD COLUMN avatar_blob  BLOB",
                    "ALTER TABLE users ADD COLUMN updated_at   TEXT",
                    "PRAGMA user_version = 2",
                };
                for (const char* sql : steps) {
                    if (!execSimple(db_, sql)) return false;
                }
                break;
            }
            default:
                // Unknown future version on disk (downgrade scenario).
                // Bail rather than mutate -- the operator should run
                // the matching binary.
                if (logger_ != nullptr) {
                    logger_->error(
                        "Auth: DB reports schema v{}, this binary knows "
                        "up to v{}; refusing to downgrade",
                        version, kCurrentSchemaVersion);
                }
                return false;
        }
        version = readSchemaVersion(db_);
    }
    return true;
}

std::optional<User>
SqliteUserRepository::findByUsername(std::string_view username) {
    if (db_ == nullptr) return std::nullopt;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSelectByUsernameSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, username.data(),
                      static_cast<int>(username.size()), SQLITE_TRANSIENT);

    std::optional<User> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = extractUser(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<User> SqliteUserRepository::findById(std::int64_t id) {
    if (db_ == nullptr) return std::nullopt;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSelectByIdSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, id);

    std::optional<User> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = extractUser(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<User> SqliteUserRepository::listAll() {
    std::vector<User> out;
    if (db_ == nullptr) return out;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSelectAllSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(extractUser(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<User> SqliteUserRepository::create(const User& user) {
    if (db_ == nullptr) return std::nullopt;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Auth: prepare INSERT failed: {}",
                           sqlite3_errmsg(db_));
        }
        return std::nullopt;
    }
    const std::string createdAt =
        user.createdAt.empty() ? nowIso8601() : user.createdAt;
    const std::string updatedAt =
        user.updatedAt.empty() ? createdAt : user.updatedAt;

    sqlite3_bind_text(stmt,  1, user.username.c_str(),
                      static_cast<int>(user.username.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  2, user.passwordHash.c_str(),
                      static_cast<int>(user.passwordHash.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,   3, static_cast<int>(user.role));
    sqlite3_bind_int(stmt,   4, user.enabled ? 1 : 0);
    sqlite3_bind_text(stmt,  5, createdAt.c_str(),
                      static_cast<int>(createdAt.size()),
                      SQLITE_TRANSIENT);
    if (user.displayName.empty()) {
        sqlite3_bind_null(stmt, 6);
    } else {
        sqlite3_bind_text(stmt, 6, user.displayName.c_str(),
                          static_cast<int>(user.displayName.size()),
                          SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt,  7, updatedAt.c_str(),
                      static_cast<int>(updatedAt.size()),
                      SQLITE_TRANSIENT);

    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) {
        // SQLITE_CONSTRAINT on duplicate username is expected (UI
        // surfaces it as "username already exists") -- not a logger
        // error, just a nullopt result.
        return std::nullopt;
    }

    User out         = user;
    out.id           = sqlite3_last_insert_rowid(db_);
    out.createdAt    = createdAt;
    out.updatedAt    = updatedAt;
    return out;
}

bool SqliteUserRepository::update(const User& user) {
    if (db_ == nullptr) return false;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kUpdateSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return false;
    }
    const std::string updatedAt = nowIso8601();

    sqlite3_bind_text(stmt,  1, user.passwordHash.c_str(),
                      static_cast<int>(user.passwordHash.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,   2, static_cast<int>(user.role));
    sqlite3_bind_int(stmt,   3, user.enabled ? 1 : 0);
    if (user.displayName.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        sqlite3_bind_text(stmt, 4, user.displayName.c_str(),
                          static_cast<int>(user.displayName.size()),
                          SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt,  5, updatedAt.c_str(),
                      static_cast<int>(updatedAt.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, user.id);

    const int step = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return step == SQLITE_DONE && changed > 0;
}

bool SqliteUserRepository::remove(std::int64_t id) {
    if (db_ == nullptr) return false;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kDeleteSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, id);
    const int step = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return step == SQLITE_DONE && changed > 0;
}

bool SqliteUserRepository::setAvatar(
        std::int64_t id,
        const std::vector<std::uint8_t>& bytes,
        std::string_view mime) {
    // Boundary validation: zero-byte payloads and unknown MIMEs never
    // reach SQLite. Caller (presenter) surfaces the failure to the UI.
    if (bytes.empty() || bytes.size() > kMaxAvatarBytes) return false;
    if (!isAllowedAvatarMime(mime)) return false;

    if (db_ == nullptr) return false;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSetAvatarSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return false;
    }
    const std::string updatedAt = nowIso8601();

    sqlite3_bind_blob(stmt, 1, bytes.data(),
                      static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, mime.data(),
                      static_cast<int>(mime.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, updatedAt.c_str(),
                      static_cast<int>(updatedAt.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, id);

    const int step = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return step == SQLITE_DONE && changed > 0;
}

bool SqliteUserRepository::clearAvatar(std::int64_t id) {
    if (db_ == nullptr) return false;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kClearAvatarSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return false;
    }
    const std::string updatedAt = nowIso8601();
    sqlite3_bind_text(stmt, 1, updatedAt.c_str(),
                      static_cast<int>(updatedAt.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);

    const int step = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return step == SQLITE_DONE && changed > 0;
}

std::optional<Avatar>
SqliteUserRepository::getAvatar(std::int64_t id) {
    if (db_ == nullptr) return std::nullopt;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kGetAvatarSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, id);

    std::optional<Avatar> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // SQL NULL in either column means "no avatar uploaded".
        const auto blobType = sqlite3_column_type(stmt, 0);
        const auto mimeType = sqlite3_column_type(stmt, 1);
        if (blobType != SQLITE_NULL && mimeType != SQLITE_NULL) {
            const auto size = sqlite3_column_bytes(stmt, 0);
            const auto* data = static_cast<const std::uint8_t*>(
                sqlite3_column_blob(stmt, 0));
            const auto* mime = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 1));
            Avatar a;
            a.bytes.assign(data, data + size);
            if (mime != nullptr) a.mime = mime;
            result = std::move(a);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::size_t SqliteUserRepository::count() const {
    if (db_ == nullptr) return 0;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kCountSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    std::size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return n;
}

}  // namespace app::auth
