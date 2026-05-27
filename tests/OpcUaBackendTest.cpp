// [utest->req~integration-004~1]
// Covers REQ-INTEGRATION-004 (OPC-UA server + client).
//
// Tests for app::integration::opcua::OpcUaBackend.
//
// The facade composes OpcUaServer + OpcUaNodeMap behind the
// IntegrationBackend lifecycle. We mock both collaborators with
// gmock to verify two things the integration layer DEPENDS on:
//
//   1. Lifecycle ORDER: start() = server.start -> registerNodes -> wire
//                       stop()  = unwire       -> server.stop
//      A regression that swaps these would silently break the model
//      callbacks (writes against a half-built address space, callbacks
//      racing a tearing-down server).
//
//   2. Idempotence: start() called twice must not double-wire;
//                   stop() called on a never-started backend must not
//                   crash.
//
// No sockets, no open62541 -- pure interface-level plumbing.

#include "src/integration/opcua/OpcUaBackend.h"

#include "src/core/LoggerImpl.h"
#include "src/integration/opcua/OpcUaNodeMap.h"
#include "src/integration/opcua/OpcUaServer.h"
#include "tests/MockOpcUaServer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

using app::integration::opcua::OpcUaBackend;
using app::integration::opcua::OpcUaNodeMap;
using app::integration::opcua::OpcUaServer;
using app::integration::opcua::testing::MockOpcUaServer;

using ::testing::InSequence;
using ::testing::Ref;
using ::testing::Return;

namespace {

class MockOpcUaNodeMap final : public OpcUaNodeMap {
public:
    MockOpcUaNodeMap()           = default;
    ~MockOpcUaNodeMap() override = default;

    MockOpcUaNodeMap(const MockOpcUaNodeMap&)            = delete;
    MockOpcUaNodeMap& operator=(const MockOpcUaNodeMap&) = delete;
    MockOpcUaNodeMap(MockOpcUaNodeMap&&)                 = delete;
    MockOpcUaNodeMap& operator=(MockOpcUaNodeMap&&)      = delete;

    MOCK_METHOD(void, registerNodes, (OpcUaServer&), (override));
    MOCK_METHOD(void, wire, (OpcUaServer&), (override));
    MOCK_METHOD(void, unwire, (), (noexcept, override));
};

/// Build a backend with mock collaborators. The mocks are constructed
/// inside the function so each test gets its own pair; ownership is
/// transferred to the backend, so we capture raw pointers before the
/// move to keep using gmock matchers in test bodies.
struct Fixture {
    MockOpcUaServer*  server  = nullptr;
    MockOpcUaNodeMap* nodeMap = nullptr;
    app::core::Logger logger{std::make_unique<app::core::ConsoleLogger>()};
    std::unique_ptr<OpcUaBackend> backend;
};

Fixture makeFixture() {
    Fixture f;
    auto server  = std::make_unique<MockOpcUaServer>();
    auto nodeMap = std::make_unique<MockOpcUaNodeMap>();
    f.server  = server.get();
    f.nodeMap = nodeMap.get();
    f.backend = std::make_unique<OpcUaBackend>(
        std::move(server), std::move(nodeMap), f.logger);
    return f;
}

TEST(OpcUaBackendTest, NameIsStableShortIdentifier) {
    Fixture f = makeFixture();
    // Destructor will call isRunning() through stop()'s guard.
    EXPECT_CALL(*f.server, isRunning()).WillRepeatedly(Return(false));
    EXPECT_EQ(f.backend->name(), "OPC-UA");
}

TEST(OpcUaBackendTest, IsRunningReflectsServerState) {
    Fixture f = makeFixture();

    // The backend forwards isRunning() verbatim. The destructor will
    // call it again on teardown, so end with WillRepeatedly so the
    // destructor's stop()-guard call doesn't trip the mock.
    EXPECT_CALL(*f.server, isRunning())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_FALSE(f.backend->isRunning());
    EXPECT_TRUE(f.backend->isRunning());
}

TEST(OpcUaBackendTest, ConnectionStateReflectsListeningVsActiveSessions) {
    using app::integration::BackendState;
    Fixture f = makeFixture();

    // Server stopped -> Disconnected.
    EXPECT_CALL(*f.server, isRunning())
        .WillOnce(Return(false))
        // Server up, no sessions -> Connecting (listening idle).
        .WillOnce(Return(true))
        // Server up, sessions attached -> Connected.
        .WillOnce(Return(true))
        // Catch-all for the destructor's stop()-guard call.
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*f.server, connectedSessions())
        .WillOnce(Return(0U))
        .WillOnce(Return(2U))
        .WillRepeatedly(Return(0U));

