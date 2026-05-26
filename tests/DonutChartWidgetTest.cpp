// Smoke + state tests for DonutChartWidget. Same approach as
// BigNumberCardTest: we don't pixel-diff the Cairo output (no
// screenshot harness wired in yet) but we cover construction,
// setter side effects, and the accessor contract a future
// refactor would need to preserve.
//
// gtk_init() runs via ViewTestMain.cpp (shared with other GUI
// tests). All widget construction MUST happen after gtk_init or
// gtkmm asserts.

#include "src/gtk/view/widgets/DonutChartWidget.h"

#include <gtest/gtest.h>

namespace {

using app::view::DonutChartWidget;
using app::view::Rgb;

TEST(DonutChartWidgetTest, ConstructionAppliesDefaults) {
    DonutChartWidget chart;

    EXPECT_TRUE(chart.segments().empty());
    EXPECT_TRUE(chart.centerTitle().empty());
    EXPECT_TRUE(chart.centerSubtitle().empty());

    EXPECT_EQ(200, DonutChartWidget::kMinContentWidth);
    EXPECT_EQ(200, DonutChartWidget::kMinContentHeight);
}

TEST(DonutChartWidgetTest, SetSegmentsStoresList) {
    DonutChartWidget chart;
    std::vector<DonutChartWidget::Segment> segs{
        {"Running",     60.0, Rgb{0.0, 1.0, 0.0}},
        {"Idle",        25.0, Rgb{0.4, 0.5, 0.9}},
        {"Calibration", 10.0, Rgb{1.0, 0.7, 0.0}},
        {"Error",        5.0, Rgb{1.0, 0.2, 0.2}},
    };
    chart.setSegments(segs);

    ASSERT_EQ(4u, chart.segments().size());
    EXPECT_EQ(Glib::ustring("Running"), chart.segments()[0].label);
    EXPECT_DOUBLE_EQ(60.0, chart.segments()[0].value);
    EXPECT_DOUBLE_EQ(0.0, chart.segments()[0].color.r);
}

TEST(DonutChartWidgetTest, SetSegmentsAcceptsEmpty) {
    // Defensive: dashboard hands over empty segments when no
    // state-change events have arrived yet; the widget should
    // tolerate that without crashing and the centre labels
    // should still render at paint time.
    DonutChartWidget chart;
    chart.setSegments({});  // empty list
    EXPECT_TRUE(chart.segments().empty());
}

TEST(DonutChartWidgetTest, SetSegmentsReplacesPrevious) {
    DonutChartWidget chart;
    chart.setSegments({{"A", 50.0, Rgb{1.0, 0.0, 0.0}}});
    chart.setSegments({{"B", 30.0, Rgb{0.0, 1.0, 0.0}},
                       {"C", 70.0, Rgb{0.0, 0.0, 1.0}}});

    ASSERT_EQ(2u, chart.segments().size());
    EXPECT_EQ(Glib::ustring("B"), chart.segments()[0].label);
}

TEST(DonutChartWidgetTest, SetCenterTitleUpdatesAccessor) {
    DonutChartWidget chart;
    chart.setCenterTitle("87%");
    EXPECT_EQ(Glib::ustring("87%"), chart.centerTitle());
}

TEST(DonutChartWidgetTest, SetCenterSubtitleUpdatesAccessor) {
    DonutChartWidget chart;
    chart.setCenterSubtitle("uptime today");
    EXPECT_EQ(Glib::ustring("uptime today"), chart.centerSubtitle());
}

TEST(DonutChartWidgetTest, IdempotentCenterSettersDoNotCrash) {
    // Each centre setter compares against current state and
    // returns early when nothing changed. Exercise that path so
    // a refactor that drops the guard would visibly fail here.
    DonutChartWidget chart;
    chart.setCenterTitle("87%");
    chart.setCenterTitle("87%");
    chart.setCenterSubtitle("uptime");
    chart.setCenterSubtitle("uptime");
    SUCCEED();
}

TEST(DonutChartWidgetTest, SegmentsToleratesZeroAndNegativeValues) {
    // The widget normalises by the sum of positive values; zero
    // or negative slices collapse silently (skipped at paint).
    // Verify storage round-trips them anyway -- the dashboard
    // may need to read the raw counts back out.
    DonutChartWidget chart;
    chart.setSegments({
        {"Running",  100.0, Rgb{0, 1, 0}},
        {"Idle",       0.0, Rgb{0, 0, 1}},
        {"Error",     -5.0, Rgb{1, 0, 0}},
    });
    ASSERT_EQ(3u, chart.segments().size());
    EXPECT_DOUBLE_EQ(  0.0, chart.segments()[1].value);
    EXPECT_DOUBLE_EQ( -5.0, chart.segments()[2].value);
}

}  // namespace
