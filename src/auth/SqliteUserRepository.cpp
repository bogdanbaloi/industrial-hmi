#include "src/auth/SqliteUserRepository.h"

#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace app::auth {

namespace {

constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,"
    "  password_hash TEXT    NOT NULL,"
    "  role          INTEGER NOT NULL,"
    "  enabled       INTEGER NOT NULL DEFAULT 1,"
    "  created_at    TEXT    NOT NULL"
    ");";

constexpr const char* kSelectByUsernameSql =
    "SELECT id, username, password_hash, role, enabled, created_at "
    "FROM users WHERE username = ? COLLATE NOCASE LIMIT 1";

constexpr const char* kSelectAllSql =
    "SELECT id, username, password_hash, role, enabled, created_at "
    "FROM users ORDER BY id";

constexpr const char* kInsertSql =
    "INSERT INTO users(username, password_hash, role, enabled, created_at) "
    "VALUES (?, ?, ?, ?, ?)";

constexpr const char* kUpdateSql =
    "UPDATE users SET password_hash = ?, role = ?, enabled = ? "
    "WHERE id = ?";

constexpr const char* kCountSql = "SELECT COUNT(*) FROM users";

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

/// Pull a row out of a positioned sqlite3_stmt. Caller owns step()
/// advancement; we only read columns 0..5 of the canonical SELECT
/// shape used by both findByUsername and listAll.
User extractUser(sqlite3_stmt* stmt) {
    User u;
    u.id            = sqlite3_column_int64(stmt, 0);
    u.username      = reinterpret_cast<const char*>(
                          sqlite3_column_text(stmt, 1));
    u.passwordHash  = reinterpret_cast<const char*>(
                          sqlite3_column_text(stmt, 2));
    u.role          = static_cast<Role>(sqlite3_column_int(stmt, 3));
    u.enabled       = sqlite3_column_int(stmt, 4) != 0;
    const char* ts  = reinterpret_cast<const char*>(
                          sqlite3_column_text(stmt, 5));
    if (ts != nullptr) u.createdAt = ts;
    return u;
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

    if (logger_ != nullptr) {
        logger_->info("Auth: user store initialised at {}", config_.dbPath);
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
    sqlite3_bind_text(stmt,  1, user.passwordHash.c_str(),
                      static_cast<int>(user.passwordHash.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,   2, static_cast<int>(user.role));
    sqlite3_bind_int(stmt,   3, user.enabled ? 1 : 0);
    sqlite3_bind_int64(stmt, 4, user.id);

    const int step = sqlite3_step(stmt);
    const int changed = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return step == SQLITE_DONE && changed > 0;
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
