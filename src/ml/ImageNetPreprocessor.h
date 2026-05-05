#pragma once

#include "src/ml/Image.h"
#include "src/ml/Preprocessor.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace app::ml {

/// Standard torchvision ImageNet inference pipeline:
///
///     1. Resize the short edge of the source image to 256 px,
///        preserving aspect ratio (bilinear interpolation).
///     2. Centre-crop a 224 x 224 region.
///     3. Convert to float32 and scale from [0, 255] to [0, 1].
///     4. Normalise per channel:
///            value = (value - mean[c]) / std[c]
///        with the canonical ImageNet mean/std published by torchvision.
///     5. Repack from HWC (decoder layout) to CHW (ONNX layout) and
///        prepend a batch axis to land at NCHW {1, 3, 224, 224}.
///
/// Matching torchvision exactly is what makes inference outputs match
/// training; any deviation here -- different resize filter, different
/// mean/std, different crop offset -- silently degrades accuracy
/// because the network sees inputs from a distribution different from
/// what its weights were tuned for.
class ImageNetPreprocessor final : public Preprocessor {
public:
    ImageNetPreprocessor() = default;
    ~ImageNetPreprocessor() override = default;

    ImageNetPreprocessor(const ImageNetPreprocessor&) = delete;
    ImageNetPreprocessor& operator=(const ImageNetPreprocessor&) = delete;
    ImageNetPreprocessor(ImageNetPreprocessor&&) = delete;
    ImageNetPreprocessor& operator=(ImageNetPreprocessor&&) = delete;

    [[nodiscard]] std::vector<float>
        apply(const Image& image) const override;

    [[nodiscard]] std::array<std::int64_t, 4>
        outputShape() const override;

    [[nodiscard]] std::string name() const override;
};

}  // namespace app::ml
