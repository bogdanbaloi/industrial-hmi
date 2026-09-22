// [utest->req~integration-011~1]
// Covers REQ-INTEGRATION-011 (OPC-UA Sign&Encrypt) at the endpoint: a server
// configured for security offers exactly one endpoint, Basic256Sha256 with
// UA_MessageSecurityMode_SignAndEncrypt, and a plaintext client is refused
// rather than quietly served (ADR-0031).
//
// This test links open62541 and binds a real loopback socket, which is why it
// lives behind BUILD_OPCUA_BACKEND next to the other Open62541 integration
// tests rather than with the mock-based unit tests.

#include "src/core/LoggerImpl.h"
#include "src/core/StartupErrors.h"
#include "src/integration/opcua/OpcUaConfig.h"
#include "src/integration/opcua/OpcUaSecurityOptions.h"
#include "src/integration/opcua/Open62541Server.h"

#include <gtest/gtest.h>

#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

using app::core::TlsMaterialError;
using app::integration::opcua::OpcUaConfig;
using app::integration::opcua::OpcUaSecurityOptions;
using app::integration::opcua::Open62541Server;

namespace {

// The fixture directory is passed in by CMake so the test finds the committed
// DER files regardless of the working directory ctest runs it from.
constexpr std::string_view kFixtureDir = INDUSTRIAL_HMI_OPCUA_FIXTURE_DIR;

/// Fixed-but-unusual port, chosen on the same reasoning as
/// Open62541ServerIntegrationTest's 14840: outside the 4840-4842 range vendor
/// PLCs default to, and offset from that test's port so the two can run
/// concurrently under `ctest -j`.
constexpr std::uint16_t kTestPort = 14842;

/// open62541's connect can race the server's accept loop in the very first
/// millisecond after start(). The same settle delay the sibling integration
/// tests use.
constexpr std::chrono::milliseconds kSettleDelay{200};

/// What securityModeName() and the metrics summary must report once the
/// material has loaded. Spelled here rather than shared with the production
/// header on purpose: a test that imported the constant would still pass if
/// the constant itself changed, and this string is an operator-facing
/// contract.
constexpr std::string_view kSignAndEncryptModeName = "sign+encrypt";

std::string fixture(std::string_view name) {
    return std::string(kFixtureDir) + "/" + std::string(name);
}

/// Server security material that is expected to load cleanly.
OpcUaSecurityOptions validServerSecurity() {
    OpcUaSecurityOptions security;
    security.enabled         = true;
    security.configKeyPrefix = "network.opcua.server.security";
    security.certPath        = fixture("server.der");
    security.privateKeyPath  = fixture("server-key.der");
    security.trustListDir    = fixture("trustlist");
    return security;
}

/// A server config whose applicationUri MATCHES the URI inside the committed
/// server certificate. OPC-UA ties the two together and open62541 enforces
/// it, so a mismatch here would fail for a reason that has nothing to do with
/// what each test is asserting.
OpcUaConfig securedConfig(std::uint16_t port) {
    OpcUaConfig config;
    config.port            = port;
    config.applicationUri  = "urn:industrial-hmi:server";
    config.applicationName = "Industrial HMI OPC-UA Server";
    config.security        = validServerSecurity();
    return config;
}

}  // namespace

TEST(Open62541ServerSecurityTest, StartsWithSignAndEncryptWhenSecurityEnabled) {
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};
    Open62541Server server{securedConfig(kTestPort), logger};

    // Reported before start() too: the mode is a property of the material
    // that loaded, not of the socket.
    EXPECT_EQ(server.securityModeName(), kSignAndEncryptModeName);

    ASSERT_NO_THROW(server.start());
    EXPECT_TRUE(server.isRunning());
    EXPECT_EQ(server.securityModeName(), kSignAndEncryptModeName);

    std::this_thread::sleep_for(kSettleDelay);
    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST(Open62541ServerSecurityTest, ReportsNoSecurityWhenSecurityDisabled) {
    // The unchanged path. A deployment that never asked for security must
    // still get the plaintext server it had before REQ-INTEGRATION-011, and
    // must say so rather than leaving the field blank.
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};
    OpcUaConfig config;
    config.port = kTestPort + 1;

    Open62541Server server{std::move(config), logger};
    EXPECT_EQ(server.securityModeName(), "none");

    ASSERT_NO_THROW(server.start());
    EXPECT_TRUE(server.isRunning());
    server.stop();
}

TEST(Open62541ServerSecurityTest,
     ConstructorThrowsOnBadCertBeforeAnyServerExists) {
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};
    OpcUaConfig config = securedConfig(kTestPort + 2);
    config.security.certPath = fixture("no-such-certificate.der");

    // Assert ONLY the throw and its type. There is deliberately no object to
    // inspect afterwards: that is the property under test. A server that
    // cannot have the security it was configured for must never reach the
    // point of binding a port, so there is nothing to ask about its state.
    EXPECT_THROW(
        { const Open62541Server server(std::move(config), logger); },
        TlsMaterialError);
}

TEST(Open62541ServerSecurityTest, RejectsPlaintextClientWhenSignAndEncryptRequired) {
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};
    Open62541Server server{securedConfig(kTestPort + 3), logger};
    ASSERT_NO_THROW(server.start());
    ASSERT_TRUE(server.isRunning());
    std::this_thread::sleep_for(kSettleDelay);

    // A stock plaintext client: UA_ClientConfig_setDefault leaves it on
    // SecurityPolicy#None. Against a secured server there is no None endpoint
    // to select, so the connect must fail. This is the assertion the whole
    // feature exists for: without the endpoint-list reduction in
    // applySignAndEncrypt, open62541 would still offer a None endpoint
    // alongside the secure ones and this client would sail straight in.
    UA_Client* client = UA_Client_new();
    ASSERT_NE(client, nullptr);
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(kTestPort + 3);
    const UA_StatusCode rc = UA_Client_connect(client, endpoint.c_str());
    EXPECT_NE(rc, UA_STATUSCODE_GOOD)
        << "a plaintext client connected to a SignAndEncrypt-only endpoint";

    UA_Client_delete(client);
    server.stop();
}

// The operator-facing summary that folds this mode into OpcUaBackend's
// metrics is covered in OpcUaBackendTest, which can exercise both the
// capable and the incapable server through mocks without a socket.
