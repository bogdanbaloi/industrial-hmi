#pragma once

#include "src/ml/Classification.h"
#include "src/ml/Image.h"

#include <string>
#include <vector>

namespace app::ml {

/// Image-in, top-K-classifications-out abstraction.
///
/// SOLID:
///   * S -- one job: classify a decoded image. No file I/O, no decoding,
///         no UI. Implementations that load a model, preprocess, and run
///         inference compose those concerns inside; the interface stays
///         narrow.
///   * O -- new model architectures (ResNet, EfficientNet) or runtimes
///         (libtorch, TensorRT, OpenVINO) plug in as new subclasses.
///         Existing call sites (presenter, console front-end, future
///         QualityInspectionPage) stay untouched.
///   * L -- presenters depend on `ImageClassifier&`, never on a concrete
///         class. `OnnxImageClassifier` and `FakeImageClassifier` are
///         interchangeable from the caller's perspective.
///   * I -- methods exposed are exactly what every classifier needs:
///         classify a single image and report a name. Threading model,
///         GPU policy, batching strategy do NOT bleed through; those
///         stay implementation details.
///   * D -- upstream code (presenter, integration tests) takes a
///         reference to this abstraction; tests inject a deterministic
///         `FakeImageClassifier` to exercise UI paths without dragging
///         the ONNX Runtime in.
class ImageClassifier {
public:
    virtual ~ImageClassifier() = default;

    ImageClassifier(const ImageClassifier&) = delete;
    ImageClassifier& operator=(const ImageClassifier&) = delete;
    ImageClassifier(ImageClassifier&&) = delete;
    ImageClassifier& operator=(ImageClassifier&&) = delete;

    /// Run inference on `image` and return the top-`k` classifications
    /// sorted by descending confidence. Throws `std::invalid_argument`
    /// if `k` is non-positive or larger than the implementation can
    /// produce; throws `std::runtime_error` on inference failure
    /// (corrupted weights, runtime exception, shape mismatch). Implementations
    /// MUST NOT silently truncate, log, or return empty vectors -- callers
    /// rely on either a populated result or a thrown exception.
    [[nodiscard]] virtual std::vector<Classification>
        classifyTopK(const Image& image, int k) const = 0;

    /// Display-friendly identifier (e.g. "MobileNetV2 INT8 (ONNX)",
    /// "Fake-2-class"). Used in log lines and the future settings UI.
    /// Stable -- do not localise.
    [[nodiscard]] virtual std::string name() const = 0;

protected:
    ImageClassifier() = default;
};

}  // namespace app::ml
