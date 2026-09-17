// [utest->req~integration-009~1]
// Covers the serial-reading -> equipment-state-command mapping that the
// SerialBackend sink uses to drive ProductionModel::setEquipmentEnabled.
// Pure, header-only logic: no serial port, no boost, so it runs everywhere.

#include "src/integration/EquipmentStateReading.h"

#include <gtest/gtest.h>

namespace {

using app::integration::SerialReading;
using app::integration::toEquipmentStateCommand;

TEST(EquipmentStateReadingTest, OnCommandParses) {
    const auto cmd = toEquipmentStateCommand({"equipment/0/state", "on"});
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->equipmentId, 0U);
    EXPECT_TRUE(cmd->enabled);
}

TEST(EquipmentStateReadingTest, OffCommandParses) {
    const auto cmd = toEquipmentStateCommand({"equipment/2/state", "off"});
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->equipmentId, 2U);
    EXPECT_FALSE(cmd->enabled);
}

TEST(EquipmentStateReadingTest, NumericAndWordValuesAccepted) {
    EXPECT_TRUE(toEquipmentStateCommand({"equipment/1/state", "1"})->enabled);
    EXPECT_FALSE(toEquipmentStateCommand({"equipment/1/state", "0"})->enabled);
    EXPECT_TRUE(toEquipmentStateCommand({"equipment/1/state", "true"})->enabled);
    EXPECT_FALSE(
        toEquipmentStateCommand({"equipment/1/state", "false"})->enabled);
}

TEST(EquipmentStateReadingTest, ValueIsCaseInsensitive) {
    EXPECT_TRUE(toEquipmentStateCommand({"equipment/0/state", "ON"})->enabled);
    EXPECT_FALSE(toEquipmentStateCommand({"equipment/0/state", "OfF"})->enabled);
}

TEST(EquipmentStateReadingTest, NonEquipmentSensorIsIgnored) {
    EXPECT_FALSE(toEquipmentStateCommand({"temp", "23.5"}).has_value());
    EXPECT_FALSE(
        toEquipmentStateCommand({"sensor/0/state", "on"}).has_value());
    EXPECT_FALSE(
        toEquipmentStateCommand({"equipment/0/value", "on"}).has_value());
}

TEST(EquipmentStateReadingTest, NonNumericOrEmptyIdIsIgnored) {
    EXPECT_FALSE(
        toEquipmentStateCommand({"equipment/A/state", "on"}).has_value());
    EXPECT_FALSE(
        toEquipmentStateCommand({"equipment//state", "on"}).has_value());
}

TEST(EquipmentStateReadingTest, UnrecognisedValueIsIgnored) {
    EXPECT_FALSE(
        toEquipmentStateCommand({"equipment/0/state", "maybe"}).has_value());
    EXPECT_FALSE(
        toEquipmentStateCommand({"equipment/0/state", ""}).has_value());
}

}  // namespace
