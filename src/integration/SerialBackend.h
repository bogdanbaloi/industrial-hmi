#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/integration/SerialFrameParser.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace app::integration {

/// Holds the Boost.Asio state (io_context, serial_port, read buffer). It
/// is an opaque pimpl so this header never pulls in <boost/asio.hpp>:
/// serial_port is a typedef of a class template and cannot be forward-
/// declared cleanly, so the whole Asio surface lives in the .cpp.
struct SerialIo;

/// Backend for a serial link to a microcontroller on a USB virtual COM
/// port. It reads `sensorId,value` telemetry and routes each decoded
/// reading to an injected sink (REQ-INTEGRATION-009, ADR-0029). It also
/// transmits raw bytes to the device through `send()`, the path the OTA
/// flash protocol needs (REQ-INTEGRATION-012, ADR-0032).
///
/// SOLID / threading:
///   * S -- owns the lifecycle of one serial link and nothing else. The
///     framing is delegated to `SerialFrameParser`; interpreting a value
///     and mutating the model is the injected sink's job.
///   * O -- a sixth `IntegrationBackend` subclass; the manager and the
///     other backends stay untouched.
///   * D -- depends on an injected `ReadingSink` (a std::function), not
///     on the model singletons, so a test drives it with a capturing
///     sink and an emulated serial endpoint (a PTY pair) -- no hardware.
///
///   * Owns its own `boost::asio::io_context` + `std::jthread`. The read
///     loop, the parser and the sink all run on that one io_context
///     thread, so nothing here is touched concurrently and no lock is
///     needed (single-threaded confinement). The sink is responsible for
///     marshalling onto the UI thread when it mutates the model, exactly
///     as the other inbound paths do.
///   * `send()` is the one entry point callable from other threads. It
///     posts the bytes onto the io_context, so the write queue is also
///     confined to that thread. `ioMutex_` guards only the `io_` pointer,
///     which start() and stop() replace.
///   * I -- `send()` lives on this class, not on `IntegrationBackend`.
///     The other backends have no raw byte channel to offer.
class SerialBackend : public IntegrationBackend {
public:
    /// Called on the io_context thread for each decoded reading. The
    /// wiring maps `sensorId` to a model mutation (reusing the existing
    /// sensor-ingest helpers); the backend itself stays model-agnostic.
    using ReadingSink = std::function<void(const SerialReading&)>;

    /// @param device    Serial port device name ("/dev/ttyACM0" on Linux,
    ///                  "COM3" on Windows). Opened by `start()`.
    /// @param baudRate  Line speed both ends agree on (e.g. 115200).
    /// @param sink      Receives every decoded reading. Must be non-empty
    ///                  and must outlive the backend.
    SerialBackend(std::string device, unsigned baudRate, ReadingSink sink);

    ~SerialBackend() override;

    /// Open the port and launch the io_context thread with the first read
    /// armed. Non-blocking. Throws if the port cannot be opened or
    /// configured (device missing, permission denied, bad baud rate).
    void start() override;

    /// Cancel the port, stop the io_context and join the thread. Blocking,
    /// idempotent, safe from the destructor.
    void stop() override;

    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string name() const override { return "Serial"; }

    /// "/dev/ttyACM0 @ 115200" -- the device and baud, for the dashboard
    /// health tooltip.
    [[nodiscard]] std::string metricsSummary() const override;

    /// Queue bytes for transmission to the device. Callable from any
    /// thread. The bytes are copied, so the caller's buffer may go away
    /// as soon as this returns. Writes go out one at a time, in the order
    /// send() was called. The bytes are sent exactly as given: no
    /// framing, no line-ending translation.
    ///
    /// Accepted is not delivered. Bytes still queued when stop() runs are
    /// dropped. A failed write drops whatever was queued behind it.
    /// A caller that needs delivery waits for the device's answer, which
    /// is what the stop-and-wait flash protocol does.
    ///
    /// @param bytes  The bytes to send. An empty span is accepted and
    ///               sends nothing.
    /// @return true if the bytes were queued, false if the backend is not
    ///         running.
    [[nodiscard]] bool send(std::span<const std::byte> bytes);

private:
    /// Non-virtual stop body shared by stop() and the destructor (a
    /// destructor cannot safely call a virtual).
    void stopImpl() noexcept;

    /// Schedule the next `async_read_some`. Its completion handler feeds
    /// the bytes to the parser, emits each reading to the sink, then calls
    /// armRead() again. That re-arm is the read loop: it returns to the
    /// io_context between reads, so the call chain never nests and the
    /// stack stays flat.
    void armRead();

    /// Start `async_write` for the front of the write queue. Its handler
    /// pops that entry and calls writeNext() again while the queue is not
    /// empty. Asio allows only one outstanding write per port, so this
    /// chain is what keeps writes from interleaving. Runs only on the
    /// io_context thread.
    void writeNext();

    std::string device_;
    unsigned    baudRate_;
    ReadingSink sink_;

    /// Framing state. Touched only by the io_context thread.
    SerialFrameParser parser_;

    /// Opaque Boost.Asio state (io_context + serial_port + read buffer +
    /// write queue).
    std::unique_ptr<SerialIo> io_;
    /// Guards the `io_` pointer between send() on a caller's thread and
    /// the replacement of `io_` in start() and stop(). Not held while any
    /// I/O runs.
    std::mutex                ioMutex_;
    std::jthread              thread_;
    std::atomic<bool>         running_{false};
};

}  // namespace app::integration
