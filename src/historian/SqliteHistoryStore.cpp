#include "src/historian/SqliteHistoryStore.h"

#include <sqlite3.h>

#include <array>
#include <string>

namespace app::historian {

namespace {

// Per-tier table names. Keep these aligned with the Tier enum's
// numeric order so we can index into the array with a cast.
constexpr std::array<const char*, 3> kTierTables{
    "samples",       // Tier::Raw
    "samples_1m",    // Tier::Minute
    "samples_1h"     // Tier::Hour
};

const char* tierTable(Tier t) {
    return kTierTables.at(static_cast<std::size_t>(t));
}

// Schema bootstrap -- one CREATE statement per table + index. Issued
// individually so a partial failure can surface the offending DDL.
constexpr const char* kCreateRawSql =
    "CREATE TABLE IF NOT EXISTS samples ("
    "  ts     INTEGER NOT NULL,"
    "  field  TEXT    NOT NULL,"
    "  entity INTEGER NOT NULL,"
    "  value  REAL    NOT NULL"
    ");";
constexpr const char* kCreateRawIdxSql =
    "CREATE INDEX IF NOT EXISTS idx_samples_lookup "
    "ON samples(field, entity, ts);";

constexpr const char* kCreateMinuteSql =
    "CREATE TABLE IF NOT EXISTS samples_1m ("
    "  ts     INTEGER NOT NULL,"
    "  field  TEXT    NOT NULL,"
    "  entity INTEGER NOT NULL,"
    "  value  REAL    NOT NULL"
    ");";
constexpr const char* kCreateMinuteIdxSql =
    "CREATE INDEX IF NOT EXISTS idx_samples_1m_lookup "
    "ON samples_1m(field, entity, ts);";

constexpr const char* kCreateHourSql =
    "CREATE TABLE IF NOT EXISTS samples_1h ("
    "  ts     INTEGER NOT NULL,"
    "  field  TEXT    NOT NULL,"
    "  entity INTEGER NOT NULL,"
    "  value  REAL    NOT NULL"
    ");";
constexpr const char* kCreateHourIdxSql =
    "CREATE INDEX IF NOT EXISTS idx_samples_1h_lookup "
    "ON samples_1h(field, entity, ts);";

constexpr const char* kInsertSqlRaw =
    "INSERT INTO samples(ts, field, entity, value) VALUES (?, ?, ?, ?)";

// Range-routed read shape. The table name is interpolated at the
// caller because SQLite cannot parameterise table names; the rest of
// the values are bound, so injection isn't a vector even at this layer.
//
// Note `LIMIT ?` accepts -1 == no cap (SQLite convention) so we don't
// branch the SQL on whether the caller passed a limit.
const char* selectSqlForTable(const char* table) {
    static thread_local std::string buf;
    buf  = "SELECT ts, value FROM ";
    buf += table;
    buf += " WHERE field = ? AND entity = ? AND ts >= ? AND ts <= ? "
           "ORDER BY ts ASC LIMIT ?";
    return buf.c_str();
}

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

/// Route a range duration to the coarsest tier that still has enough
/// resolution for a ~300-pixel chart. See the header docs for the
/// thresholds + the trade-off discussion.
Tier tierForRange(QueryRange range) noexcept {
    const std::int64_t durationMs = range.toMs - range.fromMs;
    constexpr std::int64_t kOneHourMs = 3'600'000;
    constexpr std::int64_t kOneDayMs  = 24 * kOneHourMs;
    if (durationMs <= kOneHourMs) return Tier::Raw;
    if (durationMs <= kOneDayMs)  return Tier::Minute;
    return Tier::Hour;
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
    // for ~10x throughput vs FULL.
    execSimple(db_, "PRAGMA journal_mode=WAL;",      logger_);
    execSimple(db_, "PRAGMA synchronous=NORMAL;",    logger_);
    execSimple(db_, "PRAGMA temp_store=MEMORY;",     logger_);

    // Three tiers, three tables. Each owns a compound (field, entity,
    // ts) index for the dominant "one series in a window" query.
    for (const char* sql : {kCreateRawSql,    kCreateRawIdxSql,
                            kCreateMinuteSql, kCreateMinuteIdxSql,
                            kCreateHourSql,   kCreateHourIdxSql}) {
        if (!execSimple(db_, sql, logger_)) return false;
    }

    if (logger_ != nullptr) {
        logger_->info("Historian: initialised at {} (3-tier schema)",
                      config_.dbPath);
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
    if (sqlite3_prepare_v2(db_, kInsertSqlRaw, -1, &stmt, nullptr) != SQLITE_OK) {
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
    const Tier tier = tierForRange(range);
    const char* table = tierTable(tier);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, selectSqlForTable(table), -1,
                           &stmt, nullptr) != SQLITE_OK) {
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
    // Historically meant "rows in the raw tier"; keep that contract
    // for the footer label on the History page (operator wants to
    // know "how much fine-grained data am I sitting on", not the
    // aggregated grand total).
    return rowCount(Tier::Raw);
}

std::size_t SqliteHistoryStore::rowCount(Tier tier) const {
    if (db_ == nullptr) return 0;
    const std::scoped_lock lock(mutex_);

    std::string sql = "SELECT COUNT(*) FROM ";
    sql += tierTable(tier);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    std::size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return n;
}

std::size_t SqliteHistoryStore::demoteOlderThan(Tier src,
                                                Tier dst,
                                                std::int64_t olderThanMs,
                                                std::int64_t bucketMs) {
    if (db_ == nullptr) return 0;
    if (bucketMs <= 0)  return 0;
    if (src == dst)     return 0;

    const std::scoped_lock lock(mutex_);

    const char* srcTable = tierTable(src);
    const char* dstTable = tierTable(dst);

    // SQLite arithmetic: `(ts / bucket) * bucket` floors a timestamp
    // to a bucket edge. Grouping by that gives bucket-keyed averages
    // per (field, entity). Aggregating with AVG keeps the schema
    // single-column (`value`) -- min/max/count would be additional
    // columns and a small but real schema break.
    //
    // The INSERT and DELETE share a transaction so demotion is atomic
    // from the reader's perspective (no time window where a row is
    // both in raw and in 1m, and never a window where it's in neither).
    if (!execSimple(db_, "BEGIN IMMEDIATE", logger_)) {
        return 0;
    }

    // Build the INSERT-from-SELECT, count of affected source rows
    // measured via `changes()` after the matching DELETE.
    {
        std::string insertSql;
        insertSql.reserve(256);
        insertSql += "INSERT INTO ";
        insertSql += dstTable;
        insertSql += "(ts, field, entity, value) "
                     "SELECT (ts / ?) * ?, field, entity, AVG(value) "
                     "FROM ";
        insertSql += srcTable;
        insertSql += " WHERE ts < ? "
                     "GROUP BY (ts / ?), field, entity";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, insertSql.c_str(), -1,
                               &stmt, nullptr) != SQLITE_OK) {
            if (logger_ != nullptr) {
                logger_->error("Historian: prepare demote-insert failed: {}",
                               sqlite3_errmsg(db_));
            }
            execSimple(db_, "ROLLBACK", nullptr);
            return 0;
        }
        sqlite3_bind_int64(stmt, 1, bucketMs);
        sqlite3_bind_int64(stmt, 2, bucketMs);
        sqlite3_bind_int64(stmt, 3, olderThanMs);
        sqlite3_bind_int64(stmt, 4, bucketMs);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            if (logger_ != nullptr) {
                logger_->warn("Historian: demote-insert step failed: {}",
                              sqlite3_errmsg(db_));
            }
            sqlite3_finalize(stmt);
            execSimple(db_, "ROLLBACK", nullptr);
            return 0;
        }
        sqlite3_finalize(stmt);
    }

    // Delete the originals; `sqlite3_changes()` gives us the row
    // count we report back to the maintenance worker.
    std::size_t deleted = 0;
    {
        std::string deleteSql = "DELETE FROM ";
        deleteSql += srcTable;
        deleteSql += " WHERE ts < ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, deleteSql.c_str(), -1,
                               &stmt, nullptr) != SQLITE_OK) {
            execSimple(db_, "ROLLBACK", nullptr);
            return 0;
        }
        sqlite3_bind_int64(stmt, 1, olderThanMs);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            deleted = static_cast<std::size_t>(sqlite3_changes(db_));
        }
        sqlite3_finalize(stmt);
    }

    execSimple(db_, "COMMIT", logger_);
    return deleted;
}

}  // namespace app::historian
