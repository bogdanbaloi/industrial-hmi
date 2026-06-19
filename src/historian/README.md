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
- Tiered retention / down-sampling is owned by
  `HistorianMaintenance`, a separate component that runs its own
  `std::jthread`; no collision with the write hot path.

---

## Architecture (SOLID at a glance)

```
   ProductionModel ──signal──► HistorianBridge ──batch──► HistoryWriter
                                                                │
                                                                ▼
                                                       SqliteHistoryStore
                                                       (samples / samples_1m
                                                        / samples_1h)
                                                                ▲
                                                                │
                          HistoryPage / Presenter ──query──► HistoryReader
                                                                ▲
                                                                │
                          HistorianMaintenance ──demote──► (raw->1m->1h)
```

Five small surfaces:

| Class | Role | Owns |
|---|---|---|
| `HistoryRecord` | Plain row DTO (`field`, `entityId`, `value`, `timestampMs`) | nothing |
| `HistoryWriter` | Write-only interface | nothing |
| `HistoryReader` | Read-only interface | nothing |
| `HistorianBridge` | Model -> records translator + batch buffer | in-memory batch only |
| `SqliteHistoryStore` | Concrete writer + reader on a tiered SQLite file | the DB connection + mutex |
| `HistorianMaintenance` | Tiered demotion cadence + policy | its own `std::jthread` |

**SOLID applied:**

- **S** -- bridge translates, store persists, maintenance demotes.
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

Three tiered tables, identical column shape, one per storage `Tier`
(ADR-0007). The writer only ever inserts into `samples` (raw); the
maintenance worker demotes older rows down the tiers; the reader
picks one tier per query based on the requested range duration.

```sql
CREATE TABLE samples (          -- Tier::Raw   (~1s cadence, last hour)
    ts     INTEGER NOT NULL,    -- ms since Unix epoch
    field  TEXT    NOT NULL,    -- single-char code: 'Q' / 'S' / 'T'
    entity INTEGER NOT NULL,    -- 0..N-1 (equipment / checkpoint id)
    value  REAL    NOT NULL
);
CREATE TABLE samples_1m (       -- Tier::Minute (1-min averages, last 24h)
    ts     INTEGER NOT NULL,    -- ms, aligned to the minute
    field  TEXT    NOT NULL,
    entity INTEGER NOT NULL,
    value  REAL    NOT NULL     -- average of the bucket
);
CREATE TABLE samples_1h (       -- Tier::Hour   (1-hour averages, archive)
    ts     INTEGER NOT NULL,    -- ms, aligned to the hour
    field  TEXT    NOT NULL,
    entity INTEGER NOT NULL,
    value  REAL    NOT NULL
);

-- Each table carries the same compound index:
CREATE INDEX ... ON <table> (field, entity, ts);
```

The compound `(field, entity, ts)` index makes the canonical query
-- "values for series X over time window [t0, t1]" -- an O(log n)
range scan on whichever tier the router picks. No table scan even
at millions of rows.

`field` is a stable single-character code (`'Q'` QualityPassRate /
`'S'` EquipmentSupplyLevel / `'T'` SystemState) so a human browsing
the DB with `sqlite3` can read a row without a lookup join.

`ts` is **integer unix millis**, not ISO strings. Two reasons:
range comparisons are integer ops (faster than string compare), and
storage is 8 bytes per row vs 24 bytes for an ISO 8601 text. The
tiered demotion keeps the raw table bounded (~1h) so the archive
itself never grows unbounded at raw resolution.

Query routing (mvp): range <= 1h -> `samples`; <= 24h ->
`samples_1m`; > 24h -> `samples_1h`. Demotion is **insert + delete
in one transaction** so a crash between steps never double-counts or
loses rows.

---

## API surface -- class-by-class

### `HistoryRecord`

```cpp
struct HistoryRecord {
    std::int64_t  timestampMs{0};                       // unix epoch millis
    FieldKind     field{FieldKind::QualityPassRate};    // QualityPassRate,
                                                        // EquipmentSupplyLevel,
                                                        // SystemState
    std::uint32_t entityId{0};   // 0..N per-equipment / per-checkpoint series
    float         value{0.0F};   // analog reading or enum value cast to float
};
```

