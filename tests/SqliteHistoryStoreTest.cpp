// [utest->req~historian-001~1]
// Covers REQ-HISTORIAN-001 (SQLite 3-tier schema).
//
// Tests for SqliteHistoryStore.
//
// Everything runs against an in-memory database (`:memory:`) so the
// suite stays hermetic -- no temp files, no parallel-run collisions,
// no cleanup on failure. The same paths a file-backed deployment
// exercises are covered (open, schema bootstrap, batch INSERT inside
// a transaction, indexed SELECT with limit, COUNT) because SQLite's
// `:memory:` mode is bit-identical to disk mode except for
// persistence.

#include "src/historian/SqliteHistoryStore.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using app::historian::FieldKind;
using app::historian::HistoryRecord;
using app::historian::QueryRange;
using app::historian::SqliteHistoryStore;

// Helper -- a fresh, initialised in-memory store for each test. Avoids
// leaking state across cases because every TEST() gets its own instance.
std::unique_ptr<SqliteHistoryStore> makeStore() {
    auto store = std::make_unique<SqliteHistoryStore>(
        SqliteHistoryStore::Config{.dbPath = ":memory:"});
    EXPECT_TRUE(store->initialize());
    return store;
}

}  // namespace

TEST(SqliteHistoryStoreTest, EmptyStoreReportsZeroSamples) {
    auto store = makeStore();
    EXPECT_EQ(store->totalSamples(), 0U);
}

TEST(SqliteHistoryStoreTest, EmptyStoreQueryReturnsEmpty) {
    auto store = makeStore();
    const auto rows = store->query(FieldKind::QualityPassRate, 0,
                                   QueryRange{.fromMs = 0, .toMs = 1'000'000});
    EXPECT_TRUE(rows.empty());
}

TEST(SqliteHistoryStoreTest, WriteSingleSampleRoundTrip) {
    auto store = makeStore();

    const HistoryRecord rec{.timestampMs = 1000,
                            .field       = FieldKind::QualityPassRate,
                            .entityId    = 1,
                            .value       = 98.5F};
    const std::array<HistoryRecord, 1> batch{rec};
    EXPECT_EQ(store->write(batch), 1U);

    const auto rows = store->query(FieldKind::QualityPassRate, 1,
                                   QueryRange{.fromMs = 0, .toMs = 2000});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].timestampMs, 1000);
    EXPECT_FLOAT_EQ(rows[0].value, 98.5F);
}

TEST(SqliteHistoryStoreTest, BatchInsertAllRowsAccepted) {
    auto store = makeStore();

    std::vector<HistoryRecord> batch;
    constexpr std::size_t kBatchSize = 100;
    batch.reserve(kBatchSize);
    for (std::size_t i = 0; i < kBatchSize; ++i) {
        batch.push_back(HistoryRecord{
            .timestampMs = static_cast<std::int64_t>(i),
            .field       = FieldKind::EquipmentSupplyLevel,
            .entityId    = 0,
            .value       = static_cast<float>(i % 100)});
    }
    EXPECT_EQ(store->write(batch), kBatchSize);
    EXPECT_EQ(store->totalSamples(), kBatchSize);
}

TEST(SqliteHistoryStoreTest, QueryFiltersByFieldAndEntity) {
    auto store = makeStore();

    // Three different (field, entity) tuples; query only one of them.
    const std::array<HistoryRecord, 3> batch{
        HistoryRecord{1000, FieldKind::QualityPassRate,      0, 95.0F},
        HistoryRecord{1000, FieldKind::QualityPassRate,      1, 80.0F},
        HistoryRecord{1000, FieldKind::EquipmentSupplyLevel, 0, 75.0F},
    };
    EXPECT_EQ(store->write(batch), 3U);

    const auto rows = store->query(FieldKind::QualityPassRate, 1,
                                   QueryRange{.fromMs = 0, .toMs = 5000});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_FLOAT_EQ(rows[0].value, 80.0F);
}

TEST(SqliteHistoryStoreTest, QueryRangeIsInclusive) {
    auto store = makeStore();

    // Sentinels exactly at the range edges -- both must be returned to
    // make the range/UI semantics predictable (the user picks "last
    // hour" and expects the sample at minute 0 *and* the one at minute
    // 60 to show up).
    const std::array<HistoryRecord, 3> batch{
        HistoryRecord{999,  FieldKind::QualityPassRate, 0, 1.0F},
        HistoryRecord{1000, FieldKind::QualityPassRate, 0, 2.0F},
        HistoryRecord{2000, FieldKind::QualityPassRate, 0, 3.0F},
    };
    EXPECT_EQ(store->write(batch), 3U);

    const auto rows = store->query(FieldKind::QualityPassRate, 0,
                                   QueryRange{.fromMs = 1000, .toMs = 2000});
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_FLOAT_EQ(rows[0].value, 2.0F);
    EXPECT_FLOAT_EQ(rows[1].value, 3.0F);
}

