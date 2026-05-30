#pragma once

#include <cstdint>
#include <string>

namespace app::model {

/// Default quality-checkpoint pass-rate target (percent) before a
/// product recipe is loaded. Matches config::defaults::kQualityPassThreshold
/// but kept local so this lightweight types header pulls no config
/// dependency.
inline constexpr float kDefaultPassRateTarget = 95.0F;

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
    /// Target pass rate for this checkpoint, set from the active
    /// product's recipe (ProductionModel::loadProduct). Drives the
    /// "target N%" line the dashboard / alerts show. Default 95%
    /// matches the pre-recipe hardcoded expectation.
    float passRateTarget{kDefaultPassRateTarget};
};

/// Snapshot of the work unit currently moving through the line.
struct WorkUnit {
    std::string workUnitId;
    std::string productId;
    std::string description;
    int completedOperations{0};
    int totalOperations{5};
    /// Live production rate in completed work units per hour, computed by
    /// the model from recent work-unit completions (0 until at least two
    /// completions fall inside the meter's window; decays toward 0 when
    /// the line stalls). Primary line only -- the passive secondary
    /// (MirrorModel) does not compute throughput, so it stays 0 there.
    double throughputUnitsPerHour{0.0};
};

/// Top-level state of the production system.
enum class SystemState {
    IDLE,
    RUNNING,
    ERROR,
    CALIBRATION,
};

/// Nominal performance target for the demo line, in completed work units
/// per hour. The model uses this as the denominator of the Performance
/// component of OEE so `Performance = clamp(throughput / target, 0..1)`.
/// Matches the dashboard's THROUGHPUT card target so the two KPIs read
/// consistently.
inline constexpr double kPerformanceTargetUph = 100.0;

/// World-class OEE benchmark (Vorne / OEE Industry Standards). Used as
/// the dashboard's OEE-card target line so the tier (Ok/Warning) is
/// computed against the same number industry literature uses.
inline constexpr double kOeeWorldClassPct = 85.0;

/// Decomposition of OEE = Availability * Performance * Quality, each
/// expressed as a percentage in [0, 100]. The model computes this from
/// live signals (equipment status, work-unit throughput, checkpoint pass
/// rates); the view just renders the result instead of inventing its own
/// formula. Mirrors the industrial-standard OEE breakdown so the figure
/// is auditable, not a Phase-8F placeholder.
struct OeeMetrics {
    float availabilityPct{0.0F};
    float performancePct{0.0F};
    float qualityPct{0.0F};
    float oeePct{0.0F};
};

}  // namespace app::model
