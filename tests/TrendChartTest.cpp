// [utest->req~historian-005~1]
// Covers REQ-HISTORIAN-005 (History page polish) for the TrendChart
// widget's new public surface: clear(), setUnit(), and pointCount().
//
// gtk_init() runs via ViewTestMain.cpp (shared with other GUI tests).
// All widget construction MUST happen after gtk_init or gtkmm asserts.
// Same approach as DonutChartWidgetTest: we don't pixel-diff the Cairo
// output (no screenshot harness yet); the data-path accessors are the
// contract a future refactor needs to preserve.

#include "src/gtk/view/widgets/TrendChart.h"

#include <gtest/gtest.h>

namespace {

using app::view::TrendChart;

TEST(TrendChartTest, ConstructionLeavesPointCountAtZero) {
    TrendChart chart("Test", 0.0F, 100.0F);
    EXPECT_EQ(chart.pointCount(), 0u);
}

TEST(TrendChartTest, AddPointIncrementsPointCount) {
    TrendChart chart("Test", 0.0F, 100.0F);
    chart.addPoint(50.0F);
    chart.addPoint(60.0F);
    chart.addPoint(70.0F);
    EXPECT_EQ(chart.pointCount(), 3u);
}

TEST(TrendChartTest, ClearResetsPointCount) {
    // Core REQ-HISTORIAN-005 contract: clear() drops every buffered
    // point so the next range-switch in HistoryPage does not paint
    // stale samples from the previous query window. Verified by the
    // pointCount() seam rather than by reaching into the private
    // ring buffer.
    TrendChart chart("Test", 0.0F, 100.0F);
    for (int i = 0; i < 5; ++i) {
        chart.addPoint(static_cast<float>(i) * 10.0F);
    }
    ASSERT_EQ(chart.pointCount(), 5u);

    chart.clear();
    EXPECT_EQ(chart.pointCount(), 0u);
}

TEST(TrendChartTest, AddPointAfterClearBuildsFromZero) {
    // Defence in depth: after clear() the next addPoint() must land
    // at write-position 0 and bring count back from 0 -> 1 (not
    // pick up some intermediate value the ring left behind).
    TrendChart chart("Test", 0.0F, 100.0F);
    chart.addPoint(10.0F);
    chart.addPoint(20.0F);
    chart.clear();
    chart.addPoint(99.0F);
    EXPECT_EQ(chart.pointCount(), 1u);
}

TEST(TrendChartTest, ClearOnEmptyChartIsNoop) {
    // The history page may call clear() before any addPoint()
    // (cold start of the first refresh cycle); clear() must
    // tolerate that without underflowing the ring counters.
    TrendChart chart("Test", 0.0F, 100.0F);
    chart.clear();
    EXPECT_EQ(chart.pointCount(), 0u);
    chart.clear();  // idempotent
    EXPECT_EQ(chart.pointCount(), 0u);
}

TEST(TrendChartTest, SetUnitDoesNotCrashAndChartStaysUsable) {
    // The unit string only surfaces in the latest-value overlay
    // (top-right Cairo text). We don't pixel-diff; assert the
    // widget survives setUnit and continues to accept points so
    // a regression that breaks the public surface fails here.
    TrendChart chart("Test", 0.0F, 100.0F);
    chart.setUnit("rpm");
    chart.addPoint(1234.0F);
    EXPECT_EQ(chart.pointCount(), 1u);

    // Empty unit is also valid -- a future caller wanting a
    // dimensionless number should not have to special-case the
    // setter.
    chart.setUnit("");
    chart.addPoint(5.0F);
    EXPECT_EQ(chart.pointCount(), 2u);
}

}  // namespace
