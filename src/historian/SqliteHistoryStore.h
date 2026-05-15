#pragma once

#include "src/historian/HistoryReader.h"
#include "src/historian/HistoryWriter.h"
#include "src/core/LoggerBase.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

struct sqlite3;  // forward declare; keeps <sqlite3.h> out of this header

namespace app::historian {

/// SQLite-backed implementation of both HistoryWriter and HistoryReader.
/// Single SQLite connection guarded by an internal mutex -- SQLite's
/// default threading model (serialized) handles cross-thread access at
/// the library level, but we also serialise at C++ level to keep
/// prepared statement reuse safe.
///
/// Schema (single table, MVP):
/// @code
///   CREATE TABLE IF NOT EXISTS samples (
///       ts       INTEGER NOT NULL,   -- ms since Unix epoch
///       field    TEXT    NOT NULL,   -- 'Q' / 'S' / 'T' (see fieldCode)
///       entity   INTEGER NOT NULL,   -- 0..N-1
///       value    REAL    NOT NULL
///   );
///   CREATE INDEX IF NOT EXISTS idx_samples_lookup
///       ON samples(field, entity, ts);
/// @endcode
///
/// The compound index covers the dominant query shape -- "give me all
/// samples for field F entity E in [t0, t1] ordered by ts". A plain
/// `(ts)` index would also work but a 3-column index lets SQLite skip
/// the table scan entirely for typical UI ranges.
///
/// Tiered retention / decimation is intentionally out of scope for the
/// MVP; that lives in a follow-up (`HistorianMaintenance`) that
/// demotes older `samples_raw` rows into coarser `samples_1m` and
/// `samples_1h` tables. The current single-table layout maps cleanly
/// to that future split (the new code just adds tables; existing
/// queries remain compatible).
class SqliteHistoryStore : public HistoryWriter, public HistoryReader {
public:
    struct Config {
        /// Filesystem path of the SQLite database. `:memory:` is the
        /// in-process fallback used by tests and the disabled-historian
        /// degraded path -- writes succeed, nothing persists past
        /// process exit.
        std::string dbPath{":memory:"};
    };

    explicit SqliteHistoryStore(Config config);
    ~SqliteHistoryStore() override;

    SqliteHistoryStore(const SqliteHistoryStore&)            = delete;
    SqliteHistoryStore& operator=(const SqliteHistoryStore&) = delete;
    SqliteHistoryStore(SqliteHistoryStore&&)                 = delete;
    SqliteHistoryStore& operator=(SqliteHistoryStore&&)      = delete;

    /// Optional logger injection. When null, the store stays silent --
    /// matches the wider DI pattern (ConfigManager, presenters).
    void setLogger(app::core::Logger& logger) { logger_ = &logger; }

    /// One-time schema bootstrap. Idempotent: safe to call against an
    /// existing DB (the `IF NOT EXISTS` guards handle the second-run
    /// case). Returns false if SQLite refuses to open the file -- the
    /// caller (composition root) then disables the historian and
    /// records a startup warning.
    [[nodiscard]] bool initialize();

    // HistoryWriter
    std::size_t write(std::span<const HistoryRecord> records) override;

    // HistoryReader
    [[nodiscard]] std::vector<HistoryRecord>
    query(FieldKind field,
          std::uint32_t entityId,
          QueryRange range) override;
    [[nodiscard]] std::size_t totalSamples() const override;

private:
    Config                    config_;
    sqlite3*                  db_{nullptr};
    mutable std::mutex        mutex_;
    app::core::Logger*        logger_{nullptr};
};

}  // namespace app::historian
