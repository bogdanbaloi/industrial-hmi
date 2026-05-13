#include "src/integration/modbus/ModbusClient.h"

#include <boost/asio.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace app::integration::modbus {

namespace {

// Distinguish "the op never completed" from "the op completed with
// error code Foo". We arm the sentinel before issuing the async op,
// then the completion handler overwrites it.
inline constexpr auto kPendingError = boost::asio::error::would_block;

}  // namespace

namespace core = app::core;

/// Boost.Asio state, hidden from the header so consumers stay free
/// of the full Asio surface. Owns the io_context and the socket.
struct ModbusClient::AsioState {
    boost::asio::io_context io;
    boost::asio::ip::tcp::socket socket{io};
};

ModbusClient::ModbusClient(Config config)
    : config_(std::move(config)),
      asio_(std::make_unique<AsioState>()) {}

ModbusClient::~ModbusClient() {
    disconnect();
}

bool ModbusClient::isConnected() const noexcept {
    return connected_.load(std::memory_order_acquire);
}

std::optional<ExceptionCode> ModbusClient::lastExceptionCode() const noexcept {
    const std::scoped_lock lock(mutex_);
    return lastExceptionCode_;
}

core::Result<std::vector<std::uint16_t>, ModbusClient::IoError>
ModbusClient::readHoldingRegisters(std::uint8_t unitId,
                                   std::uint16_t address,
                                   std::uint16_t quantity) {
    return readImpl(FunctionCode::ReadHoldingRegisters,
                    unitId, address, quantity);
}

core::Result<std::vector<std::uint16_t>, ModbusClient::IoError>
ModbusClient::readInputRegisters(std::uint8_t unitId,
                                 std::uint16_t address,
                                 std::uint16_t quantity) {
    return readImpl(FunctionCode::ReadInputRegisters,
                    unitId, address, quantity);
}

void ModbusClient::disconnect() noexcept {
    connected_.store(false, std::memory_order_release);
    boost::system::error_code ec;
    asio_->socket.cancel(ec);
    asio_->socket.close(ec);
    // Drain any leftover handlers so the next run_for starts clean.
    asio_->io.restart();
}

core::Result<void, ModbusClient::IoError>
ModbusClient::ensureConnected() {
    using Res = core::Result<void, IoError>;
    if (connected_.load(std::memory_order_acquire) &&
        asio_->socket.is_open()) {
        return Res(core::Ok);
    }

    // Stale socket -- close before reopening.
    boost::system::error_code closeEc;
    asio_->socket.close(closeEc);
    asio_->io.restart();

    boost::asio::ip::tcp::resolver resolver(asio_->io);
    boost::system::error_code resolveEc;
    const auto endpoints = resolver.resolve(
        config_.host, std::to_string(config_.port), resolveEc);
    if (resolveEc) {
        return Res(core::Err, IoError::ConnectionFailed);
    }

    // async_connect + run_for() = sync-with-timeout. The completion
    // handler stamps the final error code; if run_for() returns
    // before the handler fires, we're past the deadline.
    boost::system::error_code connectEc = kPendingError;
    boost::asio::async_connect(
        asio_->socket, endpoints,
        [&connectEc](const boost::system::error_code& ec,
                     const boost::asio::ip::tcp::endpoint&) {
            connectEc = ec;
        });
    asio_->io.run_for(config_.connectTimeout);

    if (connectEc == kPendingError) {
        boost::system::error_code cancelEc;
        asio_->socket.cancel(cancelEc);
        asio_->io.run();  // drain the now-cancelled handler
        return Res(core::Err, IoError::Timeout);
    }
    if (connectEc) {
        return Res(core::Err, IoError::ConnectionFailed);
    }

    connected_.store(true, std::memory_order_release);
    return Res(core::Ok);
}

