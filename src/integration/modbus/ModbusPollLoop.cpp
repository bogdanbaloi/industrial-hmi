#include "src/integration/modbus/ModbusPollLoop.h"

#include <chrono>
#include <utility>

namespace app::integration::modbus {

namespace {
// Backstop wake interval for the drain thread. The producer notifies
// after every push so this only fires if a notify is ever missed --
// the drain thread still makes progress within this bound.
constexpr std::chrono::milliseconds kDrainWakeInterval{50};
}  // namespace

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
    // Spawn the consumer first so it is ready to drain the moment the
    // producer starts pushing. Order is not strictly required (the
    // drain thread just waits on an empty queue) but it is the natural
    // producer-after-consumer setup.
    drainThread_ = std::jthread(
        [this](std::stop_token token) { drainRun(std::move(token)); });
    thread_ = std::jthread(
        [this](std::stop_token token) { run(std::move(token)); });
}

void ModbusPollLoop::stop() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    // Ordered shutdown to preserve the single-consumer invariant:
    //   1. Stop the PRODUCER first + join. No more pushes can happen.
    thread_.request_stop();
    sleepCv_.notify_all();
    thread_.join();
    //   2. Stop the CONSUMER + join. The drain thread is now the only
    //      thing that ever touched the queue's consumer side, and it is
    //      gone.
    if (drainThread_.joinable()) {
        drainThread_.request_stop();
        drainCv_.notify_all();
        drainThread_.join();
    }
    //   3. Single-threaded now: flush any samples the drain thread did
    //      not get to, so a clean shutdown loses nothing. Safe -- no
    //      other consumer exists at this point.
    drainOnce();
}

bool ModbusPollLoop::isRunning() const noexcept {
    // Poll-thread liveness is the public contract; the drain thread is
    // an internal implementation detail started/stopped alongside it.
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
        // PRODUCE: push the sample for the drain thread to dispatch.
        // Drop on overflow (REQ-ARCH-010) -- the poll thread must never
        // block on a full queue; the next walk refreshes the value.
        if (!queue_.push(RegisterSample{entry, result.unwrap().front()})) {
            droppedSamples_.fetch_add(1, std::memory_order_release);
        }
    }
    // Wake the drain thread once per walk (cheap; the consumer also
    // has a timeout backstop). Notify without holding drainMutex_ --
    // the consumer re-checks the queue under the lock, so this can
    // only ever cause one harmless extra wake, never a missed sample.
    drainCv_.notify_one();
}

void ModbusPollLoop::drainOnce() {
    // CONSUME: dispatch every queued sample to the bridge. Called by
    // the drain thread, and by stop()/tests on a single thread.
    RegisterSample sample;
    while (queue_.pop(sample)) {
        bridge_.onRegisterChanged(sample.mapping, sample.value);
    }
}

void ModbusPollLoop::drainRun(std::stop_token token) {
    while (!token.stop_requested()) {
        drainOnce();
        if (token.stop_requested()) {
            break;
        }
        // Sleep until the producer pushes (notify) or the backstop
        // timeout fires; wake immediately on stop. Predicate also wakes
        // if the queue is non-empty so a push that raced the wait is
        // not lost.
        std::unique_lock<std::mutex> lock(drainMutex_);
        drainCv_.wait_for(lock, token, kDrainWakeInterval,
                          [this, &token]() {
                              return token.stop_requested() ||
                                     !queue_.empty();
                          });
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
