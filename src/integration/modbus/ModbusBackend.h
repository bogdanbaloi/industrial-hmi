#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/integration/modbus/ModbusClient.h"
#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusPollLoop.h"
#include "src/integration/modbus/ModbusRegisterMap.h"

#include <memory>
#include <string>

namespace app::core  { class Logger; }
namespace app::model { class ProductionModel; }

namespace app::integration::modbus {

/// Aggregate `IntegrationBackend` for the Modbus master channel.
///
/// Composes the four orthogonal pieces (client + register map +
/// ingest bridge + poll loop) and adapts them to the
/// `IntegrationBackend` lifecycle that `IntegrationManager`
/// orchestrates uniformly across every protocol.
///
/// Composition stack:
///
///   ModbusBackend (this class)
///       owns ModbusClient            (TCP transport, implements ModbusReader)
///       owns ModbusIngestBridge       (wire -> Model setter dispatch)
///       owns ModbusRegisterMap        (which registers to poll)
///       owns ModbusPollLoop           (jthread cadence driver)
///
/// Composition over inheritance: only inherits from
/// `IntegrationBackend`. The four collaborators are concrete classes
/// (or owned by-value structs); they're already SOC-clean and don't
/// need their own polymorphism just to be reused here.
///
/// Lifecycle order:
///   start():
///     1. pollLoop.start()           (jthread spawned, first poll
///                                    issues the connect)
///   stop():
///     1. pollLoop.stop()            (request_stop + join; ModbusClient
///                                    socket closed via its dtor)
///
/// The poll loop is the only thread-owning piece, so start/stop are
/// trivial. ModbusClient connects lazily on the first read; if the
/// slave is offline at startup, the loop logs failures + counters,
/// and connects when the slave comes back -- no special "reconnect"
/// thread needed.
///
/// SOLID:
///   * S -- single job: orchestrate the four pieces on the
///     IntegrationBackend lifecycle. No domain awareness, no PDU
///     formatting, no GTK.
///   * O -- a non-manufacturing deployment swaps the register map +
///     the bridge for its own pair; this class stays unchanged.
///   * L -- substitutable for any IntegrationBackend; the manager
///     drives it identically with TcpBackend, MqttClient, OpcUaBackend.
///   * I -- exposes only the base interface; Modbus specifics
///     (poll counters, exception code) are reported via the
///     existing metricsSummary / connectionState hooks.
///   * D -- the four collaborators are passed in already wired
///     together at construction. Backend doesn't instantiate
///     anything itself; the composition root (main.cpp) decides
///     which registers to poll.
class ModbusBackend final : public IntegrationBackend {
public:
    /// @param client    Owned TCP transport. Provides the
    ///                  ModbusReader the poll loop reads from.
    /// @param map       Owned register map. Tells the loop which
    ///                  addresses to poll for which entities.
    /// @param bridge    Owned ingest bridge. Translates register
    ///                  values into ProductionModel setter calls.
    /// @param pollLoop  Owned poll loop. Holds the jthread.
    /// @param logger    Used for start/stop traces. Must outlive.
    ModbusBackend(std::unique_ptr<ModbusClient>       client,
                  std::unique_ptr<ModbusRegisterMap>  map,
                  std::unique_ptr<ModbusIngestBridge> bridge,
                  std::unique_ptr<ModbusPollLoop>     pollLoop,
                  core::Logger& logger);

    ~ModbusBackend() override;

    ModbusBackend(const ModbusBackend&)            = delete;
    ModbusBackend& operator=(const ModbusBackend&) = delete;
    ModbusBackend(ModbusBackend&&)                 = delete;
    ModbusBackend& operator=(ModbusBackend&&)      = delete;

    void start() override;
    void stop() override;

    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string metricsSummary() const override;

    /// Client state directly drives the pill colour:
    ///   * Loop not running          -> Disconnected (default)
    ///   * Loop running, never read  -> Connecting   (jthread up, no
    ///                                  successful poll yet)
    ///   * Loop running, last read ok-> Connected
    ///   * Loop running, last read bad-> Degraded   (talking but
    ///                                  errors -- broker / slave issue)
    [[nodiscard]] integration::BackendState
        connectionState() const noexcept override;

private:
    std::unique_ptr<ModbusClient>       client_;
    std::unique_ptr<ModbusRegisterMap>  map_;
    std::unique_ptr<ModbusIngestBridge> bridge_;
    std::unique_ptr<ModbusPollLoop>     pollLoop_;
    core::Logger& logger_;
};

}  // namespace app::integration::modbus
