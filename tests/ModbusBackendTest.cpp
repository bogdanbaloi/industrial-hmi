// [utest->req~integration-003~1]
// Covers REQ-INTEGRATION-003 (Modbus TCP master).
//
// Tests for app::integration::modbus::ModbusBackend.
//
// Lifecycle-only coverage: the four collaborators are exercised by
// their own dedicated tests (ModbusClient, ModbusIngestBridge,
// ModbusPollLoop). This file confirms that the aggregate honours the
// IntegrationBackend contract:
//
//   * start() spawns the poll loop
//   * stop() joins it
//   * name() / metricsSummary() / connectionState() report sane
//     values across the lifecycle
//
// We inject a FakeModbusClient (a ModbusClient subclass that bypasses
// Boost.Asio) so no real socket is opened. The bridge + map + poll
// loop are real, since the lifecycle test value comes from running
// them.

#include "src/core/LoggerBase.h"
#include "src/core/LoggerImpl.h"
#include "src/integration/IntegrationBackend.h"
#include "src/integration/modbus/ModbusBackend.h"
#include "src/integration/modbus/ModbusClient.h"
#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusPollLoop.h"
#include "src/integration/modbus/ModbusRegisterMap.h"

#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

using app::integration::BackendState;
using app::integration::modbus::FieldKind;
using app::integration::modbus::ModbusBackend;
using app::integration::modbus::ModbusClient;
using app::integration::modbus::ModbusIngestBridge;
using app::integration::modbus::ModbusPollLoop;
using app::integration::modbus::ModbusRegisterMap;
using app::integration::modbus::RegisterMapping;
using app::integration::modbus::RegisterType;
using app::test::MockProductionModel;

namespace {

/// Helper: wrap the project's NullLogger (LoggerBase impl) into a
/// concrete Logger ready for backend constructors. No I/O, no config
/// dependency.
app::core::Logger makeNullLogger() {
    return app::core::Logger(std::make_unique<app::core::NullLogger>());
}

/// ModbusClient configured to point at a dead port. Connect attempts
/// fail fast; the test only cares that the poll loop runs the
/// dispatcher repeatedly without crashing, NOT that any read succeeds.
std::unique_ptr<ModbusClient> makeDeadClient() {
    ModbusClient::Config cfg;
    cfg.host           = "127.0.0.1";
    cfg.port           = 1;  // privileged + nothing listens
    cfg.connectTimeout = std::chrono::milliseconds{20};
    cfg.requestTimeout = std::chrono::milliseconds{20};
    return std::make_unique<ModbusClient>(std::move(cfg));
}

std::unique_ptr<ModbusRegisterMap> makeOneEntryMap() {
    auto map = std::make_unique<ModbusRegisterMap>();
    RegisterMapping m;
    m.slaveId  = 1;
    m.type     = RegisterType::HoldingRegister;
    m.address  = 0;
    m.field    = FieldKind::EquipmentEnabled;
    m.entityId = 0;
    map->add(m);
    return map;
}

}  // namespace

TEST(ModbusBackendTest, NameIsStable) {
    auto logger = makeNullLogger();
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    auto client   = makeDeadClient();
    auto map      = makeOneEntryMap();
    auto bridge   = std::make_unique<ModbusIngestBridge>(model);
    auto pollLoop = std::make_unique<ModbusPollLoop>(*client, *map, *bridge);

    ModbusBackend backend(std::move(client), std::move(map),
                          std::move(bridge), std::move(pollLoop),
                          logger);
    EXPECT_EQ(backend.name(), "Modbus");
}

TEST(ModbusBackendTest, NotRunningBeforeStart) {
    auto logger = makeNullLogger();
    MockProductionModel model;
    auto client   = makeDeadClient();
    auto map      = makeOneEntryMap();
    auto bridge   = std::make_unique<ModbusIngestBridge>(model);
    auto pollLoop = std::make_unique<ModbusPollLoop>(*client, *map, *bridge);

    ModbusBackend backend(std::move(client), std::move(map),
                          std::move(bridge), std::move(pollLoop),
                          logger);
    EXPECT_FALSE(backend.isRunning());
    EXPECT_EQ(backend.connectionState(), BackendState::Disconnected);
    EXPECT_EQ(backend.metricsSummary(), "");
}

