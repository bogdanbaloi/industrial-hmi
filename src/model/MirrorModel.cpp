#include "src/model/MirrorModel.h"

#include <algorithm>
#include <utility>

namespace app::model {

namespace {
// Domain clamps matching the contract documented on
// ProductionModel::setEquipmentSupplyLevel + setQualityPassRate.
constexpr int    kMinSupplyLevel   = 0;
constexpr int    kMaxSupplyLevel   = 100;
constexpr float  kMinPassRate      = 0.0f;
constexpr float  kMaxPassRate      = 100.0f;

// Default equipment status seed when an id is first observed --
// matches the SimulatedModel defaults so the slave's dashboard
// looks consistent before any bridge event has arrived.
constexpr int kDefaultStatus       = 1;   // ONLINE
constexpr int kDefaultSupplyLevel  = 100;
constexpr float kDefaultPassRate   = 100.0f;
}  // namespace

MirrorModel::MirrorModel(std::uint32_t equipmentCount,
                         std::uint32_t qualityCount) {
    for (std::uint32_t i = 0; i < equipmentCount; ++i) {
        EquipmentStatus eq;
        eq.equipmentId = i + 1;
        eq.status      = kDefaultStatus;
        eq.supplyLevel = kDefaultSupplyLevel;
        equipment_.emplace(eq.equipmentId, eq);
    }
    for (std::uint32_t i = 0; i < qualityCount; ++i) {
        QualityCheckpoint qc;
        qc.checkpointId    = i + 1;
        qc.name            = "Checkpoint " + std::to_string(i + 1);
        qc.status          = 0;
        qc.unitsInspected  = 0;
        qc.defectsFound    = 0;
        qc.passRate        = kDefaultPassRate;
        quality_.emplace(qc.checkpointId, qc);
    }
}

// ---------------------------------------------------------------- //
// Subscriptions                                                     //
// ---------------------------------------------------------------- //

void MirrorModel::onEquipmentStatusChanged(EquipmentCallback callback) {
    const std::lock_guard<std::mutex> lock(mutex_);
    equipmentObservers_.push_back(std::move(callback));
}

void MirrorModel::onActuatorStatusChanged(ActuatorCallback callback) {
    const std::lock_guard<std::mutex> lock(mutex_);
    actuatorObservers_.push_back(std::move(callback));
}

void MirrorModel::onQualityCheckpointChanged(QualityCheckpointCallback callback) {
    const std::lock_guard<std::mutex> lock(mutex_);
    qualityObservers_.push_back(std::move(callback));
}

void MirrorModel::onWorkUnitChanged(WorkUnitCallback callback) {
    const std::lock_guard<std::mutex> lock(mutex_);
    workUnitObservers_.push_back(std::move(callback));
}

void MirrorModel::onSystemStateChanged(StateCallback callback) {
    const std::lock_guard<std::mutex> lock(mutex_);
    stateObservers_.push_back(std::move(callback));
}

// ---------------------------------------------------------------- //
// Commands -- local-only state updates                              //
// ---------------------------------------------------------------- //

void MirrorModel::startProduction()  { publishSystemStateChange(SystemState::RUNNING); }
void MirrorModel::stopProduction()   { publishSystemStateChange(SystemState::IDLE); }
void MirrorModel::resetSystem()      { publishSystemStateChange(SystemState::IDLE); }
void MirrorModel::startCalibration() { publishSystemStateChange(SystemState::CALIBRATION); }

void MirrorModel::setEquipmentEnabled(std::uint32_t equipmentId, bool enabled) {
    EquipmentStatus snapshot;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = equipment_.find(equipmentId);
        if (it == equipment_.end()) return;
        it->second.status = enabled ? 1 : 0;   // 1 ONLINE, 0 OFFLINE
        snapshot = it->second;
    }
    // Fire callbacks outside the lock so observers can re-enter the
    // model safely without risk of self-deadlock.
    std::vector<EquipmentCallback> observersCopy;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        observersCopy = equipmentObservers_;
    }
    for (const auto& cb : observersCopy) cb(snapshot);
}

// ---------------------------------------------------------------- //
// Inbound setters -- the bridge calls these                          //
// ---------------------------------------------------------------- //

void MirrorModel::setEquipmentSupplyLevel(std::uint32_t equipmentId, int level) {
    level = std::clamp(level, kMinSupplyLevel, kMaxSupplyLevel);
    EquipmentStatus snapshot;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = equipment_.find(equipmentId);
        if (it == equipment_.end()) return;
        it->second.supplyLevel = level;
        snapshot = it->second;
    }
    updateEquipmentSnapshot_locked(std::move(snapshot));
}

void MirrorModel::setQualityPassRate(std::uint32_t checkpointId, float rate) {
    rate = std::clamp(rate, kMinPassRate, kMaxPassRate);
    QualityCheckpoint snapshot;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = quality_.find(checkpointId);
        if (it == quality_.end()) return;
        it->second.passRate = rate;
        snapshot = it->second;
    }
    updateQualitySnapshot_locked(std::move(snapshot));
}

// ---------------------------------------------------------------- //
// Queries                                                           //
// ---------------------------------------------------------------- //

SystemState MirrorModel::getState() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

QualityCheckpoint MirrorModel::getQualityCheckpoint(std::uint32_t id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = quality_.find(id);
    return it != quality_.end() ? it->second : QualityCheckpoint{};
}

WorkUnit MirrorModel::getWorkUnit() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return workUnit_;
}

// ---------------------------------------------------------------- //
// Helpers                                                           //
// ---------------------------------------------------------------- //

void MirrorModel::updateEquipmentSnapshot_locked(EquipmentStatus snapshot) {
    // Caller already updated the entry under the lock; we just need
    // the observer callbacks. Copy out the observer list so we can
    // fire them without holding the model mutex (observers may
    // re-enter for queries; would deadlock otherwise).
    std::vector<EquipmentCallback> observersCopy;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        observersCopy = equipmentObservers_;
    }
    for (const auto& cb : observersCopy) cb(snapshot);
}

void MirrorModel::updateQualitySnapshot_locked(QualityCheckpoint snapshot) {
    std::vector<QualityCheckpointCallback> observersCopy;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        observersCopy = qualityObservers_;
    }
    for (const auto& cb : observersCopy) cb(snapshot);
}

void MirrorModel::publishSystemStateChange(SystemState newState) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == newState) return;
        state_ = newState;
    }
    std::vector<StateCallback> observersCopy;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        observersCopy = stateObservers_;
    }
    for (const auto& cb : observersCopy) cb(newState);
}

}  // namespace app::model
