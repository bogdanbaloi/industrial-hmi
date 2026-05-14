#include "src/historian/SqliteHistoryStore.h"

#include <sqlite3.h>

#include <string>

namespace app::historian {

namespace {

// Schema bootstrap. Single statement per call so we can surface a
// failure with the specific DDL that broke (helps when the DB lives on
// a path the user can't actually write to and the second statement
// fails after the first succeeds).
constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS samples ("
    "  ts     INTEGER NOT NULL,"
    "  field  TEXT    NOT NULL,"
    "  entity INTEGER NOT NULL,"
    "  value  REAL    NOT NULL"
    ");";

constexpr const char* kCreateIndexSql =
    "CREATE INDEX IF NOT EXISTS idx_samples_lookup "
    "ON samples(field, entity, ts);";

constexpr const char* kInsertSql =
    "INSERT INTO samples(ts, field, entity, value) VALUES (?, ?, ?, ?)";

constexpr const char* kSelectSql =
    "SELECT ts, value FROM samples "
    "WHERE field = ? AND entity = ? AND ts >= ? AND ts <= ? "
    "ORDER BY ts ASC "
    "LIMIT ?";

constexpr const char* kCountSql = "SELECT COUNT(*) FROM samples";

// SQLite has no "execute string" convenience that surfaces errors
// cleanly without prepared-statement ceremony; this tiny helper covers
// the DDL path where there are no parameters to bind.
bool execSimple(sqlite3* db, const char* sql, app::core::Logger* logger) {
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (logger != nullptr) {
            logger->error("Historian: schema exec failed: {}",
                          errMsg != nullptr ? errMsg : "(no msg)");
        }
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

}  // namespace

SqliteHistoryStore::SqliteHistoryStore(Config config)
    : config_(std::move(config)) {}

SqliteHistoryStore::~SqliteHistoryStore() {
    // Mutex acquired by the destructor so any in-flight write on
    // another thread completes before we close the handle. SQLite's
    // own thread-safety covers this but the explicit lock is cheaper
    // to reason about during shutdown ordering.
    const std::scoped_lock lock(mutex_);
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteHistoryStore::initialize() {
    const std::scoped_lock lock(mutex_);

    const int rc = sqlite3_open(config_.dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Historian: sqlite3_open({}) failed: {}",
                           config_.dbPath,
                           db_ != nullptr ? sqlite3_errmsg(db_) : "(no handle)");
        }
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }

    // Recommended pragmas for a write-mostly historian on a single
    // process. journal_mode=WAL gives concurrent readers + one writer
    // without blocking; synchronous=NORMAL trades a tiny crash window
    // for ~10x throughput vs FULL (acceptable: a power loss losing the
    // last second of telemetry is the same UX as the model not
    // sampling that second at all).
    execSimple(db_, "PRAGMA journal_mode=WAL;",      logger_);
    execSimple(db_, "PRAGMA synchronous=NORMAL;",    logger_);
    execSimple(db_, "PRAGMA temp_store=MEMORY;",     logger_);

    if (!execSimple(db_, kCreateTableSql, logger_)) return false;
    if (!execSimple(db_, kCreateIndexSql, logger_)) return false;

    if (logger_ != nullptr) {
        logger_->info("Historian: initialised at {}", config_.dbPath);
    }
    return true;
}

std::size_t SqliteHistoryStore::write(std::span<const HistoryRecord> records) {
    if (records.empty() || db_ == nullptr) return 0;

    const std::scoped_lock lock(mutex_);

    // One transaction for the whole batch. SQLite without an explicit
    // BEGIN ... COMMIT promotes every INSERT to its own transaction
    // (~50 ms each on spinning disk). Wrapping a 100-row batch drops
    // total time to ~5 ms.
    if (!execSimple(db_, "BEGIN IMMEDIATE", logger_)) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Historian: prepare INSERT failed: {}",
                           sqlite3_errmsg(db_));
        }
        execSimple(db_, "ROLLBACK", nullptr);
        return 0;
    }

    std::size_t accepted = 0;
    for (const HistoryRecord& r : records) {
        sqlite3_bind_int64(stmt, 1, r.timestampMs);
        const auto code = fieldCode(r.field);
        sqlite3_bind_text(stmt,  2, code.data(),
                          static_cast<int>(code.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt,   3, static_cast<int>(r.entityId));
        sqlite3_bind_double(stmt, 4, static_cast<double>(r.value));

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            ++accepted;
        } else if (logger_ != nullptr) {
            logger_->warn("Historian: insert step failed: {}",
                          sqlite3_errmsg(db_));
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    execSimple(db_, "COMMIT", logger_);
    return accepted;
}

std::vector<HistoryRecord>
SqliteHistoryStore::query(FieldKind field,
                          std::uint32_t entityId,
                          QueryRange range) {
    std::vector<HistoryRecord> out;
    if (db_ == nullptr) return out;

    const std::scoped_lock lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSelectSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (logger_ != nullptr) {
            logger_->error("Historian: prepare SELECT failed: {}",
                           sqlite3_errmsg(db_));
        }
        return out;
    }

    const auto code = fieldCode(field);
    sqlite3_bind_text(stmt,  1, code.data(),
                      static_cast<int>(code.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,   2, static_cast<int>(entityId));
    sqlite3_bind_int64(stmt, 3, range.fromMs);
    sqlite3_bind_int64(stmt, 4, range.toMs);
    // limit == 0 -> use SQLite's "negative means no limit" convention
    // so we don't need to branch the SQL itself.
    sqlite3_bind_int64(stmt, 5,
        range.limit == 0
            ? -1
            : static_cast<std::int64_t>(range.limit));

    out.reserve(std::min<std::size_t>(range.limit, 1024));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryRecord r;
        r.timestampMs = sqlite3_column_int64(stmt, 0);
        r.field       = field;
        r.entityId    = entityId;
        r.value       = static_cast<float>(sqlite3_column_double(stmt, 1));
        out.push_back(r);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::size_t SqliteHistoryStore::totalSamples() const {
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

}  // namespace app::historian
