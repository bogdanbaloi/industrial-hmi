#pragma once

#include <string>
#include <vector>

namespace app::model {

/// A target pass rate for one named quality checkpoint, as specified by
/// a product's recipe. Matched onto the live `QualityCheckpoint` by
/// `checkpointName` (not by position) so the recipe is decoupled from
/// the order checkpoints happen to be created in.
struct CheckpointTarget {
    std::string checkpointName;   ///< Must match QualityCheckpoint::name
    float       passRateTarget{0.0F};   ///< 0 - 100 percent
};

/// Process specification for producing one product.
///
/// A recipe is distinct from the product catalogue entry (`Product`):
/// the product is master data (code, name, stock), the recipe is *how to
/// make it* -- how many operations a unit goes through and the quality
/// pass-rate targets each checkpoint must hold. Keyed by `productCode`
/// so a recipe is looked up from the product the operator selects.
///
/// Loading a recipe onto the production line (ProductionModel::loadProduct)
/// drives the dashboard: the work-unit operation count and each quality
/// card's target line both come from here.
struct Recipe {
    std::string                  productCode;        ///< FK to Product::productCode
    int                          totalOperations{5}; ///< Steps a work unit completes
    std::vector<CheckpointTarget> checkpointTargets;
};

}  // namespace app::model