    EXPECT_EQ(f.backend->connectionState(), BackendState::Disconnected);
    EXPECT_EQ(f.backend->connectionState(), BackendState::Connecting);
    EXPECT_EQ(f.backend->connectionState(), BackendState::Connected);
}

TEST(OpcUaBackendTest, StartCallsCollaboratorsInLifecycleOrder) {
    Fixture f = makeFixture();

    // gmock matching is LIFO: the catch-all is consulted only after
    // the specific RetiresOnSaturation expectation below saturates.
    // The catch-all then absorbs the destructor's stop()-guard call
    // without complaint.
    EXPECT_CALL(*f.server, isRunning()).WillRepeatedly(Return(false));

    {
        InSequence seq;
        EXPECT_CALL(*f.server, isRunning())
            .WillOnce(Return(false))
            .RetiresOnSaturation();
        EXPECT_CALL(*f.server, start());
        EXPECT_CALL(*f.nodeMap, registerNodes(Ref(*f.server)));
        EXPECT_CALL(*f.nodeMap, wire(Ref(*f.server)));
    }

    f.backend->start();
}

TEST(OpcUaBackendTest, StopReversesLifecycleOrder) {
    Fixture f = makeFixture();

    // Catch-all for the destructor's stop()-guard call after the
    // sequenced expectations have all retired.
    EXPECT_CALL(*f.server, isRunning()).WillRepeatedly(Return(false));

    {
        InSequence seq;
        // start(): false -> body runs.
        EXPECT_CALL(*f.server, isRunning())
            .WillOnce(Return(false))
            .RetiresOnSaturation();
        EXPECT_CALL(*f.server, start());
        EXPECT_CALL(*f.nodeMap, registerNodes(Ref(*f.server)));
        EXPECT_CALL(*f.nodeMap, wire(Ref(*f.server)));
        // stop(): true -> teardown runs in reverse order.
        EXPECT_CALL(*f.server, isRunning())
            .WillOnce(Return(true))
            .RetiresOnSaturation();
        EXPECT_CALL(*f.nodeMap, unwire());
        EXPECT_CALL(*f.server, stop());
    }

    f.backend->start();
    f.backend->stop();
}

TEST(OpcUaBackendTest, StartIsIdempotent) {
    Fixture f = makeFixture();

    // First start(): false -> body runs. Second start(): true -> bails.
    // Destructor's stop(): false -> short-circuit. WillRepeatedly
    // covers the destructor path so we don't have to count exactly.
    EXPECT_CALL(*f.server, isRunning())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*f.server, start()).Times(1);
    EXPECT_CALL(*f.nodeMap, registerNodes(Ref(*f.server))).Times(1);
    EXPECT_CALL(*f.nodeMap, wire(Ref(*f.server))).Times(1);

    f.backend->start();
    f.backend->start();
}

TEST(OpcUaBackendTest, StopWithoutStartIsNoOp) {
    Fixture f = makeFixture();

    // Both the explicit stop() and the destructor's stop() see
    // isRunning() == false and short-circuit. Neither unwire nor
    // server.stop should ever be called.
    EXPECT_CALL(*f.server, isRunning()).WillRepeatedly(Return(false));
    EXPECT_CALL(*f.nodeMap, unwire()).Times(0);
    EXPECT_CALL(*f.server, stop()).Times(0);

    f.backend->stop();
}

}  // namespace