TEST(ModbusBackendTest, StartStopCycleIsClean) {
    auto logger = makeNullLogger();
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());

    auto client = makeDeadClient();

    auto map      = makeOneEntryMap();
    auto bridge   = std::make_unique<ModbusIngestBridge>(model);

    ModbusPollLoop::Config pollConfig;
    pollConfig.pollInterval = std::chrono::milliseconds{20};
    auto pollLoop = std::make_unique<ModbusPollLoop>(*client, *map, *bridge,
                                                     pollConfig);

    ModbusBackend backend(std::move(client), std::move(map),
                          std::move(bridge), std::move(pollLoop),
                          logger);

    backend.start();
    EXPECT_TRUE(backend.isRunning());

    // Give the loop a few cycles -- all will fail because the client
    // can't reach port 1. We're testing the lifecycle, not success.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    backend.stop();
    EXPECT_FALSE(backend.isRunning());
    EXPECT_EQ(backend.connectionState(), BackendState::Disconnected);
}

TEST(ModbusBackendTest, StartIsIdempotent) {
    auto logger = makeNullLogger();
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    auto client   = makeDeadClient();
    auto map      = makeOneEntryMap();
    auto bridge   = std::make_unique<ModbusIngestBridge>(model);

    ModbusPollLoop::Config pollConfig;
    pollConfig.pollInterval = std::chrono::milliseconds{50};
    auto pollLoop = std::make_unique<ModbusPollLoop>(*client, *map, *bridge,
                                                     pollConfig);

    ModbusBackend backend(std::move(client), std::move(map),
                          std::move(bridge), std::move(pollLoop),
                          logger);
    backend.start();
    backend.start();  // second call must be a no-op
    EXPECT_TRUE(backend.isRunning());
    backend.stop();
}

TEST(ModbusBackendTest, MetricsSummaryReportsCountersWhileRunning) {
    auto logger = makeNullLogger();
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    auto client   = makeDeadClient();
    auto map      = makeOneEntryMap();
    auto bridge   = std::make_unique<ModbusIngestBridge>(model);

    ModbusPollLoop::Config pollConfig;
    pollConfig.pollInterval = std::chrono::milliseconds{20};
    auto pollLoop = std::make_unique<ModbusPollLoop>(*client, *map, *bridge,
                                                     pollConfig);

    ModbusBackend backend(std::move(client), std::move(map),
                          std::move(bridge), std::move(pollLoop),
                          logger);
    backend.start();
    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    const auto summary = backend.metricsSummary();
    backend.stop();

    // Format: "1 regs | N ok / M fail". We assert the prefix only --
    // exact counts depend on scheduling.
    EXPECT_NE(summary.find("1 regs"), std::string::npos)
        << "summary='" << summary << "'";
    EXPECT_NE(summary.find("fail"), std::string::npos)
        << "summary='" << summary << "'";
}

TEST(ModbusBackendTest, DestructorJoinsPollLoop) {
    auto logger = makeNullLogger();
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    {
        auto client   = makeDeadClient();
        auto map      = makeOneEntryMap();
        auto bridge   = std::make_unique<ModbusIngestBridge>(model);

        ModbusPollLoop::Config pollConfig;
        pollConfig.pollInterval = std::chrono::milliseconds{30};
        auto pollLoop = std::make_unique<ModbusPollLoop>(*client, *map,
                                                         *bridge, pollConfig);

        ModbusBackend backend(std::move(client), std::move(map),
                              std::move(bridge), std::move(pollLoop),
                              logger);
        backend.start();
        std::this_thread::sleep_for(std::chrono::milliseconds{40});
        // No explicit stop(); dtor must join. If it hangs, the test
        // never returns and CI times out.
    }
    SUCCEED();
}
