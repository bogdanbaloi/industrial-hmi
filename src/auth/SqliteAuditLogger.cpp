#include "src/auth/SqliteAuditLogger.h"

#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace app::auth {

namespace {

constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS audit_log ("
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ts        TEXT NOT NULL,"
    "  username  TEXT NOT NULL,"
    "  role      TEXT NOT NULL,"
    "  category  TEXT NOT NULL,"
    "  action    TEXT NOT NULL,"
    "  details   TEXT NOT NULL,"
    "  result    TEXT NOT NULL"
    ");";

constexpr const char* kCreateIndexTsSql =
    "CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(ts);";

constexpr const char* kCreateIndexCatSql =
    "CREATE INDEX IF NOT EXISTS idx_audit_category "
    "ON audit_log(category, ts);";

constexpr const char* kInsertSql =
    "INSERT INTO audit_log(ts, username, role, category, action, "
    "                      details, result) "
    "VALUES (?, ?, ?, ?, ?, ?, ?)";

constexpr const char* kCountSql = "SELECT COUNT(*) FROM audit_log";

/// ISO 8601 UTC. Uniform with the rest of the codebase so cross-table
/// joins / reports line up.
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

void bindText(sqlite3_stmt* stmt, int idx, std::string_view s) {
    sqlite3_bind_text(stmt, idx, s.data(),
                      static_cast<int>(s.size()), SQLITE_TRANSIENT);
}

/// Pull a single row out of a positioned statement. Caller advances
/// step(); the column order matches the canonical SELECT below.
AuditEvent extractEvent(sqlite3_stmt* stmt) {
    AuditEvent e;
    e.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    e.username  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    e.role      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    e.category  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    e.action    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    e.details   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    e.result    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    return e;
}

/// Build the SELECT shape with optional WHERE clauses. SQLite cannot
/// parameterise the WHERE *structure* (only values), so we assemble
/// the SQL textually but still bind every operand. This is the same
/// pattern Modbus / Historian use for dynamic table names.
std::string buildSelectSql(const AuditQuery& q,
                           std::vector<std::string>& bindings) {
    std::string sql =
        "SELECT ts, username, role, category, action, details, result "
        "FROM audit_log WHERE 1=1";
    if (!q.fromTs.empty()) {
        sql += " AND ts >= ?";
        bindings.push_back(q.fromTs);
    }
    if (!q.toTs.empty()) {
        sql += " AND ts <= ?";
        bindings.push_back(q.toTs);
    }
    if (!q.category.empty()) {
        sql += " AND category = ?";
        bindings.push_back(q.category);
    }
    if (!q.action.empty()) {
        sql += " AND action = ?";
        bindings.push_back(q.action);
    }
    if (!q.result.empty()) {
        sql += " AND result = ?";
        bindings.push_back(q.result);
    }
    if (!q.username.empty()) {
        sql += " AND username = ?";
        bindings.push_back(q.username);
    }
    // Newest first: the audit page shows recent activity at the top.
    sql += " ORDER BY ts DESC LIMIT ?";
    return sql;
}

}  // namespace

SqliteAuditLogger::SqliteAuditLogger(Config config)
    : config_(std::move(config)) {}

SqliteAuditLogger::~SqliteAuditLogger() {
    const std::scoped_lock lock(mutex_);
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool SqliteAuditLogger::initialize() {
    const std::scoped_lock lock(mutex_);

    const int rc = sqlite3_open(config_.dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Audit: sqlite3_open({}) failed: {}",
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

    for (const char* sql : {kCreateTableSql, kCreateIndexTsSql,
                            kCreateIndexCatSql}) {
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            if (logger_ != nullptr) {
                logger_->error("Audit: schema exec failed: {}",
                               errMsg != nullptr ? errMsg : "(no msg)");
            }
            sqlite3_free(errMsg);
            return false;
        }
    }

    if (logger_ != nullptr) {
        logger_->info("Audit: log initialised at {}", config_.dbPath);
    }
    return true;
}

bool SqliteAuditLogger::record(const AuditEvent& event) {
    if (db_ == nullptr) return false;
    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &stmt, nullptr)
            != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Audit: prepare INSERT failed: {}",
                           sqlite3_errmsg(db_));
        }
        return false;
    }

    const std::string ts =
        event.timestamp.empty() ? nowIso8601() : event.timestamp;
    bindText(stmt, 1, ts);
    bindText(stmt, 2, event.username);
    bindText(stmt, 3, event.role);
    bindText(stmt, 4, event.category);
    bindText(stmt, 5, event.action);
    bindText(stmt, 6, event.details);
    bindText(stmt, 7, event.result);

    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step != SQLITE_DONE) {
        if (logger_ != nullptr) {
            logger_->warn("Audit: insert step failed: {}",
                          sqlite3_errmsg(db_));
        }
        return false;
    }
    return true;
}

std::vector<AuditEvent> SqliteAuditLogger::query(const AuditQuery& q) {
    std::vector<AuditEvent> out;
    if (db_ == nullptr) return out;
    const std::scoped_lock lock(mutex_);

    std::vector<std::string> bindings;
    const std::string sql = buildSelectSql(q, bindings);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr)
            != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Audit: prepare SELECT failed: {}",
                           sqlite3_errmsg(db_));
        }
        return out;
    }

    int idx = 1;
    for (const auto& b : bindings) {
        bindText(stmt, idx++, b);
    }
    // limit == 0 -> negative for SQLite "no cap"
    sqlite3_bind_int64(stmt, idx,
        q.limit == 0 ? -1 : static_cast<std::int64_t>(q.limit));

    out.reserve(std::min<std::size_t>(q.limit, 1024));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(extractEvent(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::size_t SqliteAuditLogger::totalEvents() const {
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
