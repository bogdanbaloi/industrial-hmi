// [utest->req~arch-010~1]
// Tests for app::integration::modbus::ModbusPollLoop.
//
// REQ-ARCH-010 (cross-thread SPSC seam): pollOnce() now PRODUCES into a
// lock-free queue and drainOnce() CONSUMES into the bridge, so the
// single-cycle tests below call pollOnce() then drainOnce() to complete
// the round-trip synchronously on one thread.
//
// Strategy: a FakeModbusReader implements the ModbusReader interface
// in memory -- the test pre-programs register values per (slaveId,
// type, address), and the loop's pollOnce() walks the register map
// and dispatches into a MockProductionModel through the real bridge.
//
// pollOnce() is exercised on the calling thread for deterministic
// assertions. A single threaded test confirms start/stop with a
// short poll interval actually fires multiple cycles and joins
// cleanly. No sockets, no jthread races to chase.

#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusPollLoop.h"
#include "src/integration/modbus/ModbusReader.h"
#include "src/integration/modbus/ModbusRegisterMap.h"

#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

using app::integration::modbus::ExceptionCode;
using app::integration::modbus::FieldKind;
using app::integration::modbus::ModbusIngestBridge;
using app::integration::modbus::ModbusPollLoop;
using app::integration::modbus::ModbusReader;
using app::integration::modbus::ModbusRegisterMap;
using app::integration::modbus::RegisterMapping;
using app::integration::modbus::RegisterType;
using app::test::MockProductionModel;

namespace {

namespace core = app::core;

/// In-memory ModbusReader. Tests program register values by
/// (slaveId, type, address) and the loop's reads pull them out.
/// Missing keys return Err(ConnectionFailed); flipping `connected_`
/// off makes every read fail with Disconnected so timeout / retry
/// paths can be exercised without sockets.
class FakeModbusReader final : public ModbusReader {
public:
    using Key = std::tuple<std::uint8_t, RegisterType, std::uint16_t>;

    void program(std::uint8_t slaveId, RegisterType type,
                 std::uint16_t address, std::uint16_t value) {
        const std::scoped_lock lock(mutex_);
        values_[{slaveId, type, address}] = value;
    }

    void disconnect() noexcept {
        connected_.store(false, std::memory_order_release);
    }

    void reconnect() noexcept {
        connected_.store(true, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t totalReads() const noexcept {
        return totalReads_.load(std::memory_order_acquire);
    }

    core::Result<std::vector<std::uint16_t>, IoError>
    readHoldingRegisters(std::uint8_t unitId, std::uint16_t address,
                         std::uint16_t quantity) override {
        return readImpl(unitId, RegisterType::HoldingRegister, address,
                        quantity);
    }

    core::Result<std::vector<std::uint16_t>, IoError>
    readInputRegisters(std::uint8_t unitId, std::uint16_t address,
                       std::uint16_t quantity) override {
        return readImpl(unitId, RegisterType::InputRegister, address,
                        quantity);
    }

    [[nodiscard]] bool isConnected() const noexcept override {
        return connected_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<ExceptionCode>
    lastExceptionCode() const noexcept override {
        return std::nullopt;
    }

private:
    core::Result<std::vector<std::uint16_t>, IoError>
    readImpl(std::uint8_t unitId, RegisterType type,
             std::uint16_t address, std::uint16_t quantity) {
        using Res = core::Result<std::vector<std::uint16_t>, IoError>;
        totalReads_.fetch_add(1, std::memory_order_release);
        if (!connected_.load(std::memory_order_acquire)) {
            return Res(core::Err, IoError::Disconnected);
        }
        const std::scoped_lock lock(mutex_);
        const auto it = values_.find({unitId, type, address});
        if (it == values_.end()) {
            return Res(core::Err, IoError::ConnectionFailed);
        }
        // Loop only ever asks for quantity=1; mirror that contract.
        std::vector<std::uint16_t> regs;
        regs.reserve(quantity);
        regs.push_back(it->second);
        return Res(core::Ok, std::move(regs));
    }

    mutable std::mutex mutex_;
    std::map<Key, std::uint16_t> values_;
    std::atomic<bool> connected_{true};
    std::atomic<std::uint64_t> totalReads_{0};
};

RegisterMapping equipmentEnabledMapping(std::uint32_t entityId,
                                        std::uint8_t slaveId = 1,
                                        std::uint16_t address = 0,
                                        RegisterType type
                                            = RegisterType::HoldingRegister) {
    RegisterMapping m;
    m.slaveId  = slaveId;
    m.type     = type;
    m.address  = address;
    m.field    = FieldKind::EquipmentEnabled;
    m.entityId = entityId;
    return m;
}

}  // namespace

// ===== pollOnce() — single deterministic cycle ===================

TEST(ModbusPollLoopTest, PollOnceDispatchesEveryMappingToBridge) {
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);  // ON
    reader.program(1, RegisterType::HoldingRegister, 0x0200, 0);  // OFF
    reader.program(1, RegisterType::InputRegister,   0x0300, 7);  // ON

    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100,
                                    RegisterType::HoldingRegister));
    map.add(equipmentEnabledMapping(1, 1, 0x0200,
                                    RegisterType::HoldingRegister));
    map.add(equipmentEnabledMapping(2, 1, 0x0300,
                                    RegisterType::InputRegister));

    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(1U, false)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(2U, true)).Times(1);

    ModbusPollLoop loop(reader, map, bridge);
    loop.pollOnce();   // produce
    loop.drainOnce();  // consume -> dispatch to bridge/model

    EXPECT_EQ(loop.successfulReads(), 3U);
    EXPECT_EQ(loop.failedReads(),     0U);
}

