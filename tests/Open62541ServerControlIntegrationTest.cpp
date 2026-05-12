// End-to-end test for inbound OPC-UA control on Open62541Server.
//
// Spawns a real `Open62541Server` with a method node and a writable
// bool variable, then opens a real `UA_Client` and:
//
//   1. invokes the method via UA_Client_call -- the registered sink
//      sees the parameterless command dispatch.
//   2. writes `true` to the bool variable -- the same sink sees the
//      `onBoolWrite(path, true)` callback.
//
// Validates the inbound wire format end-to-end: any regression in
// method-callback registration, value-callback wiring, NodeId
// resolution, or variant decoding shows up here that the mock-based
// FactoryCommandSinkTest can't see.

#include "src/core/LoggerImpl.h"
#include "src/integration/opcua/FactoryCommandSink.h"
#include "src/integration/opcua/OpcUaCommandSink.h"
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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using app::integration::opcua::OpcUaCommandSink;
using app::integration::opcua::OpcUaConfig;
using app::integration::opcua::Open62541Server;

namespace {

/// Fresh high port -- same rationale as the other OPC-UA integration
/// tests: stay clear of vendor PLCs and IANA defaults.
constexpr std::uint16_t kTestPort = 14842;

/// Sleep the OPC-UA stack needs after `start()` before a fresh
/// client can connect; matches the other integration tests.
constexpr std::chrono::milliseconds kSettleDelay{200};

/// Capturing sink. Records every command + bool write so the test
/// can assert on exact dispatch order and arguments.
class RecordingSink final : public OpcUaCommandSink {
public:
    void onCommand(std::string_view commandName) noexcept override {
        const std::scoped_lock lock(mutex_);
        commands_.emplace_back(commandName);
    }
    void onBoolWrite(std::string_view nodePath,
                     bool value) noexcept override {
        const std::scoped_lock lock(mutex_);
        boolWrites_.emplace_back(std::string{nodePath}, value);
    }

