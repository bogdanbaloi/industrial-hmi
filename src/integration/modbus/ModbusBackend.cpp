#include "src/integration/modbus/ModbusBackend.h"

#include "src/core/LoggerBase.h"

#include <cassert>
#include <format>
#include <utility>

namespace app::integration::modbus {

ModbusBackend::ModbusBackend(
        std::unique_ptr<ModbusClient>       client,
        std::unique_ptr<ModbusRegisterMap>  map,
        std::unique_ptr<ModbusIngestBridge> bridge,
        std::unique_ptr<ModbusPollLoop>     pollLoop,
        core::Logger& logger)
    : client_(std::move(client)),
      map_(std::move(map)),
      bridge_(std::move(bridge)),
      pollLoop_(std::move(pollLoop)),
      logger_(logger) {
    // Construction-time invariants. Throwing here surfaces wiring
    // bugs before the manager calls start() and gets a confusing
    // failure deep inside the jthread.
    assert(client_   && "ModbusBackend: client must be non-null");
    assert(map_      && "ModbusBackend: map must be non-null");
    assert(bridge_   && "ModbusBackend: bridge must be non-null");
    assert(pollLoop_ && "ModbusBackend: pollLoop must be non-null");
}

ModbusBackend::~ModbusBackend() {
    // RAII teardown: stop() is idempotent and noexcept downstream.
    // Calling it explicitly here gives the log line + symmetry with
    // every other backend; the unique_ptrs would still join cleanly.
    ModbusBackend::stop();
}

void ModbusBackend::start() {
    if (pollLoop_->isRunning()) {
        return;  // idempotent
    }
    logger_.info("Modbus backend starting");
    pollLoop_->start();
    logger_.info("Modbus backend started");
}

void ModbusBackend::stop() {
    if (!pollLoop_->isRunning()) {
        return;  // idempotent
    }
    logger_.info("Modbus backend stopping");
    pollLoop_->stop();
    // Client socket closes via ~ModbusClient at backend dtor; an
    // explicit close here is unnecessary because the loop is the
    // only caller of client_ methods and it's now joined.
    logger_.info("Modbus backend stopped");
}

bool ModbusBackend::isRunning() const {
    return pollLoop_->isRunning();
}

std::string ModbusBackend::name() const {
    return "Modbus";
}

std::string ModbusBackend::metricsSummary() const {
    if (!pollLoop_->isRunning()) return {};
    return std::format("{} regs | {} ok / {} fail",
                       map_->size(),
                       pollLoop_->successfulReads(),
                       pollLoop_->failedReads());
}

BackendState ModbusBackend::connectionState() const noexcept {
    if (!pollLoop_->isRunning()) {
        return BackendState::Disconnected;
    }
    // Empty register map: jthread is spinning idle. Treat as
    // Connecting so the I/O panel doesn't claim a healthy green dot
    // when no actual transport activity is happening.
    if (map_->empty()) {
        return BackendState::Connecting;
    }
    // Counters drive the rest. Note successfulReads is monotonic --
    // once any poll succeeds we never demote to Connecting again
    // (that would flicker the pill on every transient slave hiccup).
    // A long run of failures while successfulReads > 0 maps to
    // Degraded; a clean run keeps Connected.
    const std::uint64_t ok = pollLoop_->successfulReads();
    if (ok == 0) {
        // Loop is up but every poll so far has failed (or none have
        // happened yet) -- slave unreachable. Connecting fits better
        // than Degraded; we've never reached Connected to be
        // degraded *from*. failedReads() distinguishes "no attempt
        // yet" from "all attempts failed", but the I/O panel doesn't
        // surface that distinction today, so both map to one pill.
        return BackendState::Connecting;
    }
    // Have at least one success. If client is currently disconnected
    // (last attempt failed), surface Degraded; otherwise Connected.
    return client_->isConnected() ? BackendState::Connected
                                  : BackendState::Degraded;
}

}  // namespace app::integration::modbus
