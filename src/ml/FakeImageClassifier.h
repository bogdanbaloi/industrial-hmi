#pragma once

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageClassifier.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace app::ml {

/// Header-only test double for `ImageClassifier`.
///
/// Returns a pre-programmed list of `Classification` rows, optionally
/// truncated to the requested `k`. Lets upstream tests (presenter, UI)
/// drive deterministic outcomes without loading an ONNX model.
///
/// SOLID:
///   * S -- replays canned data, nothing else.
///   * L -- substitutable wherever `ImageClassifier&` is required;
///         throwing semantics for invalid `k` match the production
///         classifier.
///   * D -- decouples upstream tests from ONNX Runtime.
class FakeImageClassifier final : public ImageClassifier {
public:
    /// Build the fake from a fixed result list. The list is sorted by
    /// descending confidence here so callers don't have to remember to
    /// pre-sort it; tests can pass any order and trust the contract.
    explicit FakeImageClassifier(std::vector<Classification> cannedResults,
                                 std::string displayName = "Fake")
        : results_(std::move(cannedResults)),
          name_(std::move(displayName)) {
        std::sort(results_.begin(), results_.end(),
                  [](const Classification& lhs, const Classification& rhs) {
                      return lhs.confidence > rhs.confidence;
                  });
    }

    ~FakeImageClassifier() override = default;

    FakeImageClassifier(const FakeImageClassifier&) = delete;
    FakeImageClassifier& operator=(const FakeImageClassifier&) = delete;
    FakeImageClassifier(FakeImageClassifier&&) = delete;
    FakeImageClassifier& operator=(FakeImageClassifier&&) = delete;

    [[nodiscard]] std::vector<Classification>
        classifyTopK(const Image& /*image*/, int k) const override {
        if (k <= 0) {
            throw std::invalid_argument(
                "FakeImageClassifier: k must be positive.");
        }
        const auto count = std::min<std::size_t>(
            static_cast<std::size_t>(k), results_.size());
        return std::vector<Classification>(
            results_.begin(),
            results_.begin() + static_cast<std::ptrdiff_t>(count));
    }

    [[nodiscard]] std::string name() const override { return name_; }

private:
    std::vector<Classification> results_;
    std::string name_;
};

}  // namespace app::ml
