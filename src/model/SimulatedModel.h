#pragma once

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
class SimulatedModel {
public:
    /// Equipment status simulation
    struct EquipmentStatus {
        uint32_t equipmentId{0};
        int status{0};  // 0=offline, 1=online, 2=processing, 3=error
        int supplyLevel{0};  // 0-100%
        std::string message;
    };
    
    /// Actuator status simulation
    struct ActuatorStatus {
        uint32_t actuatorId{0};
        int status{0};  // 0=idle, 1=working, 2=error
        int posX{0};
        int posY{0};
        bool autoMode{true};
        bool atHome{true};
    };
    
    /// Quality checkpoint simulation
    struct QualityCheckpoint {
        uint32_t checkpointId{0};
        std::string name;
        int status{0};  // 0=passing, 1=warning, 2=critical
        int unitsInspected{0};
        int defectsFound{0};
        float passRate{100.0f};
        std::string lastDefect;
    };
    
    /// Work unit simulation
    struct WorkUnit {
        std::string workUnitId;
        std::string productId;
        std::string description;
        int completedOperations{0};
        int totalOperations{5};
    };
    
    /// System state
    enum class SystemState {
        IDLE,
        RUNNING,
        ERROR,
        CALIBRATION
    };
    
    static SimulatedModel& instance() {
        static SimulatedModel inst;
        return inst;
    }
    
    // Subscribe to state changes
    using EquipmentCallback = std::function<void(const EquipmentStatus&)>;
    using ActuatorCallback = std::function<void(const ActuatorStatus&)>;
    using QualityCheckpointCallback = std::function<void(const QualityCheckpoint&)>;
    using WorkUnitCallback = std::function<void(const WorkUnit&)>;
    using StateCallback = std::function<void(SystemState)>;
    
    void onEquipmentStatusChanged(EquipmentCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        equipmentCallbacks_.push_back(cb);
    }
    
    void onActuatorStatusChanged(ActuatorCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        actuatorCallbacks_.push_back(cb);
    }
    
    void onQualityCheckpointChanged(QualityCheckpointCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        qualityCallbacks_.push_back(cb);
    }
    
    void onWorkUnitChanged(WorkUnitCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        workUnitCallbacks_.push_back(cb);
    }
    
    void onSystemStateChanged(StateCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        stateCallbacks_.push_back(cb);
    }
    
    // User commands
    void startProduction() {
        currentState_ = SystemState::RUNNING;
        notifyStateChange();
        simulateProductionCycle();
    }
    
    void stopProduction() {
        currentState_ = SystemState::IDLE;
        notifyStateChange();
    }
    
    void resetSystem() {
        currentState_ = SystemState::IDLE;
        currentWorkUnit_.completedOperations = 0;
        notifyStateChange();
        notifyWorkUnitChange();
    }
    
    void startCalibration() {
        currentState_ = SystemState::CALIBRATION;
        notifyStateChange();
    }
    
    void setEquipmentEnabled(uint32_t equipmentId, bool enabled) {
        // Simulate enable/disable
        if (equipmentId < 4) {
            equipmentStatuses_[equipmentId].status = enabled ? 1 : 0;
            notifyEquipmentChange(equipmentId);
        }
    }
    
    SystemState getState() const { return currentState_; }

    const QualityCheckpoint& getQualityCheckpoint(uint32_t id) const {
        return qualityCheckpoints_.at(id);
    }
    const WorkUnit& getWorkUnit() const { return currentWorkUnit_; }

    /// Advance simulation by one tick (called by auto refresh timer)
    void tickSimulation() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::uniform_real_distribution<float> rateDist(-0.3f, 0.3f);
            std::uniform_int_distribution<int> unitDist(1, 3);

            for (auto& [id, cp] : qualityCheckpoints_) {
                cp.passRate = std::clamp(cp.passRate + rateDist(rng_), 85.0f, 100.0f);
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
            std::lock_guard<std::mutex> lock(mutex_);
            cbs = qualityCallbacks_;
            cp = qualityCheckpoints_[id];
        }
        for (auto& cb : cbs) { cb(cp); }
    }

    void notifyWorkUnitChange() {
        std::vector<WorkUnitCallback> cbs;
        WorkUnit wu;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cbs = workUnitCallbacks_;
            wu = currentWorkUnit_;
        }
        for (auto& cb : cbs) { cb(wu); }
    }
    
    // Initialize with demo data
    void initializeDemoData() {
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
            std::lock_guard<std::mutex> lock(mutex_);
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
            std::lock_guard<std::mutex> lock(mutex_);
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
            std::lock_guard<std::mutex> lock(mutex_);
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
            equipmentStatuses_[1].status = 2;
            notifyEquipmentChange(1);
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
    std::mt19937 rng_{42};
};

}  // namespace app::model
