#pragma once

#include "src/model/ProductionTypes.h"
#include "src/model/Product.h"
#include "src/model/Recipe.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace app::model {

/// Abstraction over the production-line model used by DashboardPresenter.
///
/// Lets the presenter depend on a small interface instead of the
/// SimulatedModel singleton, which keeps it unit-testable (tests inject
/// a MockProductionModel).
///
/// Conventions:
/// - Subscriptions append callbacks; there is no remove path because the
///   model and the presenters share the application's lifetime.
/// - Queries return by value (callers don't keep references into the
///   model's protected state).
class ProductionModel {
public:
    using EquipmentCallback = std::function<void(const EquipmentStatus&)>;
    using ActuatorCallback = std::function<void(const ActuatorStatus&)>;
    using QualityCheckpointCallback = std::function<void(const QualityCheckpoint&)>;
    using WorkUnitCallback = std::function<void(const WorkUnit&)>;
    using StateCallback = std::function<void(SystemState)>;

    virtual ~ProductionModel() = default;

    // Subscriptions
    virtual void onEquipmentStatusChanged(EquipmentCallback callback) = 0;
    virtual void onActuatorStatusChanged(ActuatorCallback callback) = 0;
    virtual void onQualityCheckpointChanged(QualityCheckpointCallback callback) = 0;
    virtual void onWorkUnitChanged(WorkUnitCallback callback) = 0;
    virtual void onSystemStateChanged(StateCallback callback) = 0;

    // Commands
    virtual void startProduction() = 0;
    virtual void stopProduction() = 0;
    virtual void resetSystem() = 0;
    virtual void startCalibration() = 0;
    virtual void setEquipmentEnabled(uint32_t equipmentId, bool enabled) = 0;

    /// Load a product's recipe onto the line, making it the active work
    /// unit. Replaces the work-unit identity (product code + a fresh
    /// work-unit id), resets progress + inspection counts (a new batch
    /// starts clean), sets `totalOperations` from the recipe, and applies
    /// each recipe checkpoint target onto the matching quality checkpoint
    /// (by name). Observers are notified of the work-unit and quality
    /// changes. On the passive secondary (MirrorModel) this is a no-op --
    /// the secondary mirrors the primary, it does not load recipes
    /// independently (see ADR-0011).
    virtual void loadProduct(const Product& product, const Recipe& recipe) = 0;

    /// Inbound analog setters. Used by ingest bridges (Modbus, MQTT,
    /// OPC-UA) that ship sensor data from external transports into
    /// the model. Once an analog field is set externally for a given
    /// entity id, the in-process simulator yields ownership of it --
    /// subsequent simulation ticks no longer overwrite the value.
    /// This matches what an HMI talking to a real PLC actually wants:
    /// the simulator is a development convenience, real data is
    /// authoritative.
    ///
    /// Out-of-range ids are silently dropped (a misbehaving sensor
    /// must not crash the HMI). Values are clamped to the field's
    /// natural domain (supply level 0..100; pass rate 0..100).
    virtual void setEquipmentSupplyLevel(uint32_t equipmentId,
                                         int level) = 0;
    virtual void setQualityPassRate(uint32_t checkpointId,
                                    float rate) = 0;

    // Queries
    [[nodiscard]] virtual SystemState getState() const = 0;
    [[nodiscard]] virtual QualityCheckpoint getQualityCheckpoint(uint32_t id) const = 0;

    /// All quality checkpoints, ordered by id. Lets a caller discover the
    /// canonical checkpoint names without knowing the ids -- used by the
    /// recipe editor to offer one pass-rate-target field per checkpoint
    /// even for a product that has no recipe yet.
    [[nodiscard]] virtual std::vector<QualityCheckpoint> getQualityCheckpoints() const = 0;

    [[nodiscard]] virtual WorkUnit getWorkUnit() const = 0;
};

}  // namespace app::model
