# `src/historian/` -- Time-Series History Store

Append-only scalar time-series persistence for the application model.
Subscribes to model signals, batches changes in memory, flushes to
SQLite on a size + age threshold, and exposes a clean read API for
the History page + future BI exports. GTK-free; pluggable storage
via the `HistoryWriter` / `HistoryReader` interfaces.

---

## Why this module exists separately

Industrial HMIs need a **historian** the moment an operator asks "what
was the quality pass rate yesterday afternoon?" or compliance asks
"prove the supply level never dropped below the alarm threshold for
the last 30 days." A live dashboard answers "now"; a historian
answers "then."

Putting it in a self-contained module means:

- The model still emits change signals as it always did -- it
  doesn't know a historian exists.
- The presenter layer reads historical slices through a narrow
  `HistoryReader` interface; tests inject in-memory fakes.
- The storage choice (SQLite today, InfluxDB / TimescaleDB / Parquet
  tomorrow) is one new `HistoryWriter` + `HistoryReader` pair;
  no presenter or model change.
- Retention + vacuum policy is owned by `HistorianMaintenance`,
  a separate component the composition root schedules; no
  collision with the write hot path.

---

## Architecture (SOLID at a glance)

```
   ProductionModel ──signal──► HistorianBridge ──batch──► HistoryWriter
                                                                │
                                                                ▼
                                                       SqliteHistoryStore
                                                       (idx_history_field_ts)
                                                                ▲
                                                                │
                          HistoryPage / Presenter ──query──► HistoryReader
                                                                ▲
                                                                │
                          HistorianMaintenance ──retention──► (delete + vacuum)
```

Five small surfaces:

| Class | Role | Owns |
|---|---|---|
| `HistoryRecord` | Plain row DTO (`fieldKind`, `entityId`, `value`, `timestampMs`) | nothing |
| `HistoryWriter` | Write-only interface | nothing |
| `HistoryReader` | Read-only interface | nothing |
| `HistorianBridge` | Model -> records translator + batch buffer | in-memory batch only |
| `SqliteHistoryStore` | Concrete writer + reader on one SQLite file | the DB connection + mutex |
| `HistorianMaintenance` | Retention policy + vacuum cadence | the cron-ish scheduler |

**SOLID applied:**

- **S** -- bridge translates, store persists, maintenance prunes.
  Three concerns, three classes.
- **O** -- adding a new series (e.g. ambient temperature per
  equipment) is one new subscribe in `HistorianBridge::wire()` + one
  new `FieldKind` enum value. Storage + read paths stay untouched.
- **L** -- bridge depends on `HistoryWriter&`, presenter on
  `HistoryReader&`. Tests inject `FakeHistoryWriter` /
  `FakeHistoryReader` with identical contracts.
- **I** -- writer + reader are deliberately split. A future
  read-replica deployment uses two different concretes (write to
  primary, read from a replica) without API churn.
- **D** -- composition root wires the concretes; no singletons in
  this layer.

---

## Schema

```sql
CREATE TABLE history (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    field_kind   INTEGER NOT NULL,   -- FieldKind enum
    entity_id    INTEGER NOT NULL,   -- equipment id, checkpoint id, ...
    value        REAL    NOT NULL,
    ts_ms        INTEGER NOT NULL    -- unix millis (monotonic across rows)
);

CREATE INDEX idx_history_field_ts
    ON history (field_kind, entity_id, ts_ms);
```

The compound `(field_kind, entity_id, ts_ms)` index makes the
canonical query -- "values for series X over time window [t0, t1]" --
an O(log n) range scan. No table scan even at millions of rows.

`ts_ms` is **integer unix millis**, not ISO strings. Two reasons:
range comparisons are integer ops (faster than string compare), and
storage is 8 bytes per row vs 24 bytes for an ISO 8601 text. At
1 Hz × 6 series × 30 days = ~15M rows; the storage saving alone
shaves ~250 MB.

---

## API surface -- class-by-class

### `HistoryRecord`

```cpp
struct HistoryRecord {
    FieldKind     field;       // enum: QualityPassRate, EquipmentSupplyLevel,
                               //       SystemState, ...
    std::uint32_t entityId;    // 0..N for per-equipment / per-checkpoint series
    float         value;       // analog reading or enum value cast to float
    std::int64_t  timestampMs; // unix epoch millis
};
```

`FieldKind` enum is the discriminator. Adding a new series:
1. Add enum value in `model/ProductionTypes.h`.
2. Subscribe in `HistorianBridge::wire()` to the relevant model
   signal, emit `HistoryRecord{NewField, id, value, now()}`.
3. Done. Storage + read paths are kind-agnostic.

### `HistoryWriter`

```cpp
class HistoryWriter {
    virtual std::size_t write(std::span<const HistoryRecord> records) = 0;
};
```

Single batch write. Returning row count lets the caller log /
metric write throughput. `std::span` avoids forcing a vector copy.

### `HistoryReader`

```cpp
class HistoryReader {
    virtual std::vector<HistoryRecord>
        query(FieldKind field, std::uint32_t entityId, QueryRange range) = 0;
    virtual std::size_t totalSamples() const = 0;
};
```

