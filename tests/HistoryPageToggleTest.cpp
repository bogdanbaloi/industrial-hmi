// [utest->req~historian-006~1]
// Covers REQ-HISTORIAN-006 (per-checkpoint chart visibility toggle):
// each of the 6 TrendCharts has a labelled CheckButton; toggling it
// shows/hides the chart; hidden charts keep getting queried so they
// show current data when re-enabled; refresh never force-shows a
// chart the operator hid.
//
// View-layer test. Requires GTK initialised (see ViewTestMain.cpp).
// Mirrors HistoryPageTest's fixture; FakeHistoryReader is copied
// locally (the established per-file pattern in this repo).

#include "src/gtk/view/pages/HistoryPage.h"
#include "src/historian/HistoryReader.h"
#include "src/historian/HistoryRecord.h"
#include "mocks/MockDialogManager.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace {

using app::historian::FieldKind;
using app::historian::HistoryReader;
using app::historian::HistoryRecord;
using app::historian::QueryRange;

/// Records every query so a test can assert that hidden charts are
/// still fetched. Same shape as the fake in HistoryPageTest.
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
        return canned_;
    }

    std::size_t totalSamples() const override { return cannedTotal_; }

    std::size_t callCount() const {
        const std::scoped_lock lock(mutex_);
        return calls_.size();
    }

private:
    mutable std::mutex          mutex_;
    std::vector<Call>           calls_;
    std::vector<HistoryRecord>  canned_;
    std::atomic<std::size_t>    cannedTotal_{0};
};

constexpr std::size_t kQuality = 3;
constexpr std::size_t kSupply  = 3;

}  // namespace

class HistoryPageToggleTest : public ::testing::Test {
protected:
    void SetUp() override {
        page_ = Gtk::make_managed<app::view::HistoryPage>(
            mockDM_, fakeReader_);
    }

    app::test::MockDialogManager mockDM_;
    FakeHistoryReader            fakeReader_;
    app::view::HistoryPage*      page_{nullptr};
};

TEST_F(HistoryPageToggleTest, AllChartsVisibleByDefault) {
    for (std::size_t i = 0; i < kQuality; ++i) {
        EXPECT_TRUE(page_->chartVisible(/*isQuality=*/true, i))
            << "quality chart " << i << " should start visible";
    }
    for (std::size_t i = 0; i < kSupply; ++i) {
        EXPECT_TRUE(page_->chartVisible(/*isQuality=*/false, i))
            << "supply chart " << i << " should start visible";
    }
}

TEST_F(HistoryPageToggleTest, HidingQualityChartMakesItInvisible) {
    page_->hideChartForTest(/*isQuality=*/true, 1);

    EXPECT_FALSE(page_->chartVisible(true, 1));
    // The siblings stay visible -- hiding is per-chart, not per-group.
    EXPECT_TRUE(page_->chartVisible(true, 0));
    EXPECT_TRUE(page_->chartVisible(true, 2));
}

TEST_F(HistoryPageToggleTest, HidingSupplyChartMakesItInvisible) {
    page_->hideChartForTest(/*isQuality=*/false, 2);

    EXPECT_FALSE(page_->chartVisible(false, 2));
    EXPECT_TRUE(page_->chartVisible(false, 0));
    EXPECT_TRUE(page_->chartVisible(false, 1));
}

TEST_F(HistoryPageToggleTest, ReshowingChartRestoresVisibility) {
    page_->hideChartForTest(true, 0);
    ASSERT_FALSE(page_->chartVisible(true, 0));

    // Re-check the toggle (drives the chart back to visible).
    page_->showChartForTest(true, 0);
    EXPECT_TRUE(page_->chartVisible(true, 0));
}

TEST_F(HistoryPageToggleTest, RefreshDoesNotForceHiddenChartVisible) {
    // The key regression guard: refreshAllCharts() must touch only the
    // data path (clear + addPoint), never widget visibility. A future
    // edit that inserts set_visible(true) into the refresh loop would
    // fail here.
    page_->hideChartForTest(true, 0);
    ASSERT_FALSE(page_->chartVisible(true, 0));

    page_->triggerRefreshForTest();

    EXPECT_FALSE(page_->chartVisible(true, 0))
        << "a manual refresh must not re-show a chart the operator hid";
}

TEST_F(HistoryPageToggleTest, AllSixQueriesStillRunWhenChartHidden) {
    // Hidden charts keep collecting so they show current data the
    // moment they are re-enabled.
    const auto baseline = fakeReader_.callCount();  // 6 from construction
    page_->hideChartForTest(true, 0);

    page_->triggerRefreshForTest();

    EXPECT_EQ(fakeReader_.callCount(), baseline + 6U)
        << "hiding a chart must not suppress its query on refresh";
}
