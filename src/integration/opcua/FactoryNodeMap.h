#pragma once

#include "src/integration/opcua/OpcUaNodeMap.h"

#include <cstdint>

namespace app::core    { class Logger; }
namespace app::model   { class ProductionModel; }

namespace app::integration::opcua {

class OpcUaCommandSink;


/// Reference `OpcUaNodeMap` for the manufacturing-line domain.
///
/// Maps `ProductionModel` signals onto an OPC-UA address space rooted
/// at `Objects/Factory/`:
///
///   Objects/Factory/
///     State                                 Int32   (SystemState enum)
///     EquipmentLines/
///       Line<id>/Status                     Int32
///       Line<id>/SupplyLevel                Int32  (0..100 percent)
///       Line<id>/Message                    String
///     QualityCheckpoints/
///       Checkpoint<id>/Name                 String
///       Checkpoint<id>/Status               Int32
///       Checkpoint<id>/PassRate             Float
///       Checkpoint<id>/UnitsInspected       Int32
///       Checkpoint<id>/DefectsFound         Int32
///     WorkUnit/
///       Id                                  String
///       ProductId                           String
///       CompletedOperations                 Int32
///       TotalOperations                     Int32
///
/// Equipment / checkpoint subtrees are created on demand the first
/// time the model emits a callback for that id; the static address
/// space (Factory + the two collection folders + WorkUnit) is created
/// up front in `registerNodes`.
///
/// SOLID:
///   * S -- one responsibility: translate manufacturing-domain events
///     into `OpcUaServer` calls. No protocol details, no UI.
///   * O -- additional domain concepts (e.g. operator login, shift
///     rota) extend `ProductionModel` first; this map gains a new
///     subscription without affecting the OPC-UA layer.
///   * L -- substitutable for any other `OpcUaNodeMap`.
///   * I -- exposes only the base interface methods.
///   * D -- depends on `ProductionModel` (interface) and `OpcUaServer`
///     (interface), not on `SimulatedModel` or `Open62541Server`.
///
/// Threading: `wire()` registers callbacks that the model dispatches
/// from arbitrary threads (simulator timer thread, integration manager
/// boot thread). `OpcUaServer::writeXxx` is documented MT-safe in the
/// base interface.
///
/// Rule of 5: copy / move deleted. The map is owned by composition
/// (`OpcUaBackend`) and never relocated.
class FactoryNodeMap final : public OpcUaNodeMap {
public:
    /// Read-only deployment: the map publishes Model state but the
    /// server exposes no inbound control surface.
    FactoryNodeMap(model::ProductionModel& production,
                   core::Logger& logger);

    /// Bidirectional deployment: `registerNodes` additionally creates
    /// `Factory/Commands/{Start,Stop,Reset,Calibrate}Production` as
    /// OPC-UA Methods plus a writable `Factory/EquipmentLines/Line<id>/Enabled`
    /// Boolean per equipment slot. Inbound events flow through `sink`,
    /// which must outlive this map.
    FactoryNodeMap(model::ProductionModel& production,
                   core::Logger& logger,
                   OpcUaCommandSink& sink);

    ~FactoryNodeMap() override;

    FactoryNodeMap(const FactoryNodeMap&)            = delete;
    FactoryNodeMap& operator=(const FactoryNodeMap&) = delete;
    FactoryNodeMap(FactoryNodeMap&&)                 = delete;
    FactoryNodeMap& operator=(FactoryNodeMap&&)      = delete;

    void registerNodes(OpcUaServer& server) override;
    void wire(OpcUaServer& server) override;
    void unwire() noexcept override;

private:
    /// Number of equipment slots the writable Enabled surface covers.
    /// Matches `SimulatedModel`'s three lines; future model
    /// extensions would bump this in lockstep with the model.
    static constexpr std::uint32_t kEquipmentCount = 3U;

    /// Register the inbound control surface on `server`. Called from
    /// `registerNodes` only when a sink was provided at construction.
    void registerCommandSurface(OpcUaServer& server);

    model::ProductionModel& production_;
    core::Logger& logger_;
    OpcUaCommandSink* sink_ = nullptr;  // optional; null = read-only
    bool wired_ = false;
};

}  // namespace app::integration::opcua
