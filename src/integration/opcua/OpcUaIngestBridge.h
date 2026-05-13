#pragma once

#include "src/integration/opcua/OpcUaClient.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace app::model { class ProductionModel; }

namespace app::integration::opcua {

/// Inbound counterpart to `OpcUaBackend` -- subscribes a
/// `ProductionModel` to a remote OPC-UA endpoint's equipment-status
/// nodes through an `OpcUaClient`. Mirror of `SensorIngestBridge` on
/// the MQTT side; same SoC split between transport and domain.
///
/// Topic vocabulary (default prefix `Factory`):
///   <prefix>/EquipmentLines/Line<id>/Status   Int32 (0..3 enum)
///
/// The mapping interprets the status integer the way `ProductionTypes.h`
/// documents it (0 = offline, 1+ = online/processing/error). Bridge
/// flips `model.setEquipmentEnabled(id, status != 0)` -- the model
/// owns the finer-grained status enum; the bridge only carries the
/// enabled/disabled bit because that's the only inbound surface the
/// model exposes today.
///
/// @warning Loopback caveat: pointing this bridge at the HMI's own
/// OPC-UA server (the default `opc.tcp://127.0.0.1:4840`) creates a
/// cycle -- our server publishes the simulator's current status, the
/// client subscribes and feeds it back through `setEquipmentEnabled`,
/// which overwrites finer-grained simulator values (Processing -> Online).
/// Default config has `network.opcua.client.ingest_bridge.enabled =
/// false` for that reason; deployments pointing at a real PLC (whose
/// nodes are independent of our model) are safe to flip it on.
///
/// SOLID:
///   * S -- one job: turn inbound OPC-UA notifications into Model
///     mutations. Doesn't connect, doesn't subscribe to other
///     protocols, doesn't render anything.
///   * O -- adding a new monitored node = adding a `subscribeXxx`
///     call in `wire()`. No control-flow changes elsewhere.
///   * L -- depends on `OpcUaClient&` (abstract); any concrete that
///     honours the subscribe contract works.
///   * I -- intentionally narrow: a constructor and `wire()`.
///   * D -- model mutation goes through `ProductionModel&`
///     abstraction, never `SimulatedModel`.
///
/// Threading: callbacks fire on the client's I/O thread. `Model`'s
/// setters document their thread safety; the bridge respects that
/// contract by being side-effect-only (no shared state of its own
/// beyond the per-equipment dedup cache below).
class OpcUaIngestBridge {
public:
    struct Config {
        /// Common prefix prepended to each monitored browse path.
        /// Production deployments override per-PLC vendor schema.
        std::string topicPrefix{"Factory"};
        /// How many equipment slots to subscribe to. Matches
        /// SimulatedModel's three lines; raise this if the
        /// underlying model grows.
        std::uint32_t equipmentCount{3};
        /// How many quality checkpoints to subscribe to (drives the
        /// `<prefix>/QualityCheckpoints/Checkpoint<id>/PassRate`
        /// monitored items). Matches the simulator's three by default.
        std::uint32_t qualityCount{3};
    };

    OpcUaIngestBridge(OpcUaClient& client,
                      model::ProductionModel& model);
    OpcUaIngestBridge(OpcUaClient& client,
                      model::ProductionModel& model,
                      Config config);

    OpcUaIngestBridge(const OpcUaIngestBridge&)            = delete;
    OpcUaIngestBridge& operator=(const OpcUaIngestBridge&) = delete;
    OpcUaIngestBridge(OpcUaIngestBridge&&)                 = delete;
    OpcUaIngestBridge& operator=(OpcUaIngestBridge&&)      = delete;
    ~OpcUaIngestBridge()                                   = default;

    /// Register every monitored item with the wrapped `OpcUaClient`.
    /// Idempotent on the bridge side; the client itself decides
    /// whether a duplicate subscribe hits the wire.
    void wire();

private:
    /// Dispatch one Status notification for equipment `id`. Skips
    /// the model call if the incoming status doesn't actually flip
    /// the enabled bit since the last notification we processed --
    /// avoids pointless re-emissions when the underlying status
    /// enum oscillates within "online" values (Online <-> Processing).
    void onStatusNotification(std::uint32_t id, std::int32_t status);

    /// Dispatch one SupplyLevel notification. Bypasses the dedup
    /// cache for out-of-range ids but still forwards (model clamps
    /// + log-and-drop on unknown ids -- single source of truth).
    void onSupplyNotification(std::uint32_t id, std::int32_t supply);

    /// Dispatch one PassRate notification. Same dedup pattern as
    /// supply, but on float bit-equality (deterministic for a sensor
    /// repeating the same value; naturally drifts on real readings).
    void onPassRateNotification(std::uint32_t id, float rate);

    OpcUaClient&            client_;
    model::ProductionModel& model_;
    Config                  config_;

    /// Per-entity dedup caches. `nullopt` = no observation yet, so
    /// the first reading always fires through. Arrays stay on the
    /// stack; size cap matches the Modbus bridge so a runaway
    /// operator config can't blow this up.
    static constexpr std::size_t kMaxTrackedEquipment = 16;
    static constexpr std::size_t kMaxTrackedQuality   = 16;
    std::array<std::optional<bool>, kMaxTrackedEquipment> lastEnabled_{};
    std::array<std::optional<int>, kMaxTrackedEquipment> lastSupplyLevel_{};
    std::array<std::optional<float>, kMaxTrackedQuality> lastPassRate_{};
};

}  // namespace app::integration::opcua
