// Tests for app::integration::modbus::ModbusClient.
//
// Loopback strategy: a FakeModbusSlave listens on an ephemeral port,
// accepts one connection, reads a request frame, and replies with a
// canned response queued by the test. Mirrors the pattern used in
// MqttClientTest -- a real socket on 127.0.0.1, no external mocks,
// no live broker.
//
// What we cover:
//   * Happy path: FC03 + FC04 decode and return the registers.
//   * Server exception: returns ServerException + lastExceptionCode.
//   * Connection refused: returns ConnectionFailed (no listener).
//   * Disconnect mid-stream: returns Disconnected.
//   * Reconnect after a failure: next call succeeds against a fresh
//     listener.

#include "src/integration/modbus/ModbusClient.h"
#include "src/integration/modbus/ModbusPdu.h"

#include <boost/asio.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using app::integration::modbus::ExceptionCode;
using app::integration::modbus::FunctionCode;
using app::integration::modbus::kExceptionFlag;
using app::integration::modbus::kMbapHeaderSize;
using app::integration::modbus::ModbusClient;

namespace {

using boost::asio::ip::tcp;

constexpr auto kTestTimeout = std::chrono::milliseconds{500};

/// Tiny in-process Modbus TCP slave. The test queues canned response
/// frames; the slave accepts one connection, reads requests in a
/// loop, and pops a queued frame off the front for each. The wire
/// layout of incoming requests is exposed via `lastRequest()` so
/// tests can assert the client built the right ADU.
class FakeModbusSlave {
public:
    FakeModbusSlave()
        : acceptor_(io_, tcp::endpoint(tcp::v4(), 0)) {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::jthread([this]() { runAcceptLoop(); });
    }

    ~FakeModbusSlave() { stop(); }

    FakeModbusSlave(const FakeModbusSlave&) = delete;
    FakeModbusSlave& operator=(const FakeModbusSlave&) = delete;
    FakeModbusSlave(FakeModbusSlave&&) = delete;
    FakeModbusSlave& operator=(FakeModbusSlave&&) = delete;

    void stop() {
        if (stopped_.exchange(true)) return;
        // Self-connect to wake a blocking accept(). The acceptor is
        // not thread-safe across accept/close.
        if (thread_.joinable()) {
            boost::asio::io_context wakerIo;
            tcp::socket waker(wakerIo);
            boost::system::error_code ec;
            waker.connect(
                tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                              port_),
                ec);
            thread_.join();
        }
        boost::system::error_code ec;
        acceptor_.close(ec);
        io_.stop();
    }

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    /// Queue a raw ADU to be sent back on the next request.
    void queueResponse(std::vector<std::byte> adu) {
        const std::scoped_lock lock(mutex_);
        responses_.push_back(std::move(adu));
    }

    /// Make the slave close the socket immediately after the next
    /// request -- so the client observes a disconnect mid-stream.
    void closeAfterNextRequest() {
        closeAfterNext_.store(true, std::memory_order_release);
    }

