#pragma once

#include "src/historian/HistoryRecord.h"

#include <cstdint>
#include <vector>

namespace app::historian {

/// Bounded time range query parameters. Stored explicitly (not as a
/// `std::chrono::system_clock::time_point` pair) for the same reasons
/// HistoryRecord uses ms-since-epoch -- schema compatibility, cross-
/// binary stability, test ergonomics.
struct QueryRange {
    std::int64_t fromMs{0};
    std::int64_t toMs{0};

    /// Cap on returned rows. The presentation layer scales for a fixed
    /// pixel budget; without a cap, zoom-out queries over the full
    /// archive could pull millions of rows and starve the UI thread.
    /// 0 means "no explicit cap" -- callers that genuinely want every
    /// row (e.g. CSV export) pass 0 and accept the cost.
    static constexpr std::size_t kDefaultLimit = 10'000;
    std::size_t  limit{kDefaultLimit};
};

/// Read-only side of the historian. Separated from HistoryWriter so:
///
///   * The UI / export code depends only on this narrow surface --
///     can't accidentally write while rendering a trend chart.
///   * Tests that exercise queries don't need to mock the write API.
///   * A future "read replica" tier (e.g. an in-process cache fed by
///     a real database elsewhere) can implement Reader without
///     implementing Writer.
///
/// Threading: implementations must allow concurrent reads from any
/// thread. Concurrent reads alongside writes are the common case
/// (UI polls history while the bridge persists new samples).
class HistoryReader {
public:
    virtual ~HistoryReader() = default;

    HistoryReader(const HistoryReader&)            = delete;
    HistoryReader& operator=(const HistoryReader&) = delete;
    HistoryReader(HistoryReader&&)                 = delete;
    HistoryReader& operator=(HistoryReader&&)      = delete;

    /// Fetch samples for one specific series in a time window, sorted
    /// by timestamp ascending. Empty result is normal (no data in
    /// range; cold start).
    [[nodiscard]] virtual std::vector<HistoryRecord>
    query(FieldKind field,
          std::uint32_t entityId,
          QueryRange range) = 0;

    /// Count rows currently in the store. Cheap (O(1) on SQLite via
    /// `sqlite_stat1` or a maintained counter). Used by the History
    /// page footer ("Total: 12 345 samples").
    [[nodiscard]] virtual std::size_t totalSamples() const = 0;

protected:
    HistoryReader() = default;
};

}  // namespace app::historian
