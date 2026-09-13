// [utest->req~integration-007~1]
// Covers REQ-INTEGRATION-007 (read-only HTTP/REST backend).
//
// Boots the backend on an OS-assigned port (port=0), then drives it with a
// real loopback HTTP client (cpp-httplib's client), exercising every route
// end-to-end. gmock fakes stand in for ProductionModel + ProductsRepository;
// a real AlertCenter is seeded so /alarms reflects live state.
//
// Hermetic: only loopback HTTP is required -- no Xvfb, no GTK, no broker.

#include "src/integration/HttpBackend.h"

#include "src/core/LoggerImpl.h"
#include "src/model/Product.h"
#include "src/model/ProductionModel.h"
#include "src/model/ProductsRepository.h"
#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

// Match HttpBackend.cpp's Windows include hygiene so the client header behaves.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif
#include <httplib.h>
#ifdef ERROR
#  undef ERROR
#endif

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using ::testing::NiceMock;
using ::testing::Return;

using app::integration::HttpBackend;
using app::model::Product;
using app::model::ProductionModel;
using app::model::ProductsRepository;
using app::model::SystemState;

namespace {

constexpr const char* kLoopback   = "127.0.0.1";
constexpr int         kOkStatus   = 200;
constexpr int         kNotFound   = 404;

class MockProductionModel : public ProductionModel {
public:
    MOCK_METHOD(void, onEquipmentStatusChanged, (EquipmentCallback), (override));
    MOCK_METHOD(void, onActuatorStatusChanged, (ActuatorCallback), (override));
    MOCK_METHOD(void, onQualityCheckpointChanged, (QualityCheckpointCallback),
                (override));
    MOCK_METHOD(void, onWorkUnitChanged, (WorkUnitCallback), (override));
    MOCK_METHOD(void, onSystemStateChanged, (StateCallback), (override));

    MOCK_METHOD(void, startProduction, (), (override));
    MOCK_METHOD(void, stopProduction, (), (override));
    MOCK_METHOD(void, resetSystem, (), (override));
    MOCK_METHOD(void, startCalibration, (), (override));
    MOCK_METHOD(void, setEquipmentEnabled, (uint32_t, bool), (override));
    MOCK_METHOD(void, loadProduct,
                (const app::model::Product&, const app::model::Recipe&),
                (override));
    MOCK_METHOD(void, setEquipmentSupplyLevel, (uint32_t, int), (override));
    MOCK_METHOD(void, setQualityPassRate, (uint32_t, float), (override));

    MOCK_METHOD(SystemState, getState, (), (const, override));
    MOCK_METHOD(app::model::QualityCheckpoint, getQualityCheckpoint,
                (uint32_t), (const, override));
    MOCK_METHOD((std::vector<app::model::QualityCheckpoint>),
                getQualityCheckpoints, (), (const, override));
    MOCK_METHOD(app::model::WorkUnit, getWorkUnit, (), (const, override));
    MOCK_METHOD(std::string, lastFaultReason, (), (const, override));
    MOCK_METHOD(app::model::OeeMetrics, oeeSnapshot, (), (const, override));
};

class MockProductsRepository : public ProductsRepository {
public:
    MOCK_METHOD(std::vector<Product>, getAllProducts, (), (override));
    MOCK_METHOD(Product, getProduct, (int), (override));
    MOCK_METHOD(std::vector<Product>, searchProducts, (const std::string&),
                (override));
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

/// RAII fixture: starts a backend on a random port and hands the test a
/// connected HTTP client; tears both down on destruction.
class HttpFixture {
public:
    HttpFixture()
        : logger_(std::make_unique<app::core::ConsoleLogger>()),
          backend_(0, kLoopback, model_, repo_, alerts_, logger_) {
        backend_.start();
        client_ = std::make_unique<httplib::Client>(kLoopback,
                                                     backend_.boundPort());
        client_->set_connection_timeout(2, 0);
        client_->set_read_timeout(2, 0);
    }

    ~HttpFixture() { backend_.stop(); }

    HttpFixture(const HttpFixture&)            = delete;
    HttpFixture& operator=(const HttpFixture&) = delete;

