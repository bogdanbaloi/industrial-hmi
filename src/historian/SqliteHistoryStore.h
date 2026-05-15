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

/// Storage tier identifier. Maps 1:1 to a physical table; the writer
/// only ever inserts into `Raw`, the maintenance worker demotes older
/// rows into the coarser tiers, the reader picks one tier per query
/// based on the requested range duration.
enum class Tier : std::uint8_t {
    Raw    = 0,  ///< per-sample, ~1s cadence, last hour
    Minute = 1,  ///< 1-minute aggregates (avg/min/max/count), last 24h
    Hour   = 2,  ///< 1-hour aggregates, archive
};

/// SQLite-backed implementation of both HistoryWriter and HistoryReader,
/// plus the demotion primitives the maintenance worker drives.
///
/// Single SQLite connection guarded by an internal mutex -- SQLite's
/// default threading model (serialized) handles cross-thread access at
/// the library level, but we also serialise at C++ level so prepared
/// statements + multi-statement transactions stay atomic.
///
/// Schema (three tables, tiered retention):
/// @code
///   CREATE TABLE samples (                  -- Tier::Raw
///       ts     INTEGER NOT NULL,            -- ms since Unix epoch
///       field  TEXT    NOT NULL,            -- 'Q' / 'S' / 'T'
///       entity INTEGER NOT NULL,            -- 0..N-1
///       value  REAL    NOT NULL
///   );
///   CREATE TABLE samples_1m (               -- Tier::Minute
///       ts     INTEGER NOT NULL,            -- ms, aligned to minute
///       field  TEXT    NOT NULL,
///       entity INTEGER NOT NULL,
///       value  REAL    NOT NULL             -- average of the bucket
///   );
///   CREATE TABLE samples_1h (               -- Tier::Hour
///       ts     INTEGER NOT NULL,            -- ms, aligned to hour
///       field  TEXT    NOT NULL,
///       entity INTEGER NOT NULL,
///       value  REAL    NOT NULL
///   );
/// @endcode
///
/// Each table carries the same compound index on
/// `(field, entity, ts)` -- the universal "give me one series in a
/// time window" lookup. Demotion is **insert + delete** in one
/// transaction so a crash between steps doesn't double-count or lose
/// rows.
///
/// Query routing (mvp):
///   range duration  <= 1h    -> Tier::Raw
///   range duration  <= 24h   -> Tier::Minute
///   range duration  >  24h   -> Tier::Hour
/// Trade-off: an operator zooming into the last 24h sees minute-
/// resolution data for the most recent hour even though raw data is
/// available -- a slightly fancier router could split the range
/// across tiers, but it doubles the SQL surface and produces the same
/// shape for the 300-pixel chart.
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

    // HistoryWriter -- always inserts into Tier::Raw.
    std::size_t write(std::span<const HistoryRecord> records) override;

    // HistoryReader -- routes the query to the tier that best matches
    // the requested range (see header docs).
    [[nodiscard]] std::vector<HistoryRecord>
    query(FieldKind field,
          std::uint32_t entityId,
          QueryRange range) override;
    [[nodiscard]] std::size_t totalSamples() const override;

    /// Aggregate every row in `src` older than `olderThanMs` into
    /// `bucketMs`-wide averages, write them to `dst`, then delete the
    /// originals. Insert+delete run in one transaction.
    ///
    /// Caller (HistorianMaintenance) is responsible for invoking this
    /// on a cadence -- the store itself stays free of timers and
    /// background threads so it remains trivially testable on the
    /// calling thread.
    ///
    /// @return number of source rows folded into the destination.
    std::size_t demoteOlderThan(Tier src,
                                Tier dst,
                                std::int64_t olderThanMs,
                                std::int64_t bucketMs);

    /// Diagnostic helpers -- the maintenance loop logs progress and
    /// the History page footer surfaces the raw total to the operator.
    [[nodiscard]] std::size_t rowCount(Tier tier) const;

private:
    Config                    config_;
    sqlite3*                  db_{nullptr};
    mutable std::mutex        mutex_;
    app::core::Logger*        logger_{nullptr};
};

}  // namespace app::historian
