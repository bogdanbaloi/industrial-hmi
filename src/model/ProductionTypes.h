#pragma once

#include <cstdint>
#include <string>

namespace app::model {

/// Snapshot of a single equipment line's runtime state.
/// status uses the existing integer encoding: 0 offline, 1 online,
/// 2 processing, 3 error. Kept as int (rather than an enum) so the wire
/// format with PLC-side code stays unchanged.
struct EquipmentStatus {
    uint32_t equipmentId{0};
    int status{0};
    int supplyLevel{0};   ///< 0 - 100 percent
    std::string message;
};

/// Snapshot of a single actuator's runtime state.
/// status: 0 idle, 1 working, 2 error.
struct ActuatorStatus {
    uint32_t actuatorId{0};
    int status{0};
    int posX{0};
    int posY{0};
    bool autoMode{true};
    bool atHome{true};
};

/// Snapshot of a quality checkpoint at a moment in time.
/// status: 0 passing, 1 warning, 2 critical.
struct QualityCheckpoint {
    uint32_t checkpointId{0};
    std::string name;
    int status{0};
    int unitsInspected{0};
    int defectsFound{0};
    float passRate{100.0f};
    std::string lastDefect;
};

/// Snapshot of the work unit currently moving through the line.
struct WorkUnit {
    std::string workUnitId;
    std::string productId;
    std::string description;
    int completedOperations{0};
    int totalOperations{5};
};

/// Top-level state of the production system.
enum class SystemState {
    IDLE,
    RUNNING,
    ERROR,
    CALIBRATION,
};

}  // namespace app::model
