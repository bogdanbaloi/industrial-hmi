#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/model/ProductionModel.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace app::integration {

/// In-process bridge that mirrors equipment supply levels from a
/// "master" ProductionModel onto a "slave" ProductionModel.
///
/// Industrial case: a calibration / fill station feeds a production
/// station -- both run on independent PLCs in real deployments, both
/// have their own ProductionModel here, and the bridge plays the role
/// of the physical conveyor / pipe between them at the model layer.
///
/// Cross-process equivalent (master and slave on different machines)
/// is the same shape with an MQTT-backed bridge instead of this in-
/// process subscription. Designed so the swap is a backend
/// replacement, not a presenter or view change. See ADR-0011.
///
/// SOLID:
///   * S -- one job: forward master equipment-status events into
///     slave's setEquipmentSupplyLevel setter. No threading of its own,
///     no protocol, no UI.
///   * O -- a future SlaveToMasterBridge (acknowledgement / completion
///     signalling back upstream) is a new sibling class; this one
///     stays untouched.
///   * L -- implements IntegrationBackend like every other backend, so
///     IntegrationManager treats it uniformly and BackendHealthBar
///     renders it next to TCP / MQTT / Modbus / OPC-UA.
///   * I -- the IntegrationBackend interface is intentionally narrow;
///     bridge-specific state (mirror count, last forward timestamp)
///     surfaces through metricsSummary().
///   * D -- depends on two ProductionModel& references, never on the
///     concrete SimulatedModel.
///
/// Threading: master's onEquipmentStatusChanged callback fires on the
/// thread that produced the change (typically a SimulatedModel tick
/// thread, or an integration backend thread under inbound traffic).
/// The slave's setEquipmentSupplyLevel is the same entry point every
/// other ingest bridge uses; ProductionModel guarantees thread safety
/// for setters.
///
/// Lifecycle:
///   * start() subscribes to master and flips isRunning_ true.
///     Cheap, no I/O, no thread spawn -- the bridge is just a wired
///     callback.
///   * stop() flips isRunning_ false and sets a "muted" flag so any
///     in-flight callback already past the running-check exits without
///     touching the slave. There's no remove-callback API on
///     ProductionModel by design (see its docstring) -- the muted
///     flag is the correct, non-leaking shutdown path.
class MasterToSlaveBridge final : public IntegrationBackend {
public:
    /// Wires the bridge but does not subscribe yet -- the
    /// IntegrationBackend contract says start() does the side-effecting
    /// work.
    MasterToSlaveBridge(model::ProductionModel& master,
                        model::ProductionModel& slave) noexcept;

    ~MasterToSlaveBridge() override = default;

    // IntegrationBackend
    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] BackendState connectionState() const noexcept override;
    [[nodiscard]] std::string metricsSummary() const override;

    // --- Test / introspection surface (not part of IntegrationBackend).

    /// Number of equipment-status events the bridge has forwarded to
    /// the slave since construction. Used in tests and in the metrics
    /// tooltip.
    [[nodiscard]] std::size_t forwardedCount() const noexcept;

private:
    /// Callback installed on master at start(). Forwards the supply
    /// level into slave's setEquipmentSupplyLevel setter when the
    /// bridge is running and not muted.
    void onMasterEquipmentEvent(const model::EquipmentStatus& status);

    model::ProductionModel& master_;
    model::ProductionModel& slave_;

    // Flipped by start()/stop(). Atomic because the callback may fire
    // on a different thread than the one calling stop().
    std::atomic<bool> running_{false};

    // Lazy subscription guard: ProductionModel's onEquipmentStatusChanged
    // appends a callback every call (no remove path). Subscribing only
    // once on the very first start() avoids piling up stale callbacks
    // if the bridge is start/stop/start cycled.
    std::atomic<bool> subscribed_{false};

    // Counter incremented every successful forward. Plain atomic
    // because reads (metrics) and writes (callback) can race.
    std::atomic<std::size_t> forwarded_{0};

    // Last forward time -- shown in metricsSummary(). Guarded by
    // metricsMutex_ because std::chrono::system_clock::time_point
    // isn't trivially atomic across all platforms we target.
    mutable std::mutex metricsMutex_;
    std::chrono::system_clock::time_point lastForward_{};
};

}  // namespace app::integration
