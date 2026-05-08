#pragma once

namespace app::integration::opcua {

class OpcUaServer;

/// Strategy that maps a domain model onto an OPC-UA address space.
///
/// Splits the OPC-UA backend into two orthogonal concerns:
///   1. The wire layer (`OpcUaServer`) -- generic, knows nothing about
///      the application domain.
///   2. The address-space layer (this) -- owns the node hierarchy and
///      the wiring from domain signals to node writes.
///
/// New domains drop in a new concrete:
///   * `FactoryNodeMap`         -- equipment / quality / work-unit
///                                 (the reference implementation here)
///   * `EnergyMeterNodeMap`     -- per-circuit kWh, peak demand
///   * `LabInstrumentNodeMap`   -- HPLC, mass-spec readings
///
/// Without changing `OpcUaServer` or `OpcUaBackend` -- classic
/// Open-Closed via the strategy pattern.
///
/// SOLID:
///   * S -- one job: domain-to-node-tree mapping. No socket I/O, no
///     model state machine, no UI.
///   * O -- new domain = new subclass. The base interface and
///     existing maps stay closed for modification.
///   * L -- the backend treats every map identically through
///     register / wire / unwire.
///   * I -- 3 methods, each with a distinct lifecycle stage. Mocking
///     in tests is trivial.
///   * D -- depends only on `OpcUaServer&`. Concrete maps may also
///     depend on a domain model (`ProductionModel&` for the factory
///     case), injected via their concrete constructors.
///
/// Threading: `registerNodes` and `wire` run on the construction
/// thread (typically main). After that, model callbacks may fire on
/// any thread; concrete maps must marshal writes through `OpcUaServer`,
/// which is documented as thread-safe for `writeXxx` calls.
class OpcUaNodeMap {
public:
    virtual ~OpcUaNodeMap() = default;

    OpcUaNodeMap(const OpcUaNodeMap&)            = delete;
    OpcUaNodeMap& operator=(const OpcUaNodeMap&) = delete;
    OpcUaNodeMap(OpcUaNodeMap&&)                 = delete;
    OpcUaNodeMap& operator=(OpcUaNodeMap&&)      = delete;

    /// Build the static address space on the server. Called once
    /// immediately after `server.start()` returns. Implementations
    /// add Object / Variable / Method nodes via the server-specific
    /// helpers exposed through the concrete impl (open62541 calls
    /// here would defeat DIP -- the concrete impl wraps them).
    virtual void registerNodes(OpcUaServer& server) = 0;

    /// Connect domain-model signals to node writes. Called once after
    /// `registerNodes`. Concrete maps subscribe to their domain model
    /// (e.g. `ProductionModel`) and translate each signal into one or
    /// more `OpcUaServer::writeXxx` calls.
    virtual void wire(OpcUaServer& server) = 0;

    /// Disconnect domain-model signals. Called once before
    /// `server.stop()`. Implementations cancel their model
    /// subscriptions so callbacks don't fire against a server that's
    /// in the middle of tearing down.
    virtual void unwire() noexcept = 0;

protected:
    OpcUaNodeMap() = default;
};

}  // namespace app::integration::opcua
