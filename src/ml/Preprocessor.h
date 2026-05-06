#pragma once

#include "src/ml/Image.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace app::ml {

/// Converts a raw decoded `Image` into the float tensor an ONNX model
/// expects on its input port.
///
/// The interface stays narrow on purpose: in / out is one image, one
/// tensor. Anything that varies between training pipelines (resize
/// strategy, crop strategy, mean / std vectors, channel order) is the
/// responsibility of the concrete implementation.
///
/// SOLID:
///   * S -- one job: pixel buffer to model-ready tensor. No file I/O,
///         no model loading, no labels.
///   * O -- a new pipeline (e.g. ResNet preprocessing, segmentation
///         padding-keep-aspect) is a new subclass; existing call sites
///         stay untouched.
///   * L -- callers depend on `Preprocessor&`, never on a concrete
///         pipeline; any subclass is a drop-in replacement.
///   * I -- the interface exposes only what every classifier needs;
///         model-specific knobs (e.g. tile size for segmentation)
///         do NOT pollute it.
///   * D -- the classifier owns a `Preprocessor&`, so unit tests can
///         pass a deterministic stand-in (identity, all-zeros) without
///         decoding real images.
class Preprocessor {
public:
    virtual ~Preprocessor() = default;

    Preprocessor(const Preprocessor&) = delete;
    Preprocessor& operator=(const Preprocessor&) = delete;
    Preprocessor(Preprocessor&&) = delete;
    Preprocessor& operator=(Preprocessor&&) = delete;

    /// Apply the pipeline to one image. Returns a float buffer in NCHW
    /// layout matching `outputShape()`. Throws `std::invalid_argument`
    /// if the input is empty or has an unsupported channel count.
    [[nodiscard]] virtual std::vector<float>
        apply(const Image& image) const = 0;

    /// Shape of the produced tensor in NCHW order: {batch, channels,
    /// height, width}. Stable for the lifetime of the object so callers
    /// can reuse it when binding to an ONNX session.
    [[nodiscard]] virtual std::array<std::int64_t, 4>
        outputShape() const = 0;

    /// Display-friendly name (e.g. "ImageNet"). Used in log lines and
    /// future UI dropdowns. Stable -- do not localise.
    [[nodiscard]] virtual std::string name() const = 0;

protected:
    Preprocessor() = default;
};

}  // namespace app::ml
