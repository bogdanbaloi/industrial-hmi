// End-to-end integration test for Open62541Server.
//
// Spins up a real `Open62541Server`, builds a small address space,
// then opens a real open62541 `UA_Client`, connects, browses, and
// reads back the variable. Verifies the full round-trip:
//
//   addObject + addXxxVariable + writeXxx
//                    \__________________/
//                              |
//                              v
//                     wire format on loopback
//                              |
//                              v
//                  UA_Client_readValueAttribute
//
// Anything that breaks the wire encoding (variant tagging, NodeId
// resolution, browse-path translation) surfaces here -- the unit
// tests can't see it because they stop at the interface.
//
// Why a separate executable: this test DOES link open62541 (the
// only test that does). The mock-based unit tests stay open62541-
// free so they keep running on a build without BUILD_OPCUA_BACKEND.

#include "src/core/LoggerImpl.h"
#include "src/integration/opcua/Open62541Server.h"

#include <gtest/gtest.h>

#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

using app::integration::opcua::OpcUaConfig;
using app::integration::opcua::Open62541Server;

namespace {

/// Fixed-but-unusual port to keep the test out of the typical OPC-UA
/// industrial range (4840-4842 are common defaults in vendor PLCs)
/// and the IANA-registered service range. 14840 is registered to
/// nothing and the high-bit is set so firewalls rarely whitelist it
/// for outbound, leaving it free in CI containers.
constexpr std::uint16_t kTestPort = 14840;

/// open62541's connect can race the server's accept loop the very
/// first millisecond after start(). One tiny sleep matches the same
/// pattern used by MqttClientTest and keeps the test stable
/// across CI runners with different scheduler latency.
constexpr std::chrono::milliseconds kSettleDelay{200};

class Open62541ServerIntegrationTest : public ::testing::Test {
protected:
    Open62541ServerIntegrationTest()
        : logger_{std::make_unique<app::core::ConsoleLogger>()} {
        OpcUaConfig cfg;
        cfg.port = kTestPort;
        cfg.applicationUri = "urn:test:open62541-integration";
        cfg.applicationName = "Open62541 Integration Test";
        server_ = std::make_unique<Open62541Server>(std::move(cfg), logger_);
    }

    app::core::Logger logger_;
    std::unique_ptr<Open62541Server> server_;
};

TEST_F(Open62541ServerIntegrationTest, ClientReadsBackWrittenFloat) {
    // 1. Bring the server up + populate the address space.
    server_->start();
    ASSERT_TRUE(server_->isRunning());
    ASSERT_TRUE(server_->addObject("Plant"));
    ASSERT_TRUE(server_->addFloatVariable("Plant/Throughput", 0.0F));

    // 2. Push a known value through the server's typed write.
    constexpr float kExpected = 42.5F;
    ASSERT_TRUE(server_->writeFloat("Plant/Throughput", kExpected));

    // Give the I/O thread a tick to publish the write before the
    // client comes asking.
    std::this_thread::sleep_for(kSettleDelay);

    // 3. Spin up a real OPC-UA client + connect over loopback.
    UA_Client* client = UA_Client_new();
    ASSERT_NE(client, nullptr);
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));

    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(kTestPort);
    UA_StatusCode rc = UA_Client_connect(client, endpoint.c_str());
    ASSERT_EQ(rc, UA_STATUSCODE_GOOD)
        << "client connect failed: " << UA_StatusCode_name(rc);

    // 4. Browse-path translate to the variable's NodeId. We use the
    //    same path encoding the server uses internally (Objects ->
    //    Plant -> Throughput, all in namespace 1).
    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);

    UA_RelativePathElement elements[2];
    UA_RelativePathElement_init(&elements[0]);
    elements[0].referenceTypeId =
        UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    elements[0].includeSubtypes = true;
    elements[0].targetName = UA_QUALIFIEDNAME(1, const_cast<char*>("Plant"));

    UA_RelativePathElement_init(&elements[1]);
    elements[1].referenceTypeId =
        UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    elements[1].includeSubtypes = true;
    elements[1].targetName =
        UA_QUALIFIEDNAME(1, const_cast<char*>("Throughput"));

    bp.relativePath.elements = elements;
    bp.relativePath.elementsSize = 2;

    UA_TranslateBrowsePathsToNodeIdsRequest req;
    UA_TranslateBrowsePathsToNodeIdsRequest_init(&req);
    req.browsePaths = &bp;
    req.browsePathsSize = 1;

    UA_TranslateBrowsePathsToNodeIdsResponse resp =
        UA_Client_Service_translateBrowsePathsToNodeIds(client, req);
    ASSERT_EQ(resp.responseHeader.serviceResult, UA_STATUSCODE_GOOD);
    ASSERT_GT(resp.resultsSize, 0U);
    ASSERT_GT(resp.results[0].targetsSize, 0U);

    UA_NodeId nodeId;
    UA_NodeId_copy(&resp.results[0].targets[0].targetId.nodeId, &nodeId);

    // 5. Read the variable back through the wire. The roundtrip
    //    proves that addFloatVariable + writeFloat actually moved
    //    bytes through open62541's variant codec correctly.
    UA_Variant value;
    UA_Variant_init(&value);
    rc = UA_Client_readValueAttribute(client, nodeId, &value);
    EXPECT_EQ(rc, UA_STATUSCODE_GOOD);
    EXPECT_TRUE(UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT]));
    if (rc == UA_STATUSCODE_GOOD &&
        UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
        const auto observed = *static_cast<const float*>(value.data);
        EXPECT_FLOAT_EQ(observed, kExpected);
    }

    UA_Variant_clear(&value);
    UA_NodeId_clear(&nodeId);
    UA_TranslateBrowsePathsToNodeIdsResponse_clear(&resp);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
}

}  // namespace
