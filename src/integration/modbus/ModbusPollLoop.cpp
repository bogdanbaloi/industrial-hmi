#include "src/integration/modbus/ModbusPollLoop.h"

#include <utility>

namespace app::integration::modbus {

ModbusPollLoop::ModbusPollLoop(ModbusReader& reader,
                               const ModbusRegisterMap& map,
                               ModbusIngestBridge& bridge)
    : ModbusPollLoop(reader, map, bridge, Config{}) {}

ModbusPollLoop::ModbusPollLoop(ModbusReader& reader,
                               const ModbusRegisterMap& map,
                               ModbusIngestBridge& bridge,
                               Config config)
    : reader_(reader),
      map_(map),
      bridge_(bridge),
      config_(config) {}

ModbusPollLoop::~ModbusPollLoop() {
    stop();
}

void ModbusPollLoop::start() {
    if (thread_.joinable()) {
        return;  // already running -- idempotent on purpose
    }
    thread_ = std::jthread(
        [this](std::stop_token token) { run(std::move(token)); });
}

void ModbusPollLoop::stop() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    thread_.request_stop();
    // Kick the worker out of its interruptible wait promptly. The cv
    // is shared so notify_all is cheap and avoids the dance of
    // locking the same mutex the worker holds while running.
    sleepCv_.notify_all();
    thread_.join();
}

bool ModbusPollLoop::isRunning() const noexcept {
    return thread_.joinable();
}

void ModbusPollLoop::pollOnce() {
    using enum FieldKind;
    using enum RegisterType;

    for (const auto& entry : map_.entries()) {
        // Dispatch on register type to pick FC03 vs FC04. Single
        // register reads per entry -- batching adjacent addresses is
        // a future optimisation; the spec lets us read up to 125 in
        // one round-trip but the map is small in practice and the
        // simpler one-by-one path makes per-entry failure handling
        // trivial.
        auto result = (entry.type == HoldingRegister)
            ? reader_.readHoldingRegisters(entry.slaveId, entry.address, 1)
            : reader_.readInputRegisters(entry.slaveId, entry.address, 1);

        if (result.isErr() || result.unwrap().empty()) {
            failedReads_.fetch_add(1, std::memory_order_release);
            continue;  // skip this entry, keep polling the rest
        }
        successfulReads_.fetch_add(1, std::memory_order_release);
        bridge_.onRegisterChanged(entry, result.unwrap().front());
    }
}

void ModbusPollLoop::run(std::stop_token token) {
    while (!token.stop_requested()) {
        pollOnce();
        if (token.stop_requested()) {
            break;
        }
        // Interruptible sleep: wait_for returns early when the
        // stop_token fires, so stop() doesn't pay the full
        // pollInterval. The predicate is "stop requested?", so the
        // cv is only re-checked when notify_all wakes us.
        std::unique_lock<std::mutex> lock(sleepMutex_);
        sleepCv_.wait_for(lock, token, config_.pollInterval,
                          [&token]() { return token.stop_requested(); });
    }
}

}  // namespace app::integration::modbus