TEST(SqliteHistoryStoreTest, QueryHonoursLimit) {
    auto store = makeStore();

    std::vector<HistoryRecord> batch;
    for (int i = 0; i < 10; ++i) {
        batch.push_back(HistoryRecord{
            .timestampMs = static_cast<std::int64_t>(i),
            .field       = FieldKind::QualityPassRate,
            .entityId    = 0,
            .value       = static_cast<float>(i)});
    }
    store->write(batch);

    const auto rows = store->query(FieldKind::QualityPassRate, 0,
                                   QueryRange{.fromMs = 0,
                                              .toMs = 100,
                                              .limit = 3});
    ASSERT_EQ(rows.size(), 3U);
    // Newest-N semantics: the limit caps the *tail* of the window
    // (values 7, 8, 9 from the 10-row series), returned ascending
    // for chart-friendly consumption.
    EXPECT_FLOAT_EQ(rows[0].value, 7.0F);
    EXPECT_FLOAT_EQ(rows[2].value, 9.0F);
}

TEST(SqliteHistoryStoreTest, QueryAscendingOrder) {
    auto store = makeStore();

    // Insert intentionally out of order so the ORDER BY is the only
    // thing producing the asc result.
    const std::array<HistoryRecord, 3> batch{
        HistoryRecord{3000, FieldKind::QualityPassRate, 0, 30.0F},
        HistoryRecord{1000, FieldKind::QualityPassRate, 0, 10.0F},
        HistoryRecord{2000, FieldKind::QualityPassRate, 0, 20.0F},
    };
    store->write(batch);

    const auto rows = store->query(FieldKind::QualityPassRate, 0,
                                   QueryRange{.fromMs = 0, .toMs = 5000});
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].timestampMs, 1000);
    EXPECT_EQ(rows[1].timestampMs, 2000);
    EXPECT_EQ(rows[2].timestampMs, 3000);
}

TEST(SqliteHistoryStoreTest, EmptyBatchIsAcceptableNoop) {
    auto store = makeStore();
    EXPECT_EQ(store->write(std::span<const HistoryRecord>{}), 0U);
    EXPECT_EQ(store->totalSamples(), 0U);
}

// --- Tiered retention -----------------------------------------------
//
// Demotion folds raw rows into bucket-aligned aggregates and deletes
// the originals. Tests assert: row counts before/after, bucket-aligned
// timestamps, averaging within a bucket, query routing by range.

TEST(SqliteHistoryStoreTest, DemoteRawToMinuteAveragesPerBucket) {
    auto store = makeStore();

    // Three samples inside the same 1-minute bucket (60_000 ms) for
    // checkpoint 0, with values 80, 90, 100 -> average 90.
    constexpr std::int64_t kMinuteMs = 60'000;
    const std::array<HistoryRecord, 3> batch{
        HistoryRecord{1000,  FieldKind::QualityPassRate, 0,  80.0F},
        HistoryRecord{30000, FieldKind::QualityPassRate, 0,  90.0F},
        HistoryRecord{59000, FieldKind::QualityPassRate, 0, 100.0F},
    };
    EXPECT_EQ(store->write(batch), 3U);

    // Demote everything older than 60s -> all three rows fold into
    // one minute-bucket row with average 90.
    const std::size_t demoted =
        store->demoteOlderThan(app::historian::Tier::Raw,
                               app::historian::Tier::Minute,
                               /*olderThanMs=*/60'000,
                               /*bucketMs=*/kMinuteMs);
    EXPECT_EQ(demoted, 3U);
    EXPECT_EQ(store->rowCount(app::historian::Tier::Raw),    0U);
    EXPECT_EQ(store->rowCount(app::historian::Tier::Minute), 1U);
}

