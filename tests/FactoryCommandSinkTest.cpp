// Tests for app::integration::opcua::FactoryCommandSink.
//
// The sink has one job: translate inbound OPC-UA command names +
// writable bool paths into ProductionModel mutations. No socket, no
// open62541 -- a `MockProductionModel` + a `ConsoleLogger` is the
// entire fixture.

#include "src/integration/opcua/FactoryCommandSink.h"

#include "src/core/LoggerImpl.h"
#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

namespace {

using app::integration::opcua::FactoryCommandSink;
using app::test::MockProductionModel;
using testing::_;

class FactoryCommandSinkTest : public ::testing::Test {
protected:
    FactoryCommandSinkTest()
        : logger_{std::make_unique<app::core::ConsoleLogger>()} {}

    MockProductionModel model_;
    app::core::Logger   logger_;
};

}  // namespace

// onCommand -- the four documented method names route to the
// matching model setter; anything else is a no-op (with a log).

TEST_F(FactoryCommandSinkTest, StartProductionCommandCallsModel) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, startProduction()).Times(1);
    sink.onCommand("StartProduction");
}

TEST_F(FactoryCommandSinkTest, StopProductionCommandCallsModel) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, stopProduction()).Times(1);
    sink.onCommand("StopProduction");
}

TEST_F(FactoryCommandSinkTest, ResetSystemCommandCallsModel) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, resetSystem()).Times(1);
    sink.onCommand("ResetSystem");
}

TEST_F(FactoryCommandSinkTest, StartCalibrationCommandCallsModel) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, startCalibration()).Times(1);
    sink.onCommand("StartCalibration");
}

TEST_F(FactoryCommandSinkTest, UnknownCommandIsDropped) {
    FactoryCommandSink sink(model_, logger_);
    // The fact that no EXPECT_CALL fires plus the lack of a fatal
    // gmock failure means the sink silently dropped the call.
    EXPECT_CALL(model_, startProduction()).Times(0);
    EXPECT_CALL(model_, stopProduction()).Times(0);
    EXPECT_CALL(model_, resetSystem()).Times(0);
    EXPECT_CALL(model_, startCalibration()).Times(0);

    sink.onCommand("Bogus");
    sink.onCommand("");
    sink.onCommand("startproduction");  // case-sensitive on purpose
}

// onBoolWrite -- only the equipment-Enabled path is recognised; the
// id is parsed out of the browse name.

TEST_F(FactoryCommandSinkTest, EnabledTrueCallsSetEquipmentEnabledTrue) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, setEquipmentEnabled(0U, true)).Times(1);
    sink.onBoolWrite("Factory/EquipmentLines/Line0/Enabled", true);
}

TEST_F(FactoryCommandSinkTest, EnabledFalseCallsSetEquipmentEnabledFalse) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, setEquipmentEnabled(2U, false)).Times(1);
    sink.onBoolWrite("Factory/EquipmentLines/Line2/Enabled", false);
}

TEST_F(FactoryCommandSinkTest, EnabledPathParsesMultiDigitId) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, setEquipmentEnabled(42U, true)).Times(1);
    sink.onBoolWrite("Factory/EquipmentLines/Line42/Enabled", true);
}

TEST_F(FactoryCommandSinkTest, UnknownBoolPathIsDropped) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, setEquipmentEnabled(_, _)).Times(0);

    sink.onBoolWrite("Factory/Something/Else", true);
    sink.onBoolWrite("Factory/EquipmentLines/Line0/Status", true);
    sink.onBoolWrite("", false);
}

TEST_F(FactoryCommandSinkTest, MalformedEquipmentIdIsRejected) {
    FactoryCommandSink sink(model_, logger_);
    EXPECT_CALL(model_, setEquipmentEnabled(_, _)).Times(0);

    sink.onBoolWrite("Factory/EquipmentLines/Line/Enabled", true);
    sink.onBoolWrite("Factory/EquipmentLines/LineX/Enabled", true);
    sink.onBoolWrite("Factory/EquipmentLines/Line-1/Enabled", true);
    sink.onBoolWrite("Factory/EquipmentLines/Line 0/Enabled", true);
}
