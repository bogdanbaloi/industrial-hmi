#pragma once

#include "src/model/ProductionModel.h"
#include "src/model/ProductionTypes.h"
#include "src/core/LoggerBase.h"

#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <algorithm>

namespace app::model {

/// Simulated Model layer for demo purposes
///
/// This stub provides simulated data and state changes to demonstrate
/// the MVP architecture without exposing proprietary business logic.
///
/// Implements ProductionModel so DashboardPresenter can depend on the
/// abstraction; tests inject a MockProductionModel instead of this
/// singleton.
class SimulatedModel : public ProductionModel {
public:
    // Simulation parameters (not business logic -- just demo noise shaping)
    static constexpr std::size_t kEquipmentCount = 4;
    static constexpr float kQualityRateJitter = 0.3f;
    static constexpr int kMinUnitsPerTick = 1;
    static constexpr int kMaxUnitsPerTick = 3;
    static constexpr float kQualityRateMin = 85.0f;
    static constexpr float kQualityRateMax = 100.0f;
    static constexpr int kEquipmentStatusProcessing = 2;
    static constexpr int kEquipmentStatusOnline = 1;
    static constexpr int kEquipmentStatusOffline = 0;
    static constexpr std::uint_fast32_t kRngSeed = 42;

    static SimulatedModel& instance() {
        static SimulatedModel inst;
        return inst;
    }

    /// Optional logger (DI, same pattern as DatabaseManager::setLogger).
    /// Tests skip this call so SimulatedModel stays header-only-linkable
    /// without dragging Application into the test target.
    void setLogger(app::core::Logger& logger) {
        logger_ = &logger;
    }

    void onEquipmentStatusChanged(EquipmentCallback cb) override {
        const std::scoped_lock lock(mutex_);
        equipmentCallbacks_.push_back(cb);
    }

    void onActuatorStatusChanged(ActuatorCallback cb) override {
        const std::scoped_lock lock(mutex_);
        actuatorCallbacks_.push_back(cb);
    }

    void onQualityCheckpointChanged(QualityCheckpointCallback cb) override {
        const std::scoped_lock lock(mutex_);
        qualityCallbacks_.push_back(cb);
    }

    void onWorkUnitChanged(WorkUnitCallback cb) override {
        const std::scoped_lock lock(mutex_);
        workUnitCallbacks_.push_back(cb);
    }

    void onSystemStateChanged(StateCallback cb) override {
        const std::scoped_lock lock(mutex_);
        stateCallbacks_.push_back(cb);
    }

    /// Drop every subscription. Used by MainWindow during a language
    /// rebuild: after this returns, no callback references an old (about
    /// to be destroyed) presenter, so rebuilding the page + presenter
    /// graph does not leave dangling lambdas behind.
    void clearCallbacks() {
        const std::scoped_lock lock(mutex_);
        if (logger_) {
            logger_->debug(
                "Model: clearing callbacks (eq={}, act={}, qc={}, wu={}, st={})",
                equipmentCallbacks_.size(), actuatorCallbacks_.size(),
                qualityCallbacks_.size(), workUnitCallbacks_.size(),
                stateCallbacks_.size());
        }
        equipmentCallbacks_.clear();
        actuatorCallbacks_.clear();
        qualityCallbacks_.clear();
        workUnitCallbacks_.clear();
        stateCallbacks_.clear();
    }

    // User commands
    void startProduction() override {
        if (logger_) logger_->info("Model: state IDLE -> RUNNING (production started)");
        currentState_ = SystemState::RUNNING;
        notifyStateChange();
        simulateProductionCycle();
    }

    void stopProduction() override {
        if (logger_) logger_->info("Model: state -> IDLE (production stopped)");
        currentState_ = SystemState::IDLE;
        notifyStateChange();
    }

    void resetSystem() override {
        if (logger_) logger_->info("Model: reset system - clearing work unit progress");
        currentState_ = SystemState::IDLE;
        currentWorkUnit_.completedOperations = 0;
        notifyStateChange();
        notifyWorkUnitChange();
    }