`FieldKind` enum (defined in `HistoryRecord.h`) is the
discriminator. Adding a new series:
1. Append an enum value in `HistoryRecord.h` (do not reorder --
   archived rows keep their integer codes) and extend the
   `fieldCode()` switch.
2. Subscribe in `HistorianBridge::wire()` to the relevant model
   signal, emit `HistoryRecord{now(), NewField, id, value}`.
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
keep UI paint snappy. `limit` defaults to
`QueryRange::kDefaultLimit = 10000`; pass `0` for "no cap" (the
CSV-export path that genuinely wants every row).
`totalSamples()` is a single `SELECT COUNT(*)`; surfaced in the
History page footer.

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
        std::chrono::milliseconds sweepInterval{std::chrono::minutes{1}};
        std::chrono::milliseconds rawRetention{std::chrono::hours{1}};
        std::chrono::milliseconds minuteRetention{std::chrono::hours{24}};
    };

    explicit HistorianMaintenance(SqliteHistoryStore&);
    HistorianMaintenance(SqliteHistoryStore&, Config);

    void start();           // spawn the worker jthread
    void stop() noexcept;   // idempotent; also called from the dtor
    void runOnce();         // one sweep on the caller's thread
};
```

The class **owns its own `std::jthread`** (sleeps on a
`condition_variable_any` keyed to the stop token, so `stop()` and
the destructor return within ms rather than the full sweep
interval). A dedicated thread rather than a Glib timer because the
worker must run in both the GTK binary and the timer-less console
binary.

Each tick is **demotion, not vacuum**: the worker calls
`store_.demoteOlderThan(...)` twice -- raw -> minute, then minute ->
hour -- with the retention thresholds from the Config. Demotion is
insert+delete in one transaction, gated behind the store mutex so
it never overlaps a write. `runOnce()` runs a single sweep on the
caller's thread (composition-root shutdown drain + unit tests
without spinning the thread).

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
// 1. Add enum value (in HistoryRecord.h) + extend fieldCode()
enum class FieldKind { ..., AmbientTemperature };

// 2. In HistorianBridge::wire(), subscribe:
model_.signalAmbientTempChanged().connect(
    [this](std::uint32_t equipId, float celsius) {
        // HistoryRecord member order: timestampMs, field, entityId, value
        push({nowMs(), FieldKind::AmbientTemperature, equipId, celsius});
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
- **Maintenance** runs on its own `std::jthread`; each demotion
  acquires the same store mutex, so it never overlaps with reads
  or writes.

---

## Testing

`tests/SqliteHistoryStoreTest.cpp` -- schema bootstrap, write +
read round trips, range query semantics (inclusive bounds, limit),
empty store edge cases.

`tests/HistorianBridgeTest.cpp` -- model signal -> record dispatch,
batch flush triggers (size + age), explicit flush idempotency.

`tests/HistorianMaintenanceTest.cpp` -- demotion folds raw rows
older than the cutoff into minute (then minute into hour) buckets,
leaves newer rows in place, sweep cadence + start/stop lifecycle.

`tests/HistoryPageTest.cpp` -- presenter-level: page subscribes,
fetches the configured range, populates the chart.

Run isolated:

```bash
cd build/debug
ctest -R '(History|Historian)' --output-on-failure
```

---

## Out of scope (intentional)

- **Day / week rollups beyond the hour tier** -- raw / 1-minute /
  1-hour tiering ships (ADR-0007, REQ-HISTORIAN-001..006); a
  multi-year archive with `samples_1d` / `samples_1w` tiers is the
  next step (one more `demoteOlderThan` call in the maintenance
  tick + one more table). Not wired because no deployment retains
  beyond the hour tier yet.
- **Compression / Parquet export** -- compliance walks ask for
  CSV slices, served by an ad-hoc query + CsvSerializer. Parquet
  would be a new `HistoryReader` impl when a customer asks.
- **Live streaming to BI** (Grafana websocket, Influx telegraf) --
  out of scope here; the `integration/` module's `TelemetryPublisher`
  handles real-time fan-out separately.
- **Multi-replica reads** -- the writer + reader split anticipates
  this but doesn't ship it; a deployment that wants a read replica
  swaps `HistoryReader` to a replica-aware concrete.