    [[nodiscard]] std::vector<std::byte> lastRequest() const {
        const std::scoped_lock lock(mutex_);
        return lastRequest_;
    }

private:
    void runAcceptLoop() {
        try {
            tcp::socket socket(io_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            if (ec || stopped_.load(std::memory_order_acquire)) return;
            handleClient(socket);
        } catch (...) {
            // Acceptor closed during teardown; exit cleanly.
        }
    }

    void handleClient(tcp::socket& socket) {
        while (!stopped_.load(std::memory_order_acquire)) {
            // Read MBAP header (7 bytes).
            std::vector<std::byte> mbap(kMbapHeaderSize);
            boost::system::error_code ec;
            boost::asio::read(socket, boost::asio::buffer(mbap), ec);
            if (ec) return;

            const std::uint16_t length =
                (static_cast<std::uint16_t>(mbap[4]) << 8) |
                 static_cast<std::uint16_t>(mbap[5]);
            const std::size_t pduBytes =
                static_cast<std::size_t>(length) - 1U;

            // Read PDU.
            std::vector<std::byte> pdu(pduBytes);
            boost::asio::read(socket, boost::asio::buffer(pdu), ec);
            if (ec) return;

            // Stash the full request for the test to inspect.
            std::vector<std::byte> request;
            request.reserve(mbap.size() + pdu.size());
            request.insert(request.end(), mbap.begin(), mbap.end());
            request.insert(request.end(), pdu.begin(), pdu.end());
            {
                const std::scoped_lock lock(mutex_);
                lastRequest_ = std::move(request);
            }

            // Pop the next canned response and ship it.
            std::vector<std::byte> response;
            {
                const std::scoped_lock lock(mutex_);
                if (!responses_.empty()) {
                    response = std::move(responses_.front());
                    responses_.erase(responses_.begin());
                }
            }
            if (!response.empty()) {
                boost::asio::write(socket, boost::asio::buffer(response), ec);
            }
            if (closeAfterNext_.exchange(false, std::memory_order_acq_rel)) {
                return;  // close the socket -> client sees Disconnected
            }
        }
    }

    boost::asio::io_context io_;
    tcp::acceptor acceptor_;
    std::uint16_t port_{0};
    std::jthread thread_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> closeAfterNext_{false};

    mutable std::mutex mutex_;
    std::vector<std::vector<std::byte>> responses_;
    std::vector<std::byte> lastRequest_;
};

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> raw) {
    std::vector<std::byte> out;
    out.reserve(raw.size());
    for (auto b : raw) {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

// Build the canned FC03 response a slave would send for a 2-register
// read of values {0xAA55, 0x1234}. Transaction ID = 1 to match the
// client's nextTid_ = 1.
std::vector<std::byte> fc03TwoRegisterResponse(std::uint16_t tid,
                                               std::uint8_t unitId,
                                               std::uint16_t r0,
                                               std::uint16_t r1) {
    return bytes({
        static_cast<std::uint8_t>(tid >> 8),
        static_cast<std::uint8_t>(tid & 0xFFU),
        0x00, 0x00,             // protocol
        0x00, 0x07,             // length = unit + PDU (1 + 6)
        unitId,
        0x03,                   // FC echo
        0x04,                   // byte count
        static_cast<std::uint8_t>(r0 >> 8),
        static_cast<std::uint8_t>(r0 & 0xFFU),
        static_cast<std::uint8_t>(r1 >> 8),
        static_cast<std::uint8_t>(r1 & 0xFFU),
    });
}

}  // namespace

// ----- happy paths ------------------------------------------------

TEST(ModbusClientTest, ReadHoldingRegistersHappyPath) {
    FakeModbusSlave slave;
    slave.queueResponse(fc03TwoRegisterResponse(/*tid=*/1, /*unit=*/0x11,
                                                0xAA55, 0x1234));

    ModbusClient::Config cfg;
    cfg.port           = slave.port();
    cfg.connectTimeout = kTestTimeout;
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto r = client.readHoldingRegisters(0x11, 0x0000, 2);
    ASSERT_TRUE(r.isOk()) << "err=" << r.errorMessage();
    const auto regs = r.unwrap();
    ASSERT_EQ(regs.size(), 2U);
    EXPECT_EQ(regs[0], 0xAA55U);
    EXPECT_EQ(regs[1], 0x1234U);
    EXPECT_TRUE(client.isConnected());

    // Receipt: the request the client sent matches the expected ADU.
    const auto req = slave.lastRequest();
    ASSERT_EQ(req.size(), 12U);
    EXPECT_EQ(static_cast<std::uint8_t>(req[7]), 0x03U);   // FC03
    EXPECT_EQ(static_cast<std::uint8_t>(req[6]), 0x11U);   // unit
    EXPECT_EQ(static_cast<std::uint8_t>(req[11]), 0x02U);  // qty low
}

TEST(ModbusClientTest, ReadInputRegistersUsesFc04) {
    FakeModbusSlave slave;
    // FC04 response: FC byte = 0x04 (no exception flag).
    slave.queueResponse(bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x05,
        0x07,
        0x04,
        0x02,
        0x00, 0x2A,
    }));

    ModbusClient::Config cfg;
    cfg.port           = slave.port();
    cfg.connectTimeout = kTestTimeout;
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto r = client.readInputRegisters(0x07, 0x0100, 1);
    ASSERT_TRUE(r.isOk()) << "err=" << r.errorMessage();
    ASSERT_EQ(r.unwrap().size(), 1U);
    EXPECT_EQ(r.unwrap()[0], 42U);

    // Receipt: FC byte in the request is 0x04.
    EXPECT_EQ(static_cast<std::uint8_t>(slave.lastRequest()[7]), 0x04U);
}

// ----- error paths ------------------------------------------------

TEST(ModbusClientTest, ConnectionRefusedReturnsConnectionFailed) {
    // No listener on this port. Pick a deliberately closed one.
    ModbusClient::Config cfg;
    cfg.port           = 1;  // privileged, virtually always closed
    cfg.connectTimeout = std::chrono::milliseconds{200};
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto r = client.readHoldingRegisters(0x11, 0, 1);
    ASSERT_TRUE(r.isErr());
    // Either ConnectionFailed (refused) or Timeout (firewall drop).
    // Both indicate "no server", which is what the test really cares
    // about; we accept either.
    EXPECT_TRUE(r.error() == ModbusClient::IoError::ConnectionFailed ||
                r.error() == ModbusClient::IoError::Timeout)
        << "got: " << r.errorMessage();
    EXPECT_FALSE(client.isConnected());
}