TEST(ModbusPollLoopTest, PollOnceUsesFc03ForHoldingFc04ForInput) {
    // Programming a value under one (slaveId, type, address) but
    // mapping the other type catches a mis-dispatch: the loop must
    // hit the same key the test programmed.
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);
    // No entry under InputRegister at the same address.

    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100,
                                    RegisterType::InputRegister));

    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_)).Times(0);

    ModbusPollLoop loop(reader, map, bridge);
    loop.pollOnce();
    loop.drainOnce();

    EXPECT_EQ(loop.successfulReads(), 0U);
    EXPECT_EQ(loop.failedReads(),     1U);
}

TEST(ModbusPollLoopTest, FailedReadDoesNotStopRemainingEntries) {
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);
    // 0x0200 unprogrammed -> ConnectionFailed
    reader.program(1, RegisterType::HoldingRegister, 0x0300, 1);

    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100));
    map.add(equipmentEnabledMapping(1, 1, 0x0200));  // will fail
    map.add(equipmentEnabledMapping(2, 1, 0x0300));

    // Entity 1's read fails, but 0 and 2 still get propagated.
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(2U, true)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(1U, testing::_)).Times(0);

    ModbusPollLoop loop(reader, map, bridge);
    loop.pollOnce();
    loop.drainOnce();

    EXPECT_EQ(loop.successfulReads(), 2U);
    EXPECT_EQ(loop.failedReads(),     1U);
}

TEST(ModbusPollLoopTest, DisconnectedReaderFailsAllReadsButKeepsLoopAlive) {
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);
    reader.disconnect();

    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100));
    map.add(equipmentEnabledMapping(1, 1, 0x0200));

    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_)).Times(0);

    ModbusPollLoop loop(reader, map, bridge);
    loop.pollOnce();
    loop.drainOnce();
    EXPECT_EQ(loop.failedReads(), 2U);

    // Reconnect + reprogram: same loop, next pollOnce sees the
    // freshly reachable register.
    reader.reconnect();
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    loop.pollOnce();
    loop.drainOnce();
    EXPECT_EQ(loop.successfulReads(), 1U);
}

TEST(ModbusPollLoopTest, EmptyMapIsAValidNoOp) {
    FakeModbusReader reader;
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap emptyMap;

    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_)).Times(0);
    ModbusPollLoop loop(reader, emptyMap, bridge);
    loop.pollOnce();
    loop.drainOnce();
    EXPECT_EQ(loop.successfulReads(), 0U);
    EXPECT_EQ(loop.failedReads(),     0U);
}

// ===== start() / stop() — threading sanity =======================

TEST(ModbusPollLoopTest, StartStopFiresAtLeastOneCycleAndJoinsCleanly) {
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);

    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(testing::AtLeast(1));
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100));

    ModbusPollLoop::Config cfg;
    cfg.pollInterval = std::chrono::milliseconds{10};
    ModbusPollLoop loop(reader, map, bridge, cfg);
    EXPECT_FALSE(loop.isRunning());

    loop.start();
    EXPECT_TRUE(loop.isRunning());

    // Let the worker burn a few cycles -- enough to observe the
    // counter ticking, short enough not to slow CI.
    std::this_thread::sleep_for(std::chrono::milliseconds{60});

    loop.stop();
    EXPECT_FALSE(loop.isRunning());
    EXPECT_GE(loop.successfulReads(), 1U);
    EXPECT_GE(reader.totalReads(),    1U);
}

