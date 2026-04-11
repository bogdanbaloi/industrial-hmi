#pragma once

#include <string>
#include <functional>
#include <map>
#include <mutex>

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
    
    // Get current state
    SystemState getState() const { return currentState_; }
    
    // Initialize with demo data
    void initializeDemoData() {
        // Equipment statuses
        equipmentStatuses_[0] = {0, 1, 85, "Ready"};  // Online
        equipmentStatuses_[1] = {1, 2, 60, "Working"};  // Processing
        equipmentStatuses_[2] = {2, 3, 12, "Low supply"};  // Error
        equipmentStatuses_[3] = {3, 0, 0, "Not connected"};  // Offline
        
        // Actuator statuses
        actuatorStatuses_[0] = {0, 1, 150, 200, true, false};  // Working
        actuatorStatuses_[1] = {1, 0, 0, 0, true, true};  // Idle at home
        
        // Quality checkpoints
        qualityCheckpoints_[0] = {0, "Visual Inspection", 0, 645, 12, 98.1f, "Surface scratch detected"};
        qualityCheckpoints_[1] = {1, "Dimensional Check", 0, 645, 28, 95.7f, "Tolerance exceeded +0.02mm"};
        qualityCheckpoints_[2] = {2, "Functional Test", 1, 645, 45, 93.0f, "Response time slow"};
        
        // Work unit
        currentWorkUnit_ = {
            "WU-2024-001234",
            "PROD-STD-001",
            "Standard Production Item - Type A",
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

private:
    SimulatedModel() = default;
    
    void notifyEquipmentChange(uint32_t equipmentId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (equipmentStatuses_.count(equipmentId)) {
            for (auto& cb : equipmentCallbacks_) {
                cb(equipmentStatuses_[equipmentId]);
            }
        }
    }
    
    void notifyActuatorChange(uint32_t actuatorId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (actuatorStatuses_.count(actuatorId)) {
            for (auto& cb : actuatorCallbacks_) {
                cb(actuatorStatuses_[actuatorId]);
            }
        }
    }
    
    void notifyQualityChange(uint32_t checkpointId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (qualityCheckpoints_.count(checkpointId)) {
            for (auto& cb : qualityCallbacks_) {
                cb(qualityCheckpoints_[checkpointId]);
            }
        }
    }
    
    void notifyWorkUnitChange() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& cb : workUnitCallbacks_) {
            cb(currentWorkUnit_);
        }
    }
    
    void notifyStateChange() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& cb : stateCallbacks_) {
            cb(currentState_);
        }
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
};

}  // namespace app::model