    void startCalibration() override {
        if (logger_) logger_->info("Model: state -> CALIBRATION");
        currentState_ = SystemState::CALIBRATION;
        notifyStateChange();
    }

    void setEquipmentEnabled(uint32_t equipmentId, bool enabled) override {
        if (equipmentId >= kEquipmentCount) {
            if (logger_) {
                logger_->warn("Model: setEquipmentEnabled out of range (id={}, max={})",
                              equipmentId, kEquipmentCount);
            }
            return;
        }
        if (logger_) {
            logger_->debug("Model: equipment {} -> {}",
                           equipmentId, enabled ? "online" : "offline");
        }
        equipmentStatuses_[equipmentId].status =
            enabled ? kEquipmentStatusOnline : kEquipmentStatusOffline;
        notifyEquipmentChange(equipmentId);
    }

    [[nodiscard]] SystemState getState() const override { return currentState_; }

    [[nodiscard]] QualityCheckpoint getQualityCheckpoint(uint32_t id) const override {
        return qualityCheckpoints_.at(id);
    }

    [[nodiscard]] WorkUnit getWorkUnit() const override { return currentWorkUnit_; }

    /// Advance simulation by one tick (called by auto refresh timer)
    void tickSimulation() {
        if (logger_) logger_->trace("Model: simulation tick");
        {
            const std::scoped_lock lock(mutex_);
            std::uniform_real_distribution<float> rateDist(-kQualityRateJitter, kQualityRateJitter);
            std::uniform_int_distribution<int> unitDist(kMinUnitsPerTick, kMaxUnitsPerTick);

            for (auto& [id, cp] : qualityCheckpoints_) {
                cp.passRate = std::clamp(cp.passRate + rateDist(rng_),
                                         kQualityRateMin, kQualityRateMax);
                cp.unitsInspected += unitDist(rng_);
            }

            if (currentWorkUnit_.completedOperations < currentWorkUnit_.totalOperations) {
                currentWorkUnit_.completedOperations++;
            } else {
                currentWorkUnit_.completedOperations = 0;
            }
        }

        for (const auto& [id, cp] : qualityCheckpoints_) {
            notifyQualityChange(id);
        }
        notifyWorkUnitChange();
    }

    void notifyQualityChange(uint32_t id) {
        std::vector<QualityCheckpointCallback> cbs;
        QualityCheckpoint cp;
        {
            const std::scoped_lock lock(mutex_);
            cbs = qualityCallbacks_;
            cp = qualityCheckpoints_[id];
        }
        for (auto& cb : cbs) { cb(cp); }
    }

    void notifyWorkUnitChange() {
        std::vector<WorkUnitCallback> cbs;
        WorkUnit wu;
        {
            const std::scoped_lock lock(mutex_);
            cbs = workUnitCallbacks_;
            wu = currentWorkUnit_;
        }
        for (auto& cb : cbs) { cb(wu); }
    }
    
