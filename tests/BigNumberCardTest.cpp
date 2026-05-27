// Implements: REQ-DASHBOARD-003 (KPI top strip headline metrics).
//
// Smoke + state tests for BigNumberCard. We don't exercise Cairo
// rendering directly (would require pixel-diff harness) -- the
// widget's correctness is verified by:
//   1. Construction succeeds + defaults are sane (matches widget docs).
//   2. Setters update observable state through the public accessors
//      so a future refactor catches any drift.
//   3. Setters that compare against current state behave idempotently.
//   4. Status enum + target on/off transitions flow through cleanly.
//
// gtk_init() runs via ViewTestMain.cpp (shared with the other GUI
// tests). All widget construction MUST happen after gtk_init or
// gtkmm asserts.

#include "src/gtk/view/widgets/BigNumberCard.h"

#include <gtest/gtest.h>

namespace {

using app::view::BigNumberCard;

TEST(BigNumberCardTest, ConstructionApplyesDefaults) {
    BigNumberCard card;

    EXPECT_TRUE(card.label().empty());
    EXPECT_DOUBLE_EQ(0.0, card.value());
    EXPECT_EQ(1, card.decimals());
    EXPECT_TRUE(card.unit().empty());
    EXPECT_FALSE(card.hasTarget());
    EXPECT_DOUBLE_EQ(0.0, card.target());
    EXPECT_EQ(BigNumberCard::Status::Ok, card.status());

    // Min content size is part of the widget's contract -- the
    // dashboard layout depends on these to budget the top strip.
    EXPECT_EQ(220, BigNumberCard::kMinContentWidth);
    EXPECT_EQ(130, BigNumberCard::kMinContentHeight);
}

TEST(BigNumberCardTest, SetLabelUpdatesAccessor) {
    BigNumberCard card;
    card.setLabel("OEE");
    EXPECT_EQ(Glib::ustring("OEE"), card.label());
}

TEST(BigNumberCardTest, SetValueStoresValueAndDecimals) {
    BigNumberCard card;
    card.setValue(87.345, 2);
    EXPECT_DOUBLE_EQ(87.345, card.value());
    EXPECT_EQ(2, card.decimals());
}

TEST(BigNumberCardTest, SetValueClampsNegativeDecimalsToZero) {
    BigNumberCard card;
    // Defensive: a caller passing -1 (e.g. "no decimals please") gets
    // 0 instead of an invalid std::format spec downstream.
    card.setValue(42.0, -3);
    EXPECT_EQ(0, card.decimals());
}

TEST(BigNumberCardTest, SetUnitUpdatesAccessor) {
    BigNumberCard card;
    card.setUnit("%");
    EXPECT_EQ(Glib::ustring("%"), card.unit());
}

TEST(BigNumberCardTest, SetTargetEnablesTargetRow) {
    BigNumberCard card;
    EXPECT_FALSE(card.hasTarget());

    card.setTarget(85.0);
    EXPECT_TRUE(card.hasTarget());
    EXPECT_DOUBLE_EQ(85.0, card.target());
}

TEST(BigNumberCardTest, SetTargetAcceptsZero) {
    // Defensive: 0.0 is a valid target (e.g. "downtime should hit 0
    // minutes"). Caller signals "no target" via clearTarget(), NOT
    // by passing 0.
    BigNumberCard card;
    card.setTarget(0.0);
    EXPECT_TRUE(card.hasTarget());
    EXPECT_DOUBLE_EQ(0.0, card.target());
}

TEST(BigNumberCardTest, ClearTargetHidesRow) {
    BigNumberCard card;
    card.setTarget(85.0);
    card.clearTarget();
    EXPECT_FALSE(card.hasTarget());
    EXPECT_DOUBLE_EQ(0.0, card.target());
}

TEST(BigNumberCardTest, ClearTargetOnFreshCardIsNoOp) {
    BigNumberCard card;
    // Sequence: never set, just clear. Verifies the early-return
    // path in clearTarget() doesn't try to queue_draw on an
    // unrealised widget.
    card.clearTarget();
    EXPECT_FALSE(card.hasTarget());
}

TEST(BigNumberCardTest, StatusTransitions) {
    BigNumberCard card;
    EXPECT_EQ(BigNumberCard::Status::Ok, card.status());

    card.setStatus(BigNumberCard::Status::Warning);
    EXPECT_EQ(BigNumberCard::Status::Warning, card.status());

    card.setStatus(BigNumberCard::Status::Critical);
    EXPECT_EQ(BigNumberCard::Status::Critical, card.status());

    card.setStatus(BigNumberCard::Status::Ok);
    EXPECT_EQ(BigNumberCard::Status::Ok, card.status());
}

TEST(BigNumberCardTest, IdempotentSettersDoNotCrash) {
    // Each setter compares against current state and returns early
    // when nothing changed. The test exercises that path -- if any
    // setter forgot the guard and called queue_draw() on an
    // unrealised widget, we'd see it here.
    BigNumberCard card;
    card.setLabel("OEE");
    card.setLabel("OEE");

    card.setValue(87.3, 1);
    card.setValue(87.3, 1);

    card.setUnit("%");
    card.setUnit("%");

    card.setTarget(85.0);
    card.setTarget(85.0);

    card.setStatus(BigNumberCard::Status::Warning);
    card.setStatus(BigNumberCard::Status::Warning);

    SUCCEED();
}

}  // namespace