`QueryRange { fromMs, toMs, limit }` -- inclusive bounds, cap to
keep UI paint snappy. `totalSamples()` is a single `SELECT
COUNT(*)`; surfaced in the History page footer.

### `HistorianBridge`

```cpp
class HistorianBridge {
    struct Config {
        std::size_t              maxBatchSize{32};
        std::chrono::milliseconds maxBatchAge{5'000};
    };

    HistorianBridge(HistoryWriter&, model::ProductionModel&, Config = {});

    void wire();    // subscribe to model signals
    void flush();   // explicit drain (shutdown path)
};
```

Batching policy: flush when batch hits `maxBatchSize` OR when the
oldest entry is older than `maxBatchAge`. Either threshold triggers
the writer call. `flush()` is idempotent -- calling it on an empty
batch is a no-op (no writer call).

### `HistorianMaintenance`

```cpp
class HistorianMaintenance {
    struct Config {
        std::chrono::seconds retention{60 * 60 * 24 * 30};  // 30 days
        std::chrono::seconds vacuumInterval{60 * 60 * 24};  // daily
    };

    HistorianMaintenance(SqliteHistoryStore&, Config = {});
    void runOnce();    // delete-old + optional vacuum
};
```

Composition root schedules `runOnce()` on a periodic timer or on
demand. Retention is per-row (`DELETE FROM history WHERE ts_ms <
?`); vacuum reclaims space after a delete batch. Both are gated
behind a mutex on the store so they never overlap with a write.

---

## Embedding in another C++ project

Minimum dependencies: `sqlite3`, C++20 compiler.

### Bootstrap

```cpp
#include "historian/SqliteHistoryStore.h"
#include "historian/HistorianBridge.h"
#include "historian/HistorianMaintenance.h"

app::historian::SqliteHistoryStore store{{ .dbPath = "data/historian.sqlite" }};
store.initialize();   // creates schema + index

app::historian::HistorianBridge bridge{store, simulatedModel};
bridge.wire();        // start recording

// On shutdown:
bridge.flush();
```

### Reading from a presenter

```cpp
app::historian::QueryRange range{
    .fromMs = nowMs - 60 * 60 * 1000,   // last hour
    .toMs   = nowMs,
    .limit  = 1000,
};
auto rows = store.query(FieldKind::QualityPassRate, /*entityId=*/0, range);
```

### Adding a new series

```cpp
// 1. Add enum value
enum class FieldKind { ..., AmbientTemperature };

// 2. In HistorianBridge::wire(), subscribe:
model_.signalAmbientTempChanged().connect(
    [this](std::uint32_t equipId, float celsius) {
        push({FieldKind::AmbientTemperature, equipId, celsius, nowMs()});
    });
```

That's it. The History page picks up the new kind automatically as
long as the UI is updated to show it.

---

## Threading model

- **Bridge callbacks** fire on whatever thread the model emits its
  signal (worker thread in production, test thread in unit tests).
- **In-memory batch** is mutex-guarded.
- **Flush call** (auto or explicit) issues one synchronous
  `writer.write(...)` while holding the batch mutex. Writer's
  internal connection mutex serialises against any concurrent
  reader.
- **Reader path** holds the store's mutex during the SELECT;
  blocked briefly during a flush. Acceptable because the SELECT
  itself is fast (compound-index range scan) and the History page
  reads infrequently (operator-triggered refresh).
- **Maintenance** acquires the same mutex; never overlaps with
  reads or writes.

---

## Testing

`tests/SqliteHistoryStoreTest.cpp` -- schema bootstrap, write +
read round trips, range query semantics (inclusive bounds, limit),
empty store edge cases.

`tests/HistorianBridgeTest.cpp` -- model signal -> record dispatch,
batch flush triggers (size + age), explicit flush idempotency.

`tests/HistorianMaintenanceTest.cpp` -- retention deletes rows
older than the cutoff, leaves newer ones, vacuum runs without
collision with writes.

`tests/HistoryPageTest.cpp` -- presenter-level: page subscribes,
fetches the configured range, populates the chart.

Run isolated:

```bash
cd build/debug
ctest -R '(History|Historian)' --output-on-failure
```

---

## Out of scope (intentional)

- **Down-sampling / rollups** -- the 30-day retention plus the
  range query's `limit` keeps UI paint OK. A multi-year archive
  with month / week / day rollups is the next architectural step
  (introduce `history_hourly`, `history_daily` tables; write
  asynchronously from a maintenance pass).
- **Compression / Parquet export** -- compliance walks ask for
  CSV slices, served by an ad-hoc query + CsvSerializer. Parquet
  would be a new `HistoryReader` impl when a customer asks.
- **Live streaming to BI** (Grafana websocket, Influx telegraf) --
  out of scope here; the `integration/` module's `TelemetryPublisher`
  handles real-time fan-out separately.
- **Multi-replica reads** -- the writer + reader split anticipates
  this but doesn't ship it; a deployment that wants a read replica
  swaps `HistoryReader` to a replica-aware concrete.