    // Initialize with demo data
    // Demo-data literals (batch IDs, counts, pass rates, etc.) are data,
    // not behavior -- suppress magic-number lint for this block.
    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    void initializeDemoData() {
        if (logger_) {
            logger_->info("Model: initializing demo data "
                          "(3 equipment lines, 3 quality checkpoints)");
        }
        // Equipment statuses (3 lines: A-LINE, B-LINE, C-LINE)
        equipmentStatuses_[0] = {0, 2, 85, "85K tablets/hr"};  // A-LINE (Processing)
        equipmentStatuses_[1] = {1, 1, 95, "Film coating"};     // B-LINE (Online)
        equipmentStatuses_[2] = {2, 0, 0, "Standby"};           // C-LINE (Offline)
        
        // Actuator statuses
        actuatorStatuses_[0] = {0, 1, 150, 200, true, false};  // Working
        actuatorStatuses_[1] = {1, 0, 0, 0, true, true};  // Idle at home
        
        // Quality checkpoints (3 checks)
        qualityCheckpoints_[0] = {0, "Weight Check", 0, 645, 12, 98.1f, "Underweight"};
        qualityCheckpoints_[1] = {1, "Hardness Test", 0, 645, 28, 95.7f, "Soft tablet (65N)"};
        qualityCheckpoints_[2] = {2, "Final Inspection", 1, 645, 45, 93.0f, "Coating uneven"};
        
        // Work unit (Pharma batch)
        currentWorkUnit_ = {
            "WU-2024-001234",
            "TAB-200",
            "Batch WU-2024-001234 | TAB-200",
            3,
            5
        };
        
        currentState_ = SystemState::IDLE;
        
        // Notify initial state
        for (const auto& [id, status] : equipmentStatuses_) {
            notifyEquipmentChange(id);
        }
        for (const auto& [id, status] : actuatorStatuses_) {
            notifyActuatorChange(id);
        }
        for (const auto& [id, checkpoint] : qualityCheckpoints_) {
            notifyQualityChange(id);
        }
        notifyWorkUnitChange();
        notifyStateChange();
    }
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

    SimulatedModel(const SimulatedModel&) = delete;
    SimulatedModel& operator=(const SimulatedModel&) = delete;
    SimulatedModel(SimulatedModel&&) = delete;
    SimulatedModel& operator=(SimulatedModel&&) = delete;

private:
    SimulatedModel() = default;

    void notifyEquipmentChange(uint32_t equipmentId) {
        std::vector<EquipmentCallback> cbs;
        EquipmentStatus es;
        {
            const std::scoped_lock lock(mutex_);
            if (!equipmentStatuses_.count(equipmentId)) return;
            cbs = equipmentCallbacks_;
            es = equipmentStatuses_[equipmentId];
        }
        for (auto& cb : cbs) { cb(es); }
    }

    void notifyActuatorChange(uint32_t actuatorId) {
        std::vector<ActuatorCallback> cbs;
        ActuatorStatus as;
        {
            const std::scoped_lock lock(mutex_);
            if (!actuatorStatuses_.count(actuatorId)) return;
            cbs = actuatorCallbacks_;
            as = actuatorStatuses_[actuatorId];
        }
        for (auto& cb : cbs) { cb(as); }
    }

    void notifyStateChange() {
        std::vector<StateCallback> cbs;
        SystemState state;
        {
            const std::scoped_lock lock(mutex_);
            cbs = stateCallbacks_;
            state = currentState_;
        }
        for (auto& cb : cbs) { cb(state); }
    }
    
    void simulateProductionCycle() {
        // Simulate production progress
        if (currentWorkUnit_.completedOperations < currentWorkUnit_.totalOperations) {
            currentWorkUnit_.completedOperations++;
            notifyWorkUnitChange();

            // Update equipment to "processing"
            constexpr uint32_t kBLineId = 1;
            equipmentStatuses_[kBLineId].status = kEquipmentStatusProcessing;
            notifyEquipmentChange(kBLineId);
        }
    }
    
    std::map<uint32_t, EquipmentStatus> equipmentStatuses_;
    std::map<uint32_t, ActuatorStatus> actuatorStatuses_;
    std::map<uint32_t, QualityCheckpoint> qualityCheckpoints_;
    WorkUnit currentWorkUnit_;
    SystemState currentState_{SystemState::IDLE};
    
    std::vector<EquipmentCallback> equipmentCallbacks_;
    std::vector<ActuatorCallback> actuatorCallbacks_;
    std::vector<QualityCheckpointCallback> qualityCallbacks_;
    std::vector<WorkUnitCallback> workUnitCallbacks_;
    std::vector<StateCallback> stateCallbacks_;
    
    mutable std::mutex mutex_;
    std::mt19937 rng_{kRngSeed};

    // Optional logger -- null when the test harness exercises the model
    // without a real Application; set by Application::initDatabase().
    app::core::Logger* logger_{nullptr};
};

}  // namespace app::model