core::Result<std::vector<std::uint16_t>, ModbusClient::IoError>
ModbusClient::readImpl(FunctionCode fc,
                       std::uint8_t unitId,
                       std::uint16_t address,
                       std::uint16_t quantity) {
    using Res = core::Result<std::vector<std::uint16_t>, IoError>;
    const std::scoped_lock lock(mutex_);

    if (quantity == 0 || quantity > kMaxRegistersPerRead) {
        return Res(core::Err, IoError::InvalidQuantity);
    }

    if (auto conn = ensureConnected(); conn.isErr()) {
        return Res(core::Err, conn.error());
    }

    // ---- encode request ----------------------------------------
    const std::uint16_t tid = nextTid_.fetch_add(1, std::memory_order_relaxed);
    const auto request = encodeReadRequest(tid, unitId, fc,
                                           address, quantity);

    // ---- write request -----------------------------------------
    boost::system::error_code writeEc = kPendingError;
    std::size_t bytesWritten = 0;
    boost::asio::async_write(
        asio_->socket,
        boost::asio::buffer(request),
        [&writeEc, &bytesWritten](const boost::system::error_code& ec,
                                  std::size_t n) {
            writeEc = ec;
            bytesWritten = n;
        });
    asio_->io.restart();
    asio_->io.run_for(config_.requestTimeout);
    if (writeEc == kPendingError) {
        disconnect();
        return Res(core::Err, IoError::Timeout);
    }
    if (writeEc || bytesWritten != request.size()) {
        disconnect();
        return Res(core::Err, IoError::WriteFailed);
    }

    // ---- read MBAP header (7 bytes exact) ----------------------
    std::vector<std::byte> adu(kMbapHeaderSize);
    boost::system::error_code readHeaderEc = kPendingError;
    boost::asio::async_read(
        asio_->socket,
        boost::asio::buffer(adu),
        [&readHeaderEc](const boost::system::error_code& ec, std::size_t) {
            readHeaderEc = ec;
        });
    asio_->io.restart();
    asio_->io.run_for(config_.requestTimeout);
    if (readHeaderEc == kPendingError) {
        disconnect();
        return Res(core::Err, IoError::Timeout);
    }
    if (readHeaderEc == boost::asio::error::eof) {
        disconnect();
        return Res(core::Err, IoError::Disconnected);
    }
    if (readHeaderEc) {
        disconnect();
        return Res(core::Err, IoError::ReadFailed);
    }

    // ---- parse length field, read PDU body ---------------------
    const std::uint16_t length =
        (static_cast<std::uint16_t>(adu[4]) << 8) |
         static_cast<std::uint16_t>(adu[5]);

    // Spec ceiling on length value: unit + PDU <= 1 + 253 = 254.
    // Anything beyond is a peer bug; bail before allocating huge.
    constexpr std::uint16_t kMaxLengthField = 1U + kMaxPduSize;
    if (length == 0 || length > kMaxLengthField) {
        disconnect();
        return Res(core::Err, IoError::DecodeFailed);
    }

    const std::size_t pduBytes = static_cast<std::size_t>(length) - 1U;
    const std::size_t headerSize = adu.size();
    adu.resize(headerSize + pduBytes);

    boost::system::error_code readBodyEc = kPendingError;
    boost::asio::async_read(
        asio_->socket,
        boost::asio::buffer(adu.data() + headerSize, pduBytes),
        [&readBodyEc](const boost::system::error_code& ec, std::size_t) {
            readBodyEc = ec;
        });
    asio_->io.restart();
    asio_->io.run_for(config_.requestTimeout);
    if (readBodyEc == kPendingError) {
        disconnect();
        return Res(core::Err, IoError::Timeout);
    }
    if (readBodyEc == boost::asio::error::eof) {
        disconnect();
        return Res(core::Err, IoError::Disconnected);
    }
    if (readBodyEc) {
        disconnect();
        return Res(core::Err, IoError::ReadFailed);
    }

    // ---- decode -------------------------------------------------
    ResponseContext ctx;
    ctx.expectedTransactionId = tid;
    ctx.expectedUnitId        = unitId;
    ctx.expectedFunctionCode  = fc;
    ctx.expectedQuantity      = quantity;
    auto decoded = decodeReadResponse(adu, ctx);
    if (decoded.isErr()) {
        // Framing failure: keep socket open (peer may be fine, this
        // frame may have been corrupted in transit) but surface the
        // error so the poll loop can decide.
        return Res(core::Err, IoError::DecodeFailed);
    }

    auto& payload = decoded.unwrap();
    // Decoder contract: exception responses come back as Ok with
    // empty registers + populated exceptionCode (including Other for
    // unknown codes). Normal responses always have registers.size()
    // == quantity (>= 1, since we rejected quantity == 0 earlier).
    // So registers.empty() is the unambiguous "remote rejected"
    // signal.
    if (payload.registers.empty()) {
        lastExceptionCode_ = payload.exceptionCode;
        return Res(core::Err, IoError::ServerException);
    }

    // Successful read clears any stale exception code.
    lastExceptionCode_.reset();
    return Res(core::Ok, std::move(payload.registers));
}

}  // namespace app::integration::modbus

// NOTE: errorToString specialisation for ModbusReader::IoError lives
// in ModbusPdu.cpp alongside the DecodeError table. That TU is the
// lowest common ancestor of every consumer (Client, PollLoop, future
// RTU client), so the linker resolves the symbol regardless of which
// modbus components a binary pulls in.
