#pragma once

#include <cstdint>
#include <string>

namespace app::presenter {

/// Display-ready data for current production work unit information
///
/// @design This ViewModel aggregates data from multiple sources:
///         - Work unit ID from PLC signals
///         - Product data from database
///         - Production progress from state machine
///         - Process specifications from database
///
/// @pattern The Presenter queries all these sources, aggregates the data,
///          and packages it into this simple ViewModel for the View to render.
struct WorkUnitViewModel {
    /// Current work unit identifier (barcode or ID from production system)
    /// @example "WU-2024-001234"
    std::string workUnitId;

    /// Product/item identifier
    /// @example "PROD-STD-001"
    std::string productId;

    /// Human-readable product description
    /// @example "Standard Production Item - Type A"
    std::string productDescription;

    /// Production progress (0.0 to 1.0)
    /// @note 0.0 = started, 1.0 = complete
    /// @example 0.8 = 80% complete (4 of 5 operations)
    float progress{0.0f};

    /// Total number of operations to complete
    /// @note Varies by product type and process requirements
    uint32_t totalOperations{0};

    /// Number of operations already completed successfully
    uint32_t completedOperations{0};

    /// Current operation status message
    /// @examples "Processing", "Work unit complete", "Waiting for equipment"
    std::string statusMessage;

    /// Whether this is a test run (affects UI styling and workflow)
    bool isTestMode{false};

    /// Whether this work unit has errors (drives error styling)
    bool hasErrors{false};

    /// Error message if hasErrors == true
    std::string errorMessage;

    /// Equality for caching
    bool operator!=(const WorkUnitViewModel& other) const {
        return workUnitId != other.workUnitId
            || productId != other.productId
            || progress != other.progress
            || completedOperations != other.completedOperations
            || statusMessage != other.statusMessage
            || hasErrors != other.hasErrors;
    }
};

}  // namespace app::presenter
