// [utest->req~integration-007~1]
// Covers REQ-INTEGRATION-007 (HTTP/REST backend) at the composition seam:
// buildIntegrationServices, handed a live AlertCenter with network.http.enabled
// true, actually registers and starts the HTTP backend, and its `/alarms` route
// projects that AlertCenter. This is the activation the GTK/console frontends
// rely on -- the unit-level HttpBackendTest proves the routes in isolation,
// this proves the shared bootstrap wires them.
//
// Hermetic: only a loopback HTTP socket is opened. No GTK, no Xvfb, no broker.
// IntegrationBootstrap.cpp is compiled directly with INDUSTRIAL_HMI_HAS_HTTP_BACKEND
// so the registerHttpBackend branch is live regardless of the build's
// BUILD_HTTP_BACKEND switch.

#include "src/app/IntegrationBootstrap.h"

#include "src/config/ConfigManager.h"
#include "src/core/LoggerImpl.h"
#include "src/integration/HttpBackend.h"
#include "src/integration/IntegrationManager.h"
#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"

#include <gtest/gtest.h>

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

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

using app::config::ConfigManager;

namespace {

constexpr const char* kLoopback = "127.0.0.1";
constexpr int         kOkStatus = 200;

// Find the running HTTP backend among the manager's registered backends and
// report the port it bound. Returns 0 when the HTTP backend was not
// registered -- the failure the caller asserts against.
std::uint16_t httpBoundPort(const app::integration::IntegrationManager& manager) {
    for (const auto& backend : manager.backends()) {
        if (auto* http =
                dynamic_cast<app::integration::HttpBackend*>(backend.get())) {
            return http->boundPort();
        }
    }
    return 0;
}

class IntegrationBootstrapTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string name = info ? info->name() : "unnamed";
        configPath_ = fs::temp_directory_path() /
                      ("industrial-hmi-bootstrap-" + name + ".json");
        ConfigManager::instance().clear();
    }

    void TearDown() override {
        ConfigManager::instance().clear();
        std::error_code ec;
        fs::remove(configPath_, ec);
    }

    // Only the HTTP backend enabled; every other backend defaults to false so
    // this composition brings up exactly one loopback socket. port 0 asks the
    // OS for a free port, read back through the backend.
    void writeHttpOnlyConfig() {
        std::ofstream out(configPath_, std::ios::trunc);
        out << R"({
  "application": { "name": "Industrial HMI" },
  "network": {
    "http": { "enabled": true, "port": 0, "bind_address": "127.0.0.1" }
  }
})";
    }

    fs::path configPath_;
};

TEST_F(IntegrationBootstrapTest, AlarmsRouteServesRaisedAlarmThroughBootstrap) {
    writeHttpOnlyConfig();
    auto& config = ConfigManager::instance();
    ASSERT_TRUE(config.initialize(configPath_.string()));
    ASSERT_TRUE(config.isHttpBackendEnabled());

    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};

    // The alarm store must outlive the services bundle: the HTTP backend holds
    // a reference into it for the /alarms route. Declared first, destroyed last.
    app::presenter::AlertCenter alerts;
    app::presenter::AlertViewModel vm;
    vm.key      = "equipment-0-offline";
    vm.severity = app::presenter::AlertSeverity::Critical;
    vm.title    = "Equipment offline";
    vm.message  = "Line 0 lost supply";
    vm.priority = app::presenter::kAlarmPriorityHigh;
    alerts.raise(vm);

    auto services =
        app::integration::buildIntegrationServices(config, logger, &alerts);
    ASSERT_NE(services.manager, nullptr);
    services.manager->startAll();

    const std::uint16_t port = httpBoundPort(*services.manager);
    ASSERT_GT(port, 0) << "HTTP backend was not registered by the bootstrap";

    httplib::Client client(kLoopback, port);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(2, 0);

    auto res = client.Get("/alarms");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, kOkStatus);

    const auto j = nlohmann::json::parse(res->body);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 1U);
    EXPECT_EQ(j[0].at("key").get<std::string>(), "equipment-0-offline");
    EXPECT_EQ(j[0].at("severity").get<std::string>(), "critical");

    services.manager->stopAll();
}

}  // namespace
