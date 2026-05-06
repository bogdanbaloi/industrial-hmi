#pragma once

#include <string>

namespace app::ml {

/// One row of a classifier's top-K output.
///
/// Pure data, no behaviour. The classifier produces `Classification`s;
/// the UI / log / CSV consumers read them. Keeping this struct trivially
/// constructible means tests can build expected results inline without
/// going through the classifier or labels machinery.
struct Classification {
    /// ImageNet class index (0-999 for the 1000-class taxonomy). Stable
    /// across model exports because it ties to the dataset, not the
    /// network.
    int classId = 0;

    /// Human-readable label resolved through `ImageNetLabels::at(classId)`.
    /// Stored as `std::string` (not `string_view`) so the result outlives
    /// the labels object that produced it -- callers may reorder, copy,
    /// or persist these without thinking about lifetimes.
    std::string label;

    /// Softmax probability in [0, 1]. Comparable across classes within
    /// one classifier output, but NOT comparable across different model
    /// architectures or quantisations -- INT8 confidences in particular
    /// drift relative to FP32. Use `confidence` for ranking, not for
    /// absolute decisions ("is this 90% sure?").
    float confidence = 0.0F;
};

}  // namespace app::ml
