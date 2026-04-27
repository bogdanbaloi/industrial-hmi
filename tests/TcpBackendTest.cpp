// Tests for app::integration::TcpBackend.
//
// Boots the backend on an OS-assigned port (port=0), opens a real TCP
// client socket, and exercises the line protocol end-to-end. Uses
// gmock-backed fakes for ProductionModel + ProductsRepository so the
// dispatcher's calls are observable without bringing up SimulatedModel
// or DatabaseManager.
//
// These run hermetically -- no Xvfb, no GTK, no external broker. The
// only platform requirement is loopback TCP, which works on every CI
// runner.

#include "src/integration/TcpBackend.h"

#include "src/model/ProductionModel.h"
#include "src/model/ProductsRepository.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using ::testing::_;
using ::testing::Return;

using app::integration::TcpBackend;
using app::model::Product;
using app::model::ProductionModel;
using app::model::ProductsRepository;
using app::model::SystemState;

namespace asio = boost::asio;
using boost::asio::ip::tcp;

namespace {

class MockProductionModel : public ProductionModel {
public:
    MOCK_METHOD(void, onEquipmentStatusChanged, (EquipmentCallback), (override));
    MOCK_METHOD(void, onActuatorStatusChanged, (ActuatorCallback), (override));
    MOCK_METHOD(void, onQualityCheckpointChanged, (QualityCheckpointCallback), (override));
    MOCK_METHOD(void, onWorkUnitChanged, (WorkUnitCallback), (override));
    MOCK_METHOD(void, onSystemStateChanged, (StateCallback), (override));

    MOCK_METHOD(void, startProduction, (), (override));
    MOCK_METHOD(void, stopProduction, (), (override));
    MOCK_METHOD(void, resetSystem, (), (override));
    MOCK_METHOD(void, startCalibration, (), (override));
    MOCK_METHOD(void, setEquipmentEnabled, (uint32_t, bool), (override));

    MOCK_METHOD(SystemState, getState, (), (const, override));
    MOCK_METHOD(app::model::QualityCheckpoint, getQualityCheckpoint,
                (uint32_t), (const, override));
    MOCK_METHOD(app::model::WorkUnit, getWorkUnit, (), (const, override));
};

class MockProductsRepository : public ProductsRepository {
public:
    MOCK_METHOD(std::vector<Product>, getAllProducts, (), (override));
    MOCK_METHOD(Product, getProduct, (int), (override));
    MOCK_METHOD(std::vector<Product>, searchProducts,
                (const std::string&), (override));
};

Product makeProduct(int id, const std::string& code, const std::string& name,
                    int stock, float quality) {
    Product p;
    p.id = id;
    p.productCode = code;
    p.name = name;
    p.status = "Active";
    p.stock = stock;
    p.qualityRate = quality;
    return p;
}

/// RAII fixture: starts a backend on a random port, gives the test a
/// connected client socket, and tears everything down on destruction.
class TcpClient {
public:
    TcpClient(MockProductionModel& model, MockProductsRepository& repo)
        : backend_(0, model, repo) {
        backend_.start();
        // start() returned -- backend is bound; query the OS-assigned port.
        port_ = backend_.boundPort();

        // Tiny retry loop because the accept loop is on a different
        // thread and may not be ready the instant start() returns.
        for (int attempt = 0; attempt < 10; ++attempt) {
            try {
                socket_.connect(tcp::endpoint(
                    asio::ip::make_address("127.0.0.1"), port_));
                return;
            } catch (...) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        throw std::runtime_error("Could not connect to TcpBackend");
    }

    ~TcpClient() {
        boost::system::error_code ec;
        socket_.close(ec);
        backend_.stop();
    }

    /// Send a single command line + read until "\n" terminator.
    /// Returns the response (one or more lines, raw).
    ///
    /// IMPORTANT: read_until is allowed to buffer bytes past the
    /// first '\n' it sees, so we share a single streambuf across
    /// iterations. A fresh streambuf each iteration would discard
    /// bytes already in flight and the second read_until would
    /// block forever on data the socket already delivered.
    std::string sendCommand(const std::string& cmd, std::size_t expectedLines = 1) {
        const std::string framed = cmd + "\n";
        asio::write(socket_, asio::buffer(framed));

        std::string out;
        for (std::size_t i = 0; i < expectedLines; ++i) {
            asio::read_until(socket_, readBuf_, '\n');
            std::istream is(&readBuf_);
            std::string line;
            std::getline(is, line);
            out += line + '\n';
        }
        return out;
    }

private:
    TcpBackend backend_;
    asio::io_context io_;
    tcp::socket socket_{io_};
    asio::streambuf readBuf_;  // shared across sendCommand calls
    std::uint16_t port_{0};
};

}  // namespace

TEST(TcpBackendTest, BindsToOsAssignedPortWhenZero) {
    MockProductionModel model;
    MockProductsRepository repo;
    TcpBackend backend(0, model, repo);

    EXPECT_EQ(backend.boundPort(), 0u);  // before start
    backend.start();
    EXPECT_GT(backend.boundPort(), 0u);   // OS picked one
    EXPECT_TRUE(backend.isRunning());
    backend.stop();
    EXPECT_FALSE(backend.isRunning());
}

TEST(TcpBackendTest, NameIsTcp) {
    MockProductionModel model;
    MockProductsRepository repo;
    TcpBackend backend(0, model, repo);
    EXPECT_EQ(backend.name(), "TCP");
}

TEST(TcpBackendTest, StartIsIdempotent) {
    MockProductionModel model;
    MockProductsRepository repo;
    TcpBackend backend(0, model, repo);
    backend.start();
    const auto port1 = backend.boundPort();
    EXPECT_NO_THROW(backend.start());  // second call is a no-op
    EXPECT_EQ(backend.boundPort(), port1);
    backend.stop();
}

TEST(TcpBackendTest, StopIsIdempotent) {
    MockProductionModel model;
    MockProductsRepository repo;
    TcpBackend backend(0, model, repo);
    backend.start();
    backend.stop();
    EXPECT_NO_THROW(backend.stop());  // second call is a no-op
}

// Protocol -- happy paths

TEST(TcpBackendTest, StatusCommandReturnsRunningJson) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, getState())
        .WillOnce(Return(SystemState::RUNNING));

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("status");

    EXPECT_NE(response.find("\"state\":\"running\""), std::string::npos);
    EXPECT_NE(response.find("\"running\":true"),      std::string::npos);
}

