#pragma once

#include "src/historian/HistoryRecord.h"

#include <span>

namespace app::historian {

/// Write-only sink for time-series samples.
///
/// The bridge layer (HistorianBridge) subscribes to ProductionModel
/// signals and pushes one record per change here. Concrete
/// implementations decide *where* the data lands -- SQLite on disk
/// (production), an in-memory vector (tests), `/dev/null` (when the
/// historian is disabled by config).
///
/// SOLID:
///   * S -- one job: durably accept samples. No domain awareness, no
///     read API, no query semantics. Querying lives behind a separate
///     `HistoryReader` interface; concrete stores typically implement
///     both, but consumers depend on whichever surface they need.
///   * O -- new storage engines (Postgres, InfluxDB, custom binary
///     log) plug in as new concretes without touching the bridge.
///   * I -- intentionally narrow: one batch-aware write entry point.
///     Single-record convenience overloads live as free helpers so the
///     interface stays minimal.
///   * D -- bridges depend on `HistoryWriter&`; the concrete is
///     selected once at composition root.
///
/// Threading: implementations must serialise writes internally; the
/// bridge calls `write` from whatever thread the model dispatches on
/// (model-thread for callbacks, ingest-thread for inbound bridges).
class HistoryWriter {
public:
    virtual ~HistoryWriter() = default;

    HistoryWriter(const HistoryWriter&)            = delete;
    HistoryWriter& operator=(const HistoryWriter&) = delete;
    HistoryWriter(HistoryWriter&&)                 = delete;
    HistoryWriter& operator=(HistoryWriter&&)      = delete;

    /// Persist a batch of records. Batches let the SQLite implementation
    /// wrap an INSERT in a single transaction for an ~80x throughput
    /// gain over per-row commits. Bridges call this with one-record
    /// spans on hot paths and longer spans when draining a buffer.
    ///
    /// @return number of records actually durably accepted. Equal to
    ///         `records.size()` on success; smaller on partial failure
    ///         (e.g. disk full mid-batch). Never negative.
    virtual std::size_t write(std::span<const HistoryRecord> records) = 0;

protected:
    HistoryWriter() = default;
};

}  // namespace app::historian