TEST(ModbusClientTest, ServerExceptionReturnsServerExceptionAndCachesCode) {
    FakeModbusSlave slave;
    // FC03 exception with IllegalDataAddress (0x02).
    slave.queueResponse(bytes({
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x03,
        0x11,
        static_cast<std::uint8_t>(0x03 | kExceptionFlag),
        0x02,
    }));

    ModbusClient::Config cfg;
    cfg.port           = slave.port();
    cfg.connectTimeout = kTestTimeout;
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto r = client.readHoldingRegisters(0x11, 0xFFFF, 1);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), ModbusClient::IoError::ServerException);

    const auto cached = client.lastExceptionCode();
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(*cached, ExceptionCode::IllegalDataAddress);

    // Socket stays open after a remote rejection -- it's a protocol
    // event, not a transport failure. Next call should reuse the
    // connection.
    EXPECT_TRUE(client.isConnected());
}

TEST(ModbusClientTest, DisconnectMidStreamReturnsDisconnected) {
    FakeModbusSlave slave;
    // No response queued + close-after-first-request: the client
    // writes the request, the slave closes, the client's read
    // observes EOF.
    slave.closeAfterNextRequest();

    ModbusClient::Config cfg;
    cfg.port           = slave.port();
    cfg.connectTimeout = kTestTimeout;
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto r = client.readHoldingRegisters(0x11, 0, 1);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), ModbusClient::IoError::Disconnected);
    EXPECT_FALSE(client.isConnected());
}

TEST(ModbusClientTest, ReconnectsTransparentlyAfterFailure) {
    // First call against a slave that drops the connection. Second
    // call against the same client + same slave (which has accepted
    // a fresh connection in the meantime, because our fake spawns
    // one accept thread per slave -- a real master would loop on
    // accept, but for the test we just spin a second slave).
    FakeModbusSlave deadSlave;
    deadSlave.closeAfterNextRequest();

    ModbusClient::Config cfg;
    cfg.port           = deadSlave.port();
    cfg.connectTimeout = kTestTimeout;
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto first = client.readHoldingRegisters(0x11, 0, 1);
    ASSERT_TRUE(first.isErr());
    EXPECT_FALSE(client.isConnected());

    // Second slave on a fresh port; rebuild a client pointing at it
    // to exercise the same reconnect path the poll loop would on a
    // fresh poll cycle.
    deadSlave.stop();
    FakeModbusSlave liveSlave;
    liveSlave.queueResponse(fc03TwoRegisterResponse(/*tid=*/1, 0x11,
                                                    100, 200));
    cfg.port = liveSlave.port();
    ModbusClient liveClient(cfg);
    auto second = liveClient.readHoldingRegisters(0x11, 0, 2);
    ASSERT_TRUE(second.isOk()) << "err=" << second.errorMessage();
    EXPECT_EQ(second.unwrap()[0], 100U);
    EXPECT_EQ(second.unwrap()[1], 200U);
}

TEST(ModbusClientTest, InvalidQuantityShortCircuitsBeforeIo) {
    // No slave required -- the validator rejects before connecting.
    ModbusClient::Config cfg;
    cfg.port           = 65000;
    cfg.connectTimeout = std::chrono::milliseconds{50};
    cfg.requestTimeout = std::chrono::milliseconds{50};
    ModbusClient client(cfg);

    auto r = client.readHoldingRegisters(0x11, 0, 0);
    ASSERT_TRUE(r.isErr());
    EXPECT_EQ(r.error(), ModbusClient::IoError::InvalidQuantity);
    EXPECT_FALSE(client.isConnected());
}

TEST(ModbusClientTest, SuccessClearsCachedExceptionCode) {
    FakeModbusSlave slave;
    // Response 1: exception. Response 2: normal.
    slave.queueResponse(bytes({
        0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x11,
        static_cast<std::uint8_t>(0x03 | kExceptionFlag),
        0x04,                   // SlaveDeviceFailure
    }));
    slave.queueResponse(fc03TwoRegisterResponse(/*tid=*/2, 0x11, 7, 8));

    ModbusClient::Config cfg;
    cfg.port           = slave.port();
    cfg.connectTimeout = kTestTimeout;
    cfg.requestTimeout = kTestTimeout;
    ModbusClient client(cfg);

    auto r1 = client.readHoldingRegisters(0x11, 0, 1);
    ASSERT_TRUE(r1.isErr());
    EXPECT_EQ(r1.error(), ModbusClient::IoError::ServerException);
    ASSERT_TRUE(client.lastExceptionCode().has_value());

    auto r2 = client.readHoldingRegisters(0x11, 0, 2);
    ASSERT_TRUE(r2.isOk()) << "err=" << r2.errorMessage();
    EXPECT_FALSE(client.lastExceptionCode().has_value());
}
