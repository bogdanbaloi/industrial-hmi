// Tests for app::view::HistoryPage -- View layer smoke + interactions.
//
// HistoryPage queries an injected HistoryReader on construction and
// after every Refresh / range change. We mock the reader with a fake
// that records every query so the test asserts on call patterns
// rather than rendered pixels.
//
// Requires GTK initialised (see ViewTestMain.cpp).

#include "src/gtk/view/pages/HistoryPage.h"
#include "src/historian/HistoryReader.h"
#include "src/historian/HistoryRecord.h"
#include "mocks/MockDialogManager.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace {

using app::historian::FieldKind;
using app::historian::HistoryReader;
using app::historian::HistoryRecord;
using app::historian::QueryRange;

/// Fake reader that records every query the page makes and lets us
/// stage canned responses. Mutex because the page might query off the
/// GTK main loop's idle handler (in practice it's on-thread, but the
/// mutex is cheap insurance).
class FakeHistoryReader : public HistoryReader {
public:
    struct Call {
        FieldKind     field;
        std::uint32_t entityId;
        QueryRange    range;
    };

    std::vector<HistoryRecord>
    query(FieldKind field,
          std::uint32_t entityId,
          QueryRange range) override {
        const std::scoped_lock lock(mutex_);
        calls_.push_back(Call{field, entityId, range});
        // Return the staged response if there is one; otherwise empty.
        return canned_;
    }

    std::size_t totalSamples() const override {
        return cannedTotal_;
    }

    std::vector<Call> calls() const {
        const std::scoped_lock lock(mutex_);
        return calls_;
    }
    void setResponse(std::vector<HistoryRecord> rows) {
        const std::scoped_lock lock(mutex_);
        canned_ = std::move(rows);
    }
    void setTotal(std::size_t n) { cannedTotal_ = n; }

private:
    mutable std::mutex          mutex_;
    std::vector<Call>           calls_;
    std::vector<HistoryRecord>  canned_;
    std::atomic<std::size_t>    cannedTotal_{0};
};

}  // namespace

class HistoryPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        page_ = Gtk::make_managed<app::view::HistoryPage>(
            mockDM_, fakeReader_);
    }

    app::test::MockDialogManager mockDM_;
    FakeHistoryReader            fakeReader_;
    app::view::HistoryPage*      page_{nullptr};
};

TEST_F(HistoryPageTest, ConstructionTriggersInitialRefresh) {
    // Page refreshes on construction so the first paint isn't blank.
    // Expect 6 query calls (3 quality + 3 supply) plus one
    // totalSamples() implicit via footer rendering.
    const auto calls = fakeReader_.calls();
    EXPECT_EQ(calls.size(), 6U)
        << "expected 3 quality + 3 supply queries on construction";
}

TEST_F(HistoryPageTest, AllSixSeriesAreQueried) {
    const auto calls = fakeReader_.calls();

    int quality = 0;
    int supply  = 0;
    for (const auto& c : calls) {
        if (c.field == FieldKind::QualityPassRate)      ++quality;
        if (c.field == FieldKind::EquipmentSupplyLevel) ++supply;
    }
    EXPECT_EQ(quality, 3);
    EXPECT_EQ(supply,  3);
}

TEST_F(HistoryPageTest, EachEntityIdAppearsExactlyOnce) {
    // The page should query entity 0, 1, 2 for each field -- catches a
    // regression where a loop bound or offset wires duplicates.
    const auto calls = fakeReader_.calls();

    int qualityHits[3] = {0, 0, 0};
    int supplyHits[3]  = {0, 0, 0};
    for (const auto& c : calls) {
        if (c.entityId > 2U) continue;
        if (c.field == FieldKind::QualityPassRate) {
            ++qualityHits[c.entityId];
        } else if (c.field == FieldKind::EquipmentSupplyLevel) {
            ++supplyHits[c.entityId];
        }
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(qualityHits[i], 1) << "quality entity " << i;
        EXPECT_EQ(supplyHits[i],  1) << "supply entity "  << i;
    }
}

TEST_F(HistoryPageTest, DefaultRangeIsOneHour) {
    // Default range picker selection is "Last hour"; the page should
    // translate that into a ~3 600 000 ms lookback.
    const auto calls = fakeReader_.calls();
    ASSERT_FALSE(calls.empty());

    const auto& c = calls.front();
    const auto lookback = c.range.toMs - c.range.fromMs;
    constexpr std::int64_t kOneHourMs = 3'600'000;
    EXPECT_NEAR(lookback, kOneHourMs, 1000)
        << "expected ~1h lookback for default range";
}