TEST(ModbusPollLoopTest, StopReturnsPromptlyEvenWithLongPollInterval) {
    // 10s interval; the loop is sleeping for 99% of its life. stop()
    // must wake the cv quickly, not wait the full interval.
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);

    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100));

    ModbusPollLoop::Config cfg;
    cfg.pollInterval = std::chrono::seconds{10};
    ModbusPollLoop loop(reader, map, bridge, cfg);
    loop.start();

    // Give the worker a chance to enter its first sleep.
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    const auto before = std::chrono::steady_clock::now();
    loop.stop();
    const auto elapsed = std::chrono::steady_clock::now() - before;

    // Cooperative cancellation budget: anything well under the full
    // 10s interval. Generous bound (500ms) keeps the assertion
    // resilient on heavily-loaded CI runners.
    EXPECT_LT(elapsed, std::chrono::milliseconds{500})
        << "stop() did not honour the stop_token within budget";
}

TEST(ModbusPollLoopTest, StartIsIdempotent) {
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100));

    ModbusPollLoop::Config cfg;
    cfg.pollInterval = std::chrono::milliseconds{20};
    ModbusPollLoop loop(reader, map, bridge, cfg);
    loop.start();
    loop.start();  // no-op; must not spawn a second thread
    EXPECT_TRUE(loop.isRunning());
    loop.stop();
    EXPECT_FALSE(loop.isRunning());
}

// ===== SPSC cross-thread seam (REQ-ARCH-010) ====================

TEST(ModbusPollLoopTest, DrainOnceIsNoOpWhenQueueEmpty) {
    FakeModbusReader reader;
    MockProductionModel model;
    // No pollOnce() -> nothing queued -> drainOnce() must dispatch
    // nothing and not crash.
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_)).Times(0);
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;

    ModbusPollLoop loop(reader, map, bridge);
    loop.drainOnce();  // empty pop loop -> no-op

    EXPECT_EQ(loop.successfulReads(), 0U);
    EXPECT_EQ(loop.droppedSamples(), 0U);
}

TEST(ModbusPollLoopTest, PollOncePushesDroppedSamplesOnQueueFull) {
    // Produce more samples in a single walk than the SPSC queue can
    // hold WITHOUT draining, so the overflow path increments the drop
    // counter. The queue's usable depth is kQueueCapacity - 1.
    FakeModbusReader reader;
    MockProductionModel model;
    // Allow any dispatch (none happens -- we never drainOnce) but don't
    // require it.
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    ModbusIngestBridge bridge(model);

    ModbusRegisterMap map;
    constexpr std::uint16_t kEntries =
        static_cast<std::uint16_t>(ModbusPollLoop::kQueueCapacity + 4);
    for (std::uint16_t i = 0; i < kEntries; ++i) {
        reader.program(1, RegisterType::HoldingRegister, i, 1);
        map.add(equipmentEnabledMapping(i, 1, i,
                                        RegisterType::HoldingRegister));
    }

    ModbusPollLoop loop(reader, map, bridge);
    loop.pollOnce();  // produce only -- no drain, so the queue fills

    EXPECT_EQ(loop.successfulReads(), kEntries)
        << "every read succeeded regardless of queue space";
    EXPECT_GE(loop.droppedSamples(), 1U)
        << "samples beyond the queue depth must be dropped, not block";
}

TEST(ModbusPollLoopTest, DestructorJoinsRunningThread) {
    FakeModbusReader reader;
    reader.program(1, RegisterType::HoldingRegister, 0x0100, 1);
    MockProductionModel model;
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_))
        .Times(testing::AnyNumber());
    ModbusIngestBridge bridge(model);
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 1, 0x0100));

    {
        ModbusPollLoop::Config cfg;
        cfg.pollInterval = std::chrono::milliseconds{15};
        ModbusPollLoop loop(reader, map, bridge, cfg);
        loop.start();
        std::this_thread::sleep_for(std::chrono::milliseconds{30});
        // No explicit stop(); rely on the destructor.
    }
    // If we got here without hanging, the dtor joined cleanly. The
    // explicit assertion is just a marker.
    SUCCEED();
}