    NiceMock<MockProductionModel>&  model()  { return model_; }
    NiceMock<MockProductsRepository>& repo() { return repo_; }
    app::presenter::AlertCenter&    alerts() { return alerts_; }
    HttpBackend&                    backend() { return backend_; }
    httplib::Client&                client() { return *client_; }

private:
    NiceMock<MockProductionModel>    model_;
    NiceMock<MockProductsRepository> repo_;
    app::presenter::AlertCenter      alerts_;
    app::core::Logger                logger_;
    HttpBackend                      backend_;
    std::unique_ptr<httplib::Client> client_;
};

TEST(HttpBackendTest, StartStopIsRunning) {
    NiceMock<MockProductionModel>    model;
    NiceMock<MockProductsRepository> repo;
    app::presenter::AlertCenter      alerts;
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};

    HttpBackend backend(0, kLoopback, model, repo, alerts, logger);
    EXPECT_FALSE(backend.isRunning());

    backend.start();
    EXPECT_TRUE(backend.isRunning());
    EXPECT_GT(backend.boundPort(), 0);

    backend.stop();
    EXPECT_FALSE(backend.isRunning());
    EXPECT_EQ(backend.boundPort(), 0);
}

TEST(HttpBackendTest, HealthReturns200AndOkJson) {
    HttpFixture f;
    auto res = f.client().Get("/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kOkStatus);

    const auto j = nlohmann::json::parse(res->body);
    EXPECT_EQ(j.at("status").get<std::string>(), "ok");
}

TEST(HttpBackendTest, StatusReturnsSystemState) {
    HttpFixture f;
    EXPECT_CALL(f.model(), getState())
        .WillRepeatedly(Return(SystemState::RUNNING));

    auto res = f.client().Get("/status");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kOkStatus);

    const auto j = nlohmann::json::parse(res->body);
    EXPECT_EQ(j.at("state").get<std::string>(), "running");
    EXPECT_TRUE(j.at("running").get<bool>());
}

TEST(HttpBackendTest, AlarmsReturnsAlertCenterSnapshot) {
    HttpFixture f;

    app::presenter::AlertViewModel vm;
    vm.key      = "equipment-0-offline";
    vm.severity = app::presenter::AlertSeverity::Critical;
    vm.title    = "Equipment offline";
    vm.message  = "Line 0 lost supply";
    vm.priority = app::presenter::kAlarmPriorityHigh;
    f.alerts().raise(vm);

    auto res = f.client().Get("/alarms");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kOkStatus);

    const auto j = nlohmann::json::parse(res->body);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 1U);
    EXPECT_EQ(j[0].at("key").get<std::string>(), "equipment-0-offline");
    EXPECT_EQ(j[0].at("severity").get<std::string>(), "critical");
    EXPECT_EQ(j[0].at("title").get<std::string>(), "Equipment offline");
}

TEST(HttpBackendTest, ProductsReturnsRepositorySnapshot) {
    HttpFixture f;
    const std::vector<Product> products = {
        makeProduct(1, "PROD-001", "Widget A", 850, 98.1F),
        makeProduct(2, "PROD-002", "Widget B", 120, 91.5F),
    };
    EXPECT_CALL(f.repo(), getAllProducts()).WillRepeatedly(Return(products));

    auto res = f.client().Get("/products");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kOkStatus);

    const auto j = nlohmann::json::parse(res->body);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 2U);
    EXPECT_EQ(j[0].at("productCode").get<std::string>(), "PROD-001");
    EXPECT_EQ(j[0].at("name").get<std::string>(), "Widget A");
    EXPECT_EQ(j[0].at("stock").get<int>(), 850);
    EXPECT_EQ(j[1].at("productCode").get<std::string>(), "PROD-002");
}

TEST(HttpBackendTest, UnknownPathReturns404) {
    HttpFixture f;
    auto res = f.client().Get("/does-not-exist");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kNotFound);

    const auto j = nlohmann::json::parse(res->body);
    EXPECT_EQ(j.at("error").get<std::string>(), "not found");
}

TEST(HttpBackendTest, RouteTableHasNoDuplicatePaths) {
    auto paths = HttpBackend::routePaths();
    EXPECT_FALSE(paths.empty());

    std::vector<std::string_view> sorted = paths;
    std::sort(sorted.begin(), sorted.end());
    const auto dup = std::adjacent_find(sorted.begin(), sorted.end());
    EXPECT_EQ(dup, sorted.end()) << "route table contains a duplicate path";
}

}  // namespace
