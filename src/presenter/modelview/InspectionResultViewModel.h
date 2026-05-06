#pragma once

#include "src/ml/Classification.h"

#include <chrono>
#include <string>
#include <vector>

namespace app::presenter {

/// Display-ready outcome of one image inspection run.
///
/// Carries the source path verbatim so the view can show "what was
/// inspected" alongside "what the model said". Latency is included
/// in milliseconds because real-world QA stations care about the time
/// budget per part; the value is wall-clock from decode start to
/// classifier return so it covers the full cost on the user's machine,
/// not just the model forward pass.
struct InspectionResultViewModel {
    /// Absolute path of the image that was just inspected. Stored as a
    /// string (already utf-8) so the view does not have to convert on
    /// each redraw.
    std::string sourcePath;

    /// Top-K classifications, sorted descending by confidence. Empty
    /// only on the failure path; the success callback always receives
    /// at least one row.
    std::vector<ml::Classification> results;

    /// End-to-end latency: decode + preprocess + ORT Run + softmax +
    /// label resolution. Stored as `std::chrono::milliseconds` so the
    /// view can format with whatever locale-aware policy it chooses.
    std::chrono::milliseconds latency{0};
};

}  // namespace app::presenter
