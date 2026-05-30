#pragma once

#include "src/model/ProductionModel.h"
#include "src/model/ProductionTypes.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace app::model {

/// Concrete ProductionModel that holds state but does not run its own
/// simulation. Designed for the SLAVE role in a multi-station
/// deployment: state changes arrive from a `PrimaryToSecondaryBridge`
/// (or, in a future cross-process variant, from an MQTT/OPC-UA ingest
/// bridge) and the model just stores them + fires observer callbacks.
///
/// Why not reuse SimulatedModel for both stations?
///   SimulatedModel is intentionally a singleton -- the single-station
///   HMI assumes one model instance per process. Lifting that
///   constraint touches every caller of `SimulatedModel::instance()`.
///   MirrorModel is the lower-risk path: introduce a separate concrete
///   for the secondary role, keep SimulatedModel as the primary, both
///   implement the same ProductionModel interface. See ADR-0011 for
///   the trade-off discussion.
///
/// Behaviour:
///   * Inbound setters (setEquipmentSupplyLevel, setQualityPassRate,
///     setEquipmentEnabled) mutate local state and fire the matching
///     observer callbacks. This is what the bridge calls.
///   * Commands (startProduction, stopProduction, resetSystem,
///     startCalibration) DO update the local system state and fire
///     onSystemStateChanged, so the operator sees something happen
///     when they click. They do NOT trigger any background ticking;
///     the secondary is a mirror, not an independent producer. Sufficient
///     for v1; cross-process v2 will route secondary commands back upstream.
///   * Queries return whatever is in local state.
///
/// Thread safety: the bridge fires from whatever thread produced the
/// primary event (typically the simulator tick thread). Setters take
/// the model mutex; callbacks are invoked under the mutex with copies
/// so the observer can safely read.
class MirrorModel final : public ProductionModel {
public:
    /// Pre-populates with `equipmentCount` equipment slots and
    /// `qualityCount` quality checkpoints so the secondary's dashboard
    /// renders cards from the very first frame, before any bridge
    /// event arrives. Defaults match SimulatedModel's three-line
    /// layout.
    explicit MirrorModel(std::uint32_t equipmentCount = 3,
                         std::uint32_t qualityCount   = 3);

    ~MirrorModel() override = default;

    MirrorModel(const MirrorModel&)            = delete;
    MirrorModel& operator=(const MirrorModel&) = delete;
    MirrorModel(MirrorModel&&)                 = delete;
    MirrorModel& operator=(MirrorModel&&)      = delete;

    // ProductionModel: subscriptions
    void onEquipmentStatusChanged(EquipmentCallback callback) override;
    void onActuatorStatusChanged(ActuatorCallback callback) override;
    void onQualityCheckpointChanged(QualityCheckpointCallback callback) override;
    void onWorkUnitChanged(WorkUnitCallback callback) override;
    void onSystemStateChanged(StateCallback callback) override;

    // ProductionModel: commands (local-only -- see class docstring)
    void startProduction() override;
    void stopProduction() override;
    void resetSystem() override;
    void startCalibration() override;
    void setEquipmentEnabled(std::uint32_t equipmentId, bool enabled) override;

    /// No-op. The secondary station mirrors the primary; an operator
    /// does not load a recipe independently onto it (see ADR-0011).
    /// The primary's loadProduct propagates through the bridge.
    void loadProduct(const Product& product, const Recipe& recipe) override;

    // ProductionModel: inbound setters (driven by the bridge)
    void setEquipmentSupplyLevel(std::uint32_t equipmentId, int level) override;
    void setQualityPassRate(std::uint32_t checkpointId, float rate) override;

    // ProductionModel: queries
    [[nodiscard]] SystemState getState() const override;
    [[nodiscard]] std::string lastFaultReason() const override { return {}; }
    [[nodiscard]] QualityCheckpoint getQualityCheckpoint(std::uint32_t id) const override;
    [[nodiscard]] std::vector<QualityCheckpoint> getQualityCheckpoints() const override;
    [[nodiscard]] WorkUnit getWorkUnit() const override;

private:
    // Helpers: clamp + dispatch. The setEquipmentSupplyLevel /
    // setQualityPassRate spec says values are clamped to the field's
    // natural domain (0-100). Out-of-range ids are silently dropped.
    void updateEquipmentSnapshotLocked(EquipmentStatus snapshot);
    void updateQualitySnapshotLocked(QualityCheckpoint snapshot);
    void publishSystemStateChange(SystemState newState);

    mutable std::mutex mutex_;

    // State -- by id so the bridge can write to any slot.
    std::unordered_map<std::uint32_t, EquipmentStatus>  equipment_;
    std::unordered_map<std::uint32_t, QualityCheckpoint> quality_;
    WorkUnit                                            workUnit_{};
    SystemState                                         state_{SystemState::IDLE};

    // Observer lists. Subscriptions append-only -- ProductionModel's
    // contract says there's no remove path because models live for the
    // application's lifetime.
    std::vector<EquipmentCallback>          equipmentObservers_;
    std::vector<ActuatorCallback>           actuatorObservers_;
    std::vector<QualityCheckpointCallback>  qualityObservers_;
    std::vector<WorkUnitCallback>           workUnitObservers_;
    std::vector<StateCallback>              stateObservers_;
};

}  // namespace app::model
