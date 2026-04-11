#pragma once

#include <string>

namespace app::presenter {

/// Status for quality checkpoint
enum class QualityCheckpointStatus {
    Passing,     // Pass rate meets target
    Warning,     // Pass rate below target but acceptable
    Critical     // Pass rate critically low
};

/// ViewModel for Quality Checkpoint card display
///
/// Represents a quality inspection point in the production line.
/// Business value: Quality tracking reduces defects and warranty costs.
struct QualityCheckpointViewModel {
    uint32_t checkpointId{0};
    std::string checkpointName;         // e.g., "Visual Inspection", "Dimensional Check"
    QualityCheckpointStatus status{QualityCheckpointStatus::Passing};
    
    int unitsInspected{0};              // Total units checked today
    int defectsFound{0};                // Defects detected
    float passRate{100.0f};             // Percentage (0-100)
    float targetPassRate{95.0f};        // Target threshold
    
    std::string lastDefect;             // Description of last defect found
    
    bool operator==(const QualityCheckpointViewModel& other) const {
        return checkpointId == other.checkpointId &&
               status == other.status &&
               unitsInspected == other.unitsInspected &&
               defectsFound == other.defectsFound &&
               passRate == other.passRate;
    }
};

}  // namespace app::presenter
