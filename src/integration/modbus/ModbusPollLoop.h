#pragma once

#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusReader.h"
#include "src/integration/modbus/ModbusRegisterMap.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace app::integration::modbus {

/// Cadence driver: on a worker thread, walks the register map every
/// `pollInterval`, reads each entry through a ModbusReader, and
/// dispatches successful reads to a ModbusIngestBridge.
///
/// The poll loop is the only thing in the Modbus stack that owns a
/// thread. ModbusClient is synchronous; the bridge is pure dispatch;
/// the register map is immutable data. Containing the threading
/// concern here keeps every other component testable on the calling
/// thread.
///
/// SOLID:
///   * S -- one job: drive cadence. Picks no targets, decodes nothing,
///     mutates no domain state directly.
///   * O -- new register types / function codes land in the dispatch
///     switch; pollOnce()'s shape stays stable.
///   * L -- depends on ModbusReader& (interface), not ModbusClient.
///     Tests substitute a FakeModbusReader with programmed responses.
///   * I -- exposes start/stop + a public pollOnce() for unit tests.
///     The public tick keeps the threading-vs-logic split visible.
///   * D -- everything is injected: reader, map, bridge. No
///     singletons, no globals.
///
/// Threading:
///   * `start()` launches a single std::jthread.
///   * The thread sleeps on a condition_variable_any keyed to the
///     jthread's stop_token, so `stop()` returns within a couple of
///     ms rather than the full pollInterval.
///   * `pollOnce()` is reentrant and may be called from any thread
///     (the bridge's dedup state and the reader's internal mutex are
///     the only shared state; both already serialise).
///
/// Failure handling:
///   * Per-entry failures (ConnectionFailed, Timeout, ServerException
///     ...) increment failedReads_ and continue the iteration. A
///     bad slave does not stop the loop reading good ones.
///   * Successful reads increment successfulReads_ and pass through
///     the bridge.
///   * Both counters are surfaced for the I/O panel tooltip
///     ("123 ok / 4 fail | last poll Xs ago").
class ModbusPollLoop {
public:
    struct Config {
        /// How long between two consecutive walks of the register
        /// map. 1s default matches typical PLC poll cadences and the
        /// dashboard refresh rate.
        std::chrono::milliseconds pollInterval{1000};
    };

    // Two overloads instead of a default Config{} -- gcc rejects the
    // default member initialisers inside Config when used as a
    // default function argument here (the class body isn't fully
    // parsed yet at the point of the declaration). Same workaround
    // as SensorIngestBridge.
    ModbusPollLoop(ModbusReader& reader,
                   const ModbusRegisterMap& map,
                   ModbusIngestBridge& bridge);
    ModbusPollLoop(ModbusReader& reader,
                   const ModbusRegisterMap& map,
                   ModbusIngestBridge& bridge,
                   Config config);

    ~ModbusPollLoop();

    ModbusPollLoop(const ModbusPollLoop&) = delete;
    ModbusPollLoop& operator=(const ModbusPollLoop&) = delete;
    ModbusPollLoop(ModbusPollLoop&&) = delete;
    ModbusPollLoop& operator=(ModbusPollLoop&&) = delete;

    /// Spawn the worker jthread. Idempotent: calling twice in a row
    /// without an intervening stop() is a no-op (already running).
    void start();

    /// Request stop + join. Idempotent: safe to call from the dtor
    /// after a manual stop().
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;

    /// Execute exactly one walk of the register map synchronously on
    /// the calling thread. Production code never calls this -- the
    /// worker thread does. Tests use it to assert dispatch logic
    /// without the timing variability of a real sleep.
    void pollOnce();

    [[nodiscard]] std::uint64_t successfulReads() const noexcept {
        return successfulReads_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t failedReads() const noexcept {
        return failedReads_.load(std::memory_order_acquire);
    }

private:
    /// Body of the worker thread: alternates pollOnce() with an
    /// interruptible wait on the configured interval.
    void run(std::stop_token token);

    ModbusReader&            reader_;
    const ModbusRegisterMap& map_;
    ModbusIngestBridge&      bridge_;
    Config                   config_;

    std::jthread             thread_;
    mutable std::mutex       sleepMutex_;
    std::condition_variable_any sleepCv_;

    std::atomic<std::uint64_t> successfulReads_{0};
    std::atomic<std::uint64_t> failedReads_{0};
};

}  // namespace app::integration::modbus
