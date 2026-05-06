#pragma once

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageClassifier.h"
#include "src/ml/ImageNetLabels.h"
#include "src/ml/Preprocessor.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app::ml {

/// `ImageClassifier` backed by an ONNX Runtime session.
///
/// The constructor takes ownership of the inference session created from
/// `modelPath`; thereafter every `classifyTopK` call:
///
///     1. Runs the input image through the supplied `Preprocessor`,
///        producing the NCHW float tensor the model expects on its
///        single input port.
///     2. Hands the tensor to the ONNX Runtime session.
///     3. Reads the logits tensor, applies softmax, picks the top-K
///        indices.
///     4. Resolves indices to human-readable strings via `ImageNetLabels`.
///
/// SOLID:
///   * S -- the class is the glue between three collaborators
///         (preprocessor, labels, ORT session). Each collaborator owns
///         its own job. This file only orchestrates.
///   * L -- substitutable wherever `ImageClassifier&` appears; the
///         throwing contract matches the abstract base.
///   * D -- the constructor takes references to the abstractions
///         (`const Preprocessor&`, `const ImageNetLabels&`), not
///         concrete classes, so a test can pair an `OnnxImageClassifier`
///         with a deterministic preprocessor or in-memory labels.
///
/// Threading: ONNX Runtime sessions are NOT thread-safe for concurrent
/// `Run` calls. If the future GUI calls into one classifier from
/// multiple threads, wrap with an executor or per-thread session pool
/// at that boundary; this class will not synchronise internally.
///
/// Pimpl: ORT headers stay out of the public surface so consumers
/// compiled without ONNX Runtime headers (the foundation tests, for
/// instance) still link against the rest of the ML library.
class OnnxImageClassifier final : public ImageClassifier {
public:
    /// Load `modelPath` into a CPU-execution-provider session. Throws
    /// `std::runtime_error` if the file is missing or ORT fails to
    /// build the session (corrupted opset, unsupported op).
    OnnxImageClassifier(std::filesystem::path modelPath,
                        const Preprocessor& preprocessor,
                        const ImageNetLabels& labels);

    ~OnnxImageClassifier() override;

    OnnxImageClassifier(const OnnxImageClassifier&) = delete;
    OnnxImageClassifier& operator=(const OnnxImageClassifier&) = delete;
    OnnxImageClassifier(OnnxImageClassifier&&) = delete;
    OnnxImageClassifier& operator=(OnnxImageClassifier&&) = delete;

    [[nodiscard]] std::vector<Classification>
        classifyTopK(const Image& image, int k) const override;

    [[nodiscard]] std::string name() const override;

private:
    /// Holds the ORT environment + session. Forward-declared here, defined
    /// in the .cpp so the ORT headers never escape this translation unit.
    struct Session;

    std::unique_ptr<Session> session_;
    const Preprocessor& preprocessor_;
    const ImageNetLabels& labels_;
    std::string name_;
};

}  // namespace app::ml
