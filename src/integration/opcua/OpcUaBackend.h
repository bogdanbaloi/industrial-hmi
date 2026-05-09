#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/integration/opcua/OpcUaConfig.h"

#include <memory>
#include <string>

namespace app::core    { class Logger; }

namespace app::integration::opcua {

class OpcUaServer;
class OpcUaNodeMap;

/// Aggregate `IntegrationBackend` for the OPC-UA channel.
///
/// Composes the two orthogonal pieces (server + node map) and adapts
/// them to the `IntegrationBackend` lifecycle (`start` / `stop` /
/// `isRunning` / `name`) that `IntegrationManager` orchestrates over
/// every protocol uniformly.
///
/// Composition over inheritance:
///   * Inherits ONLY from `IntegrationBackend` (the one polymorphic
///     hierarchy this class participates in).
///   * Holds `unique_ptr<OpcUaServer>` and `unique_ptr<OpcUaNodeMap>`
///     by interface, not by concrete. The constructor takes ownership
///     so callers can wire any combination -- the production GTK app
///     passes `Open62541Server` + `FactoryNodeMap`, unit tests pass
///     mocks.
///
/// Lifecycle order:
///   start():
///     1. server.start()              (bind + spin I/O thread)
///     2. nodeMap.registerNodes(srv)  (build address space)
///     3. nodeMap.wire(srv)           (subscribe to model signals)
///   stop():
///     1. nodeMap.unwire()            (silence callbacks)
///     2. server.stop()               (join I/O thread)
///   The order matters: `unwire` before `server.stop` so a model
///   callback can't race with the iterate loop tearing down. Same
///   reason `wire` runs after `registerNodes` -- callbacks reference
///   nodes that must already exist.
///
/// SOLID:
///   * S -- single job: orchestrate the two collaborators on the
///     `IntegrationBackend` lifecycle.
///   * O -- a non-manufacturing deployment swaps `FactoryNodeMap` for
///     a different `OpcUaNodeMap` at construction without touching
///     anything in this file.
///   * L -- substitutable for any `IntegrationBackend`; the manager
///     drives it identically with TcpBackend / MqttPublisher.
///   * I -- exposes only the base interface; OPC-UA specifics
///     (port number, address-space root) stay encapsulated.
///   * D -- depends on abstract collaborators injected via
///     constructor. Neither `Open62541Server` nor `FactoryNodeMap` is
///     visible at this level.
///
/// Rule of 5: copy / move deleted; backend identity is tied to its
/// owned server lifetime, which is non-relocatable.
class OpcUaBackend final : public IntegrationBackend {
public:
    /// @param server  Owned OPC-UA server abstraction.
    /// @param nodeMap Owned address-space strategy.
    /// @param logger  Used for start/stop traces. Must outlive this
    ///                backend.
    OpcUaBackend(std::unique_ptr<OpcUaServer> server,
                 std::unique_ptr<OpcUaNodeMap> nodeMap,
                 core::Logger& logger);

    ~OpcUaBackend() override;

    OpcUaBackend(const OpcUaBackend&)            = delete;
    OpcUaBackend& operator=(const OpcUaBackend&) = delete;
    OpcUaBackend(OpcUaBackend&&)                 = delete;
    OpcUaBackend& operator=(OpcUaBackend&&)      = delete;

    void start() override;
    void stop() override;

    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] std::string name() const override;

private:
    std::unique_ptr<OpcUaServer> server_;
    std::unique_ptr<OpcUaNodeMap> nodeMap_;
    core::Logger& logger_;
};

}  // namespace app::integration::opcua