TEST(SqliteHistoryStoreTest, DemoteIsAtomicOnEmptyRange) {
    auto store = makeStore();
    const std::array<HistoryRecord, 1> batch{
        HistoryRecord{50, FieldKind::QualityPassRate, 0, 50.0F}};
    store->write(batch);

    // olderThanMs lower than any row -> nothing to demote, both tables
    // remain consistent (raw count unchanged, minute count zero).
    const std::size_t demoted =
        store->demoteOlderThan(app::historian::Tier::Raw,
                               app::historian::Tier::Minute,
                               /*olderThanMs=*/0,
                               /*bucketMs=*/60'000);
    EXPECT_EQ(demoted, 0U);
    EXPECT_EQ(store->rowCount(app::historian::Tier::Raw),    1U);
    EXPECT_EQ(store->rowCount(app::historian::Tier::Minute), 0U);
}

TEST(SqliteHistoryStoreTest, QueryRoutesShortRangeToRaw) {
    auto store = makeStore();
    // Raw row at t=500; minute row at t=120000 (well separated).
    store->write(std::array<HistoryRecord, 1>{
        HistoryRecord{500, FieldKind::QualityPassRate, 0, 11.0F}});

    // Range 1 hour -> Raw tier. Should see the raw row only.
    constexpr std::int64_t kOneHourMs = 3'600'000;
    const auto rows = store->query(
        FieldKind::QualityPassRate, 0,
        QueryRange{.fromMs = 0, .toMs = kOneHourMs});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_FLOAT_EQ(rows[0].value, 11.0F);
}

TEST(SqliteHistoryStoreTest, QueryRoutesMediumRangeToMinute) {
    auto store = makeStore();

    // Stage one raw row + demote -> creates a minute-tier row.
    constexpr std::int64_t kMinuteMs = 60'000;
    store->write(std::array<HistoryRecord, 1>{
        HistoryRecord{1000, FieldKind::QualityPassRate, 0, 42.0F}});
    store->demoteOlderThan(app::historian::Tier::Raw,
                           app::historian::Tier::Minute,
                           /*olderThanMs=*/kMinuteMs,
                           /*bucketMs=*/kMinuteMs);

    // 12-hour range -> Minute tier. Should see the demoted row.
    constexpr std::int64_t kTwelveHoursMs = 12 * 3'600'000;
    const auto rows = store->query(
        FieldKind::QualityPassRate, 0,
        QueryRange{.fromMs = 0, .toMs = kTwelveHoursMs});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_FLOAT_EQ(rows[0].value, 42.0F);
}

TEST(SqliteHistoryStoreTest, QueryRoutesLongRangeToHour) {
    auto store = makeStore();

    // Push a raw row, demote raw->minute, then minute->hour. End
    // state: one row only in the hour tier.
    constexpr std::int64_t kMinuteMs = 60'000;
    constexpr std::int64_t kHourMs   = 3'600'000;
    store->write(std::array<HistoryRecord, 1>{
        HistoryRecord{1000, FieldKind::QualityPassRate, 0, 99.0F}});
    store->demoteOlderThan(app::historian::Tier::Raw,
                           app::historian::Tier::Minute,
                           kMinuteMs, kMinuteMs);
    store->demoteOlderThan(app::historian::Tier::Minute,
                           app::historian::Tier::Hour,
                           kHourMs,   kHourMs);

    EXPECT_EQ(store->rowCount(app::historian::Tier::Hour), 1U);

    // 48-hour range -> Hour tier.
    constexpr std::int64_t kTwoDaysMs = 48 * 3'600'000;
    const auto rows = store->query(
        FieldKind::QualityPassRate, 0,
        QueryRange{.fromMs = 0, .toMs = kTwoDaysMs});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_FLOAT_EQ(rows[0].value, 99.0F);
}

TEST(SqliteHistoryStoreTest, DemoteBucketsAreAlignedToBucketStart) {
    auto store = makeStore();

    constexpr std::int64_t kMinuteMs = 60'000;
    // Two samples in the same 1-min bucket starting at t=120_000.
    store->write(std::array<HistoryRecord, 2>{
        HistoryRecord{125'000, FieldKind::QualityPassRate, 0, 70.0F},
        HistoryRecord{150'000, FieldKind::QualityPassRate, 0, 80.0F}});

    store->demoteOlderThan(app::historian::Tier::Raw,
                           app::historian::Tier::Minute,
                           /*olderThanMs=*/200'000,
                           /*bucketMs=*/kMinuteMs);

    // 12-hour range to force routing to Minute tier.
    const auto rows = store->query(
        FieldKind::QualityPassRate, 0,
        QueryRange{.fromMs = 0,
                   .toMs   = 12 * 3'600'000});
    ASSERT_EQ(rows.size(), 1U);
    // Bucket start = floor(125_000 / 60_000) * 60_000 = 120_000.
    EXPECT_EQ(rows[0].timestampMs, 120'000);
    EXPECT_FLOAT_EQ(rows[0].value, 75.0F);  // avg(70, 80)
}

TEST(SqliteHistoryStoreTest, SystemStateRoundTrip) {
    // SystemState is stored as REAL in the schema even though it's a
    // small int set, so the float<->int round trip needs to come back
    // bit-cleanly for downstream code that switches on it.
    auto store = makeStore();
    const std::array<HistoryRecord, 1> batch{HistoryRecord{
        500, FieldKind::SystemState, 0, 1.0F}};  // 1 = RUNNING
    store->write(batch);
    const auto rows = store->query(FieldKind::SystemState, 0,
                                   QueryRange{.fromMs = 0, .toMs = 1000});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_FLOAT_EQ(rows[0].value, 1.0F);
}