TEST(TcpBackendTest, StatusCommandReturnsIdleJson) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, getState())
        .WillOnce(Return(SystemState::IDLE));

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("status");

    EXPECT_NE(response.find("\"state\":\"idle\""),  std::string::npos);
    EXPECT_NE(response.find("\"running\":false"),    std::string::npos);
}

TEST(TcpBackendTest, ProductsCommandReturnsCountAndJsonLines) {
    MockProductionModel model;
    MockProductsRepository repo;
    std::vector<Product> seed{
        makeProduct(1, "P-001", "Alpha", 10, 99.0f),
        makeProduct(2, "P-002", "Beta",  20, 88.5f),
    };
    EXPECT_CALL(repo, getAllProducts()).WillOnce(Return(seed));

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("products", /*lines=*/3);

    // First line is the count.
    EXPECT_NE(response.find("2\n"), std::string::npos);
    EXPECT_NE(response.find("P-001"), std::string::npos);
    EXPECT_NE(response.find("Alpha"), std::string::npos);
    EXPECT_NE(response.find("P-002"), std::string::npos);
    EXPECT_NE(response.find("Beta"),  std::string::npos);
}

TEST(TcpBackendTest, EqOnCommandCallsSetEquipmentEnabledTrue) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, setEquipmentEnabled(42u, true)).Times(1);

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("eq 42 on");
    EXPECT_NE(response.find("OK"), std::string::npos);
}

TEST(TcpBackendTest, EqOffCommandCallsSetEquipmentEnabledFalse) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, setEquipmentEnabled(7u, false)).Times(1);

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("eq 7 off");
    EXPECT_NE(response.find("OK"), std::string::npos);
}

TEST(TcpBackendTest, ProductionStartCallsStartProduction) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, startProduction()).Times(1);

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("production start");
    EXPECT_NE(response.find("OK"), std::string::npos);
}

TEST(TcpBackendTest, ProductionStopCallsStopProduction) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, stopProduction()).Times(1);

    TcpClient client(model, repo);
    EXPECT_NE(client.sendCommand("production stop").find("OK"),
              std::string::npos);
}

TEST(TcpBackendTest, ProductionResetCallsResetSystem) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, resetSystem()).Times(1);

    TcpClient client(model, repo);
    EXPECT_NE(client.sendCommand("production reset").find("OK"),
              std::string::npos);
}

TEST(TcpBackendTest, HelpCommandPrintsBanner) {
    MockProductionModel model;
    MockProductsRepository repo;

    TcpClient client(model, repo);
    const std::string response =
        client.sendCommand("help", /*lines=*/7);  // help banner is multi-line

    EXPECT_NE(response.find("Available commands"), std::string::npos);
    EXPECT_NE(response.find("status"),             std::string::npos);
    EXPECT_NE(response.find("products"),           std::string::npos);
}

// Protocol -- error paths

TEST(TcpBackendTest, UnknownCommandReturnsErr) {
    MockProductionModel model;
    MockProductsRepository repo;

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("nonsense");
    EXPECT_NE(response.find("ERR unknown command"), std::string::npos);
}

TEST(TcpBackendTest, EqWithBadIdReturnsErr) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, setEquipmentEnabled(_, _)).Times(0);

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("eq abc on");
    EXPECT_NE(response.find("ERR bad id"), std::string::npos);
}

TEST(TcpBackendTest, EqWithMissingArgReturnsErr) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, setEquipmentEnabled(_, _)).Times(0);

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("eq 42");
    EXPECT_NE(response.find("ERR usage"), std::string::npos);
}

TEST(TcpBackendTest, ProductionWithBadActionReturnsErr) {
    MockProductionModel model;
    MockProductsRepository repo;
    EXPECT_CALL(model, startProduction()).Times(0);

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("production foo");
    EXPECT_NE(response.find("ERR expected start|stop|reset"),
              std::string::npos);
}

TEST(TcpBackendTest, QuitClosesConnectionAfterBye) {
    MockProductionModel model;
    MockProductsRepository repo;

    TcpClient client(model, repo);
    const std::string response = client.sendCommand("quit");
    EXPECT_NE(response.find("BYE"), std::string::npos);
    // No further commands -- next read on the socket would return EOF.
}
