#include "src/integration/opcua/OpcUaBackend.h"

#include "src/core/LoggerBase.h"
#include "src/integration/opcua/OpcUaNodeMap.h"
#include "src/integration/opcua/OpcUaSecurityReport.h"
#include "src/integration/opcua/OpcUaServer.h"

#include <cassert>
#include <format>
#include <utility>

namespace app::integration::opcua {

OpcUaBackend::OpcUaBackend(std::unique_ptr<OpcUaServer> server,
                           std::unique_ptr<OpcUaNodeMap> nodeMap,
                           core::Logger& logger)
    : server_(std::move(server)),
      nodeMap_(std::move(nodeMap)),
      logger_(logger) {
    // Construction-time invariants. Throwing here surfaces wiring bugs
    // before the manager calls start() and gets a confusing failure.
    assert(server_ && "OpcUaBackend: server must be non-null");
    assert(nodeMap_ && "OpcUaBackend: nodeMap must be non-null");
}

OpcUaBackend::~OpcUaBackend() {
    // RAII teardown: stop() is idempotent and noexcept on the wrapped
    // pieces, so a destructor without an explicit stop() still leaves
    // the world consistent. We invoke stop() explicitly for the log
    // line + symmetry with TcpBackend / MqttClient.
    OpcUaBackend::stop();
}

void OpcUaBackend::start() {
    if (server_->isRunning()) {
        return;  // idempotent
    }

    logger_.info("OPC-UA backend starting");

    // Order matters: server up FIRST so the address-space build
    // operates against a live UA_Server, then wire model callbacks.
    server_->start();
    nodeMap_->registerNodes(*server_);
    nodeMap_->wire(*server_);

    logger_.info("OPC-UA backend started");
}

void OpcUaBackend::stop() {
    if (!server_->isRunning()) {
        return;  // idempotent
    }

    logger_.info("OPC-UA backend stopping");

    // Reverse order: silence callbacks BEFORE shutting down the I/O
    // thread, otherwise an in-flight model event could try to write
    // through a half-destroyed server.
    nodeMap_->unwire();
    server_->stop();

    logger_.info("OPC-UA backend stopped");
}

bool OpcUaBackend::isRunning() const {
    return server_->isRunning();
}

std::string OpcUaBackend::name() const {
    return "OPC-UA";
}

std::string OpcUaBackend::metricsSummary() const {
    if (!server_->isRunning()) return {};

    // Security is an OPTIONAL capability of a server, probed rather than
    // demanded: `OpcUaServer`'s own contract reserves itself for what a node
    // map and a command sink need and sends extension surfaces such as
    // encryption to their own interface. A server that does not implement
    // `OpcUaSecurityReport` simply has no security field in its summary,
    // which is why `MockOpcUaServer` needed no change for
    // REQ-INTEGRATION-011.
    const auto* security =
        dynamic_cast<const OpcUaSecurityReport*>(server_.get());
    if (security == nullptr) {
        return std::format("port {} | {} sessions",
                           server_->boundPort(),
                           server_->connectedSessions());
    }
    return std::format("port {} | {} sessions | security {}",
                       server_->boundPort(),
                       server_->connectedSessions(),
                       security->securityModeName());
}

BackendState OpcUaBackend::connectionState() const noexcept {
    // Server not running -> Disconnected.
    if (!server_->isRunning()) {
        return BackendState::Disconnected;
    }
    // Listening but no client sessions -> Connecting (idle listening).
    // Green `Connected` is reserved for "an actual peer is talking".
    if (server_->connectedSessions() == 0) {
        return BackendState::Connecting;
    }
    return BackendState::Connected;
}

}  // namespace app::integration::opcua
