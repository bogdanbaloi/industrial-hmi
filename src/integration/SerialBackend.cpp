#include "src/integration/SerialBackend.h"

#include <boost/asio.hpp>

// Boost.Asio on Windows pulls in <windows.h>, which #defines ERROR. Nothing
// here needs that macro; undef it to keep the token clean (same guard the
// TCP backend uses).
#ifdef ERROR
#  undef ERROR
#endif

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace app::integration {

namespace asio = boost::asio;

namespace {
/// Bytes one async_read_some may deliver. The parser reassembles across
/// chunks, so this bounds a single read, not the frame size.
constexpr std::size_t kReadBufferBytes = 256;
/// Serial line format: 8 data bits, no parity, one stop bit, no flow
/// control -- the 8N1 default a bench microcontroller uses.
constexpr unsigned kCharacterSizeBits = 8;
}  // namespace

/// Boost.Asio state. Defined in the .cpp so <boost/asio.hpp> stays out of
/// the header, and because serial_port is a typedef of a class template
/// that cannot be forward-declared cleanly.
struct SerialIo {
    asio::io_context                   context;
    std::unique_ptr<asio::serial_port> port;
    std::array<char, kReadBufferBytes> buffer{};
};

SerialBackend::SerialBackend(std::string device, unsigned baudRate,
                             ReadingSink sink)
    : device_(std::move(device)),
      baudRate_(baudRate),
      sink_(std::move(sink)),
      io_(std::make_unique<SerialIo>()) {}

SerialBackend::~SerialBackend() {
    // Non-virtual stopImpl(): a destructor must not call the virtual
    // stop() (clang-analyzer-optin.cplusplus.VirtualCall).
    stopImpl();
}

void SerialBackend::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running, idempotent
    }

    // Open and configure the port synchronously so the caller learns about
    // a missing device, a permission error or a bad baud rate before
    // start() returns (per the IntegrationBackend contract).
    try {
        io_->port = std::make_unique<asio::serial_port>(io_->context);
        io_->port->open(device_);
        io_->port->set_option(asio::serial_port_base::baud_rate(baudRate_));
        io_->port->set_option(
            asio::serial_port_base::character_size(kCharacterSizeBits));
        io_->port->set_option(asio::serial_port_base::parity(
            asio::serial_port_base::parity::none));
        io_->port->set_option(asio::serial_port_base::stop_bits(
            asio::serial_port_base::stop_bits::one));
        io_->port->set_option(asio::serial_port_base::flow_control(
            asio::serial_port_base::flow_control::none));
    } catch (...) {
        running_.store(false, std::memory_order_release);
        throw;
    }

    // Queue the first read before the worker thread starts, so run() has
    // work and does not return immediately.
    armRead();

    // jthread auto-joins on stop() / dtor.
    thread_ = std::jthread([this]() {
        try {
            io_->context.run();
        } catch (...) {
            // A worker thread cannot propagate; mark the backend down so
            // callers notice via isRunning().
            running_.store(false, std::memory_order_release);
        }
    });
}

void SerialBackend::armRead() {
    io_->port->async_read_some(
        asio::buffer(io_->buffer),
        [this](const boost::system::error_code& ec, std::size_t bytes) {
            if (ec) {
                // Port cancelled by stop(), or an unrecoverable read
                // error. Stop re-arming and let the io_context drain.
                return;
            }
            const std::string_view chunk(io_->buffer.data(), bytes);
            for (const SerialReading& reading : parser_.consume(chunk)) {
                sink_(reading);
            }
            armRead();  // re-arm: the read loop, flat on the stack
        });
}

void SerialBackend::stop() {
    stopImpl();
}

void SerialBackend::stopImpl() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // already stopped, idempotent
    }
    try {
        if (io_) {
            if (io_->port && io_->port->is_open()) {
                // close() cancels any pending async read (its handler then
                // fires with operation_aborted) and releases the descriptor.
                // Its own try so a close error cannot skip the io_context
                // stop + thread join below.
                try {
                    io_->port->close();
                }
                // NOLINTNEXTLINE(bugprone-empty-catch)
                catch (...) { /* shutdown: nowhere to report */ }
            }
            io_->context.stop();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        // Fresh Asio state so a future start() begins from a clean slate
        // (a stopped io_context would otherwise need restart()).
        io_ = std::make_unique<SerialIo>();
    }
    // Shutdown is noexcept by contract; there is nowhere meaningful to
    // surface a stop-time failure.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (...) { /* swallow */ }
}

std::string SerialBackend::metricsSummary() const {
    return std::format("{} @ {}", device_, baudRate_);
}

}  // namespace app::integration
