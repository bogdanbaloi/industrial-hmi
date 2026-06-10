#pragma once

#include "src/core/SpscQueue.h"
#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusReader.h"
#include "src/integration/modbus/ModbusRegisterMap.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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
/// Threading (REQ-ARCH-010, ADR-0018): a lock-free SPSC queue
/// decouples the poll thread from bridge/model dispatch.
///   * `start()` launches TWO std::jthreads: the POLL thread (produces
///     register samples) and the DRAIN thread (consumes them and calls
///     the bridge). Exactly one producer, exactly one consumer -- the
///     SPSC contract `SpscQueue` requires.
///   * The poll thread does blocking socket I/O, then PUSHES samples
///     into `queue_` and returns immediately to the wire; it never
///     blocks on bridge/model lock-hold time. On queue overflow the
///     sample is dropped (`droppedSamples_` counts it) -- the next poll
///     refreshes the value, so a drop is staler data, never corruption.
///   * The drain thread is the ONLY caller of `bridge_` during normal
///     operation, so the bridge sees a single consumer.
///   * Both threads sleep on a `condition_variable_any` keyed to their
///     stop_token so `stop()` returns in ms. The poll thread wakes the
///     drain thread after each push; a short timeout backstops any
///     missed notify.
///   * `pollOnce()` (produce) and `drainOnce()` (consume) are exposed
///     so tests can drive a full round-trip synchronously on one
///     thread without spawning the workers.
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
    /// Default cadence: one walk per second. Matches typical PLC
    /// poll rates and the dashboard refresh interval.
    static constexpr std::chrono::milliseconds kDefaultPollInterval =
        std::chrono::seconds{1};

    struct Config {
        std::chrono::milliseconds pollInterval{kDefaultPollInterval};
    };

    /// SPSC queue depth (REQ-ARCH-010). Power of two for the mask;
    /// 256 absorbs a full register-map walk plus a few bursts before
    /// the drain thread catches up. Public so tests can exercise the
    /// overflow path without a magic literal.
    static constexpr std::size_t kQueueCapacity = 256;

    /// Value pushed across the SPSC seam: a register mapping plus the
    /// raw value just read. Trivially copyable so the queue element is
    /// nothrow-constructible.
    struct RegisterSample {
        RegisterMapping mapping{};
        std::uint16_t   value{0};
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

    /// PRODUCE one walk of the register map synchronously on the
    /// calling thread: read each entry and PUSH the successful results
    /// into the SPSC queue (REQ-ARCH-010). Does NOT dispatch to the
    /// bridge -- `drainOnce()` does. On the poll thread this is the
    /// producer; tests call `pollOnce()` then `drainOnce()` for a full
    /// synchronous round-trip.
    void pollOnce();

    /// CONSUME: pop every queued sample and dispatch it to the bridge.
    /// On the drain thread this is the consumer; tests call it right
    /// after `pollOnce()` to complete the round-trip without spawning
    /// the workers.
    void drainOnce();

    [[nodiscard]] std::uint64_t successfulReads() const noexcept {
        return successfulReads_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t failedReads() const noexcept {
        return failedReads_.load(std::memory_order_acquire);
    }
    /// Count of samples dropped because the SPSC queue was full
    /// (REQ-ARCH-010). Non-zero means the drain thread fell behind the
    /// poll thread; surfaced in ModbusBackend::metricsSummary().
    [[nodiscard]] std::uint64_t droppedSamples() const noexcept {
        return droppedSamples_.load(std::memory_order_acquire);
    }

private:
    /// Body of the POLL thread: alternates pollOnce() (produce) with an
    /// interruptible wait on the configured interval.
    void run(std::stop_token token);

    /// Body of the DRAIN thread: drainOnce() (consume) then sleep on
    /// drainCv_ until the producer wakes it or a short timeout fires.
    void drainRun(std::stop_token token);

    // Member order is chosen to minimise struct padding: the SPSC queue
    // is 64-byte aligned internally (false-sharing avoidance), so it
    // leads, then the smaller members pack after it. Functionally the
    // grouping is: queue -> injected deps -> counters -> producer
    // thread + its cv -> consumer thread + its cv. (clang-analyzer
    // optin.performance.Padding is a CI error if this is reordered
    // sub-optimally.)
    //
    // SPSC seam (REQ-ARCH-010): poll thread produces, drain thread
    // consumes. Lock-free; no mutex on the hot path.
    core::SpscQueue<RegisterSample, kQueueCapacity> queue_;

    ModbusReader&            reader_;
    const ModbusRegisterMap& map_;
    ModbusIngestBridge&      bridge_;
    Config                   config_;

    std::atomic<std::uint64_t> successfulReads_{0};
    std::atomic<std::uint64_t> failedReads_{0};
    std::atomic<std::uint64_t> droppedSamples_{0};

    std::jthread             thread_;        // producer (poll)
    std::jthread             drainThread_;   // consumer (drain)
    mutable std::mutex       sleepMutex_;
    std::mutex               drainMutex_;
    std::condition_variable_any sleepCv_;
    std::condition_variable_any drainCv_;
};

}  // namespace app::integration::modbus