    [[nodiscard]] std::vector<std::string> commands() const {
        const std::scoped_lock lock(mutex_);
        return commands_;
    }
    [[nodiscard]] std::vector<std::pair<std::string, bool>> boolWrites() const {
        const std::scoped_lock lock(mutex_);
        return boolWrites_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> commands_;
    std::vector<std::pair<std::string, bool>> boolWrites_;
};

}  // namespace

TEST(Open62541ServerControlIntegrationTest, MethodCallReachesSink) {
    auto logger = app::core::Logger{
        std::make_unique<app::core::ConsoleLogger>()};
    OpcUaConfig cfg;
    cfg.port = kTestPort;
    cfg.applicationUri  = "urn:test:opcua-server-control";
    cfg.applicationName = "OPC-UA Server Control Test";
    Open62541Server server(std::move(cfg), logger);

    RecordingSink sink;
    server.start();
    ASSERT_TRUE(server.isRunning());
    ASSERT_TRUE(server.addObject("Factory"));
    ASSERT_TRUE(server.addObject("Factory/Commands"));
    ASSERT_TRUE(server.addMethod("Factory/Commands/StartProduction", sink));

    std::this_thread::sleep_for(kSettleDelay);

    UA_Client* client = UA_Client_new();
    ASSERT_NE(client, nullptr);
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(kTestPort);
    ASSERT_EQ(UA_Client_connect(client, endpoint.c_str()),
              UA_STATUSCODE_GOOD);

    // Resolve both the parent object and the method node. OPC-UA's
    // Call service requires the objectId to be the actual parent of
    // the method; passing the root Objects folder fails with
    // BadMethodInvalid.
    auto resolveBrowsePath =
        [&](const std::vector<const char*>& names) -> UA_NodeId {
        UA_BrowsePath path;
        UA_BrowsePath_init(&path);
        path.startingNode = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        std::vector<UA_RelativePathElement> elements(names.size());
        for (std::size_t i = 0; i < names.size(); ++i) {
            UA_RelativePathElement_init(&elements[i]);
            elements[i].referenceTypeId =
                UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
            elements[i].includeSubtypes = true;
            elements[i].targetName =
                UA_QUALIFIEDNAME(1, const_cast<char*>(names[i]));
        }
        path.relativePath.elements = elements.data();
        path.relativePath.elementsSize = elements.size();

        UA_TranslateBrowsePathsToNodeIdsRequest req;
        UA_TranslateBrowsePathsToNodeIdsRequest_init(&req);
        req.browsePathsSize = 1;
        req.browsePaths = &path;
        UA_TranslateBrowsePathsToNodeIdsResponse resp =
            UA_Client_Service_translateBrowsePathsToNodeIds(client, req);
        UA_NodeId out = UA_NODEID_NULL;
        if (resp.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
            resp.resultsSize > 0 &&
            resp.results[0].statusCode == UA_STATUSCODE_GOOD &&
            resp.results[0].targetsSize > 0) {
            UA_NodeId_copy(&resp.results[0].targets[0].targetId.nodeId,
                           &out);
        }
        UA_TranslateBrowsePathsToNodeIdsResponse_clear(&resp);
        return out;
    };

    UA_NodeId parentId =
        resolveBrowsePath({"Factory", "Commands"});
    UA_NodeId methodId =
        resolveBrowsePath({"Factory", "Commands", "StartProduction"});
    ASSERT_FALSE(UA_NodeId_isNull(&parentId));
    ASSERT_FALSE(UA_NodeId_isNull(&methodId));

    UA_CallMethodRequest call;
    UA_CallMethodRequest_init(&call);
    call.objectId           = parentId;
    call.methodId           = methodId;
    call.inputArgumentsSize = 0;
    call.inputArguments     = nullptr;

    UA_CallRequest callReq;
    UA_CallRequest_init(&callReq);
    callReq.methodsToCallSize = 1;
    callReq.methodsToCall     = &call;
    UA_CallResponse callResp = UA_Client_Service_call(client, callReq);
    EXPECT_EQ(callResp.responseHeader.serviceResult, UA_STATUSCODE_GOOD);
    ASSERT_GE(callResp.resultsSize, 1U);
    EXPECT_EQ(callResp.results[0].statusCode, UA_STATUSCODE_GOOD);
    UA_CallResponse_clear(&callResp);

    // Drain a few publish cycles so the dispatch lands.
    std::this_thread::sleep_for(kSettleDelay);

    const auto commands = sink.commands();
    EXPECT_GE(commands.size(), 1U);
    if (!commands.empty()) {
        EXPECT_EQ(commands[0], "StartProduction");
    }

    UA_NodeId_clear(&parentId);
    UA_NodeId_clear(&methodId);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}

TEST(Open62541ServerControlIntegrationTest,
     BoolVariableWriteCallbackReachesSink) {
    auto logger = app::core::Logger{
        std::make_unique<app::core::ConsoleLogger>()};
    OpcUaConfig cfg;
    cfg.port = kTestPort + 1;  // distinct from the first test
    cfg.applicationUri  = "urn:test:opcua-server-control";
    cfg.applicationName = "OPC-UA Server Control Test";
    Open62541Server server(std::move(cfg), logger);

    RecordingSink sink;
    server.start();
    ASSERT_TRUE(server.isRunning());
    ASSERT_TRUE(server.addObject("Factory"));
    ASSERT_TRUE(server.addObject("Factory/EquipmentLines"));
    ASSERT_TRUE(server.addObject("Factory/EquipmentLines/Line0"));
    ASSERT_TRUE(server.addBoolVariableWithWriteCallback(
        "Factory/EquipmentLines/Line0/Enabled",
        /*initial=*/true, sink));

    std::this_thread::sleep_for(kSettleDelay);

    UA_Client* client = UA_Client_new();
    ASSERT_NE(client, nullptr);
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    const std::string endpoint =
        "opc.tcp://127.0.0.1:" + std::to_string(kTestPort + 1);
    ASSERT_EQ(UA_Client_connect(client, endpoint.c_str()),
              UA_STATUSCODE_GOOD);

    // Resolve the variable node.
    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_RelativePathElement elements[4];
    const char* names[4] = {"Factory", "EquipmentLines", "Line0", "Enabled"};
    for (int i = 0; i < 4; ++i) {
        UA_RelativePathElement_init(&elements[i]);
        elements[i].referenceTypeId =
            UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
        elements[i].includeSubtypes = true;
        elements[i].targetName =
            UA_QUALIFIEDNAME(1, const_cast<char*>(names[i]));
    }
    bp.relativePath.elements = elements;
    bp.relativePath.elementsSize = 4;

    UA_TranslateBrowsePathsToNodeIdsRequest req;
    UA_TranslateBrowsePathsToNodeIdsRequest_init(&req);
    req.browsePathsSize = 1;
    req.browsePaths = &bp;
    UA_TranslateBrowsePathsToNodeIdsResponse resp =
        UA_Client_Service_translateBrowsePathsToNodeIds(client, req);
    ASSERT_EQ(resp.responseHeader.serviceResult, UA_STATUSCODE_GOOD);
    ASSERT_GT(resp.results[0].targetsSize, 0U);
    UA_NodeId nodeId;
    UA_NodeId_copy(&resp.results[0].targets[0].targetId.nodeId, &nodeId);
    UA_TranslateBrowsePathsToNodeIdsResponse_clear(&resp);

    UA_Boolean newValue = false;
    UA_Variant value;
    UA_Variant_init(&value);
    UA_Variant_setScalarCopy(&value, &newValue,
                              &UA_TYPES[UA_TYPES_BOOLEAN]);
    const UA_StatusCode rc = UA_Client_writeValueAttribute(
        client, nodeId, &value);
    EXPECT_EQ(rc, UA_STATUSCODE_GOOD);
    UA_Variant_clear(&value);

    std::this_thread::sleep_for(kSettleDelay);

    const auto writes = sink.boolWrites();
    EXPECT_GE(writes.size(), 1U);
    if (!writes.empty()) {
        EXPECT_EQ(writes[0].first,
                  "Factory/EquipmentLines/Line0/Enabled");
        EXPECT_FALSE(writes[0].second);
    }

    UA_NodeId_clear(&nodeId);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    server.stop();
}
