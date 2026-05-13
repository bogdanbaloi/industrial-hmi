#pragma once

#include "src/core/Result.h"
#include "src/integration/modbus/ModbusPdu.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Forward-declare Asio types so the header doesn't drag
// <boost/asio.hpp> into every TU that includes us. Foreign API
// naming -- we don't get to rename io_context to CamelCase.
namespace boost::asio {
    // NOLINTNEXTLINE(readability-identifier-naming)
    class io_context;
    namespace ip {
        // NOLINTBEGIN(readability-identifier-naming)
        class tcp;
        // NOLINTEND(readability-identifier-naming)
    }
}

namespace app::integration::modbus {

/// Synchronous Modbus TCP master client.
///
/// Wraps a single TCP socket to one slave endpoint and exposes
/// `readHoldingRegisters` / `readInputRegisters` as blocking
/// request/response calls. Cadence (poll interval, parallel slaves)
/// is the caller's problem -- this class is one socket, one slave,
/// one in-flight request at a time.
///
/// Threading: every public method serialises on an internal mutex,
/// so it's safe to share a client across threads even though the
/// expected use is a single poll loop calling on its own jthread.
/// The io_context lives inside the client; `io.run_for(timeout)`
/// gives us connect / write / read with deadlines, avoiding the
/// platform-specific dance around SO_RCVTIMEO.
///
/// Failure model:
///   * Local framing / transport errors -> Err(IoError::*)
///   * Remote exception PDU (FC | 0x80)  -> Err(IoError::ServerException)
///     plus `lastExceptionCode()` exposes the parsed code for logs /
///     I/O panel tooltip.
///   * Connection drops are observed lazily on the next call: the
///     client reconnects transparently if `isConnected()` is false.
///
/// SOLID:
///   * S -- one job: ship Modbus PDUs over TCP and surface a typed
///     Result. No domain awareness, no polling cadence, no register
///     map.
///   * O -- new function codes (FC06 write, FC01 coils) plug in as
///     additional methods using the same ModbusPdu helpers; existing
///     callers stay binary-stable.
///   * L -- the class is concrete, not virtual. Substitutability
///     lives at the poll-loop layer where a FakeModbusClient
///     implements the same shape against a different transport for
///     tests.
///   * D -- depends on ModbusPdu (pure functions); the bridge above
///     depends on this concrete class through a thin adapter when /
///     if a second transport (RTU over serial) appears.
class ModbusClient {
public:
    struct Config {
        std::string host{"127.0.0.1"};
        std::uint16_t port{502};
        std::chrono::milliseconds connectTimeout{2000};
        std::chrono::milliseconds requestTimeout{1000};
    };

    /// IoError lumps transport-level and decoder-level failures into
    /// one enum so callers have a single switch / log site. The
    /// decoder's DecodeError stays internal to the codec; this
    /// surface is the one the poll loop and metrics use.
    enum class IoError : std::uint8_t {
        ConnectionFailed,   ///< TCP connect refused / DNS failed
        Timeout,            ///< Operation exceeded the configured deadline
        WriteFailed,        ///< Socket write returned an error
        ReadFailed,         ///< Socket read returned an error
        Disconnected,       ///< Peer closed the socket mid-stream
        DecodeFailed,       ///< MBAP / PDU framing rejected by decoder
        ServerException,    ///< Slave returned exception PDU; see
                            ///< lastExceptionCode()
        InvalidQuantity,    ///< Quantity outside [1, 125]
    };

    explicit ModbusClient(Config config);
    ~ModbusClient();

    // Owns a socket + io_context. No copies, no moves -- the io_context
    // is non-movable in older Boost and keeping the API symmetric
    // makes the lifetime story unambiguous.
    ModbusClient(const ModbusClient&) = delete;
    ModbusClient& operator=(const ModbusClient&) = delete;
    ModbusClient(ModbusClient&&) = delete;
    ModbusClient& operator=(ModbusClient&&) = delete;

    /// FC03 -- read holding registers from `unitId` starting at
    /// `address` for `quantity` registers (1..125). Reconnects on
    /// demand if the socket is down.
    [[nodiscard]] app::core::Result<std::vector<std::uint16_t>, IoError>
    readHoldingRegisters(std::uint8_t unitId,
                         std::uint16_t address,
                         std::uint16_t quantity);

    /// FC04 -- read input registers (R-only). Same shape as FC03.
    [[nodiscard]] app::core::Result<std::vector<std::uint16_t>, IoError>
    readInputRegisters(std::uint8_t unitId,
                       std::uint16_t address,
                       std::uint16_t quantity);

    /// True iff the socket is currently open and the last operation
    /// did not observe a transport-level failure. Drives the I/O
    /// panel pill colour for the Modbus backend.
    [[nodiscard]] bool isConnected() const noexcept;

    /// Last exception code returned by the slave, if any. Cleared on
    /// the next successful read. Surfaced in the I/O panel tooltip
    /// so an operator can tell "remote rejected" apart from "wire
    /// broken".
    [[nodiscard]] std::optional<ExceptionCode>
    lastExceptionCode() const noexcept;

private:
    /// Shared body of FC03 / FC04 -- the only difference is the
    /// function code byte.
    [[nodiscard]] app::core::Result<std::vector<std::uint16_t>, IoError>
    readImpl(FunctionCode fc,
             std::uint8_t unitId,
             std::uint16_t address,
             std::uint16_t quantity);

    /// Open (or reopen) the TCP socket. Lazy: called on demand from
    /// readImpl when the socket is down. Returns Ok or
    /// ConnectionFailed / Timeout.
    [[nodiscard]] app::core::Result<void, IoError> ensureConnected();

    void disconnect() noexcept;

    Config config_;

    // PIMPL on the Boost.Asio types so consumers don't pull the full
    // Asio surface through this header. Same approach as MqttClient.
    struct AsioState;
    std::unique_ptr<AsioState> asio_;

    /// Transaction ID round-robins per request; the codec validates
    /// the echo so a stale frame from a previous timed-out request
    /// is detected by the decoder rather than mis-attributed to a
    /// new one.
    std::atomic<std::uint16_t> nextTid_{1};

    std::atomic<bool> connected_{false};
    std::optional<ExceptionCode> lastExceptionCode_;

    mutable std::mutex mutex_;
};

}  // namespace app::integration::modbus
