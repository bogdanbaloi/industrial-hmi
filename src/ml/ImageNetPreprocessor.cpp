#include "src/ml/ImageNetPreprocessor.h"

#include "src/ml/Image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace app::ml {

namespace {

/// All knobs published as named constants -- `magic numbers` in the
/// pipeline are the single most common source of "model works, but
/// accuracy is off" bugs in deployed inference code.

/// Short-edge target after the resize step. torchvision's default for
/// MobileNet / ResNet / VGG inference.
constexpr int kResizeShortEdge = 256;

/// Centre-crop output size. Matches the spatial input every torchvision
/// ImageNet checkpoint was trained at.
constexpr int kCropSize = 224;

/// Channel count. Every supported pre-trained model is RGB; the rest of
/// the pipeline asserts on this.
constexpr int kChannels = 3;

/// Inference always runs single-image batches in this code path. The
/// model accepts dynamic batch (we set a dynamic axis at export time),
/// so changing this value would require updating only the tensor wiring,
/// not the preprocessing.
constexpr std::int64_t kBatchSize = 1;

/// Pixel value range coming out of the decoder.
constexpr float kPixelScaleMax = 255.0F;

/// ImageNet normalisation constants -- byte-identical to torchvision's
/// `transforms.Normalize` defaults. Rebuilding the values from a paper
/// is a recipe for accuracy drift; pin them here verbatim instead.
constexpr std::array<float, kChannels> kImageNetMean = {
    0.485F, 0.456F, 0.406F,
};
constexpr std::array<float, kChannels> kImageNetStd = {
    0.229F, 0.224F, 0.225F,
};

/// Returns the index of pixel (x, y, c) in a row-major,
/// channel-interleaved buffer of size `width * height * kChannels`.
[[nodiscard]] std::size_t pixelIndex(int x, int y, int c, int width) {
    const auto wide = static_cast<std::size_t>(width);
    const auto chans = static_cast<std::size_t>(kChannels);
    return ((static_cast<std::size_t>(y) * wide) +
            static_cast<std::size_t>(x)) * chans +
           static_cast<std::size_t>(c);
}

/// Clamp a floating-point source coordinate into the legal range of an
/// integer source axis. Using the half-pixel convention (`+0.5` in the
/// callers) lines the result up with how torchvision and PIL sample
/// pixels, which is what the published top-1 accuracy figures assume.
[[nodiscard]] int clampToAxis(int value, int axisLength) {
    if (value < 0) {
        return 0;
    }
    if (value >= axisLength) {
        return axisLength - 1;
    }
    return value;
}

/// Bilinear resize from `src` (any aspect ratio) to a fixed
/// `dstWidth x dstHeight` RGB buffer.
[[nodiscard]] std::vector<std::uint8_t>
    bilinearResize(const Image& src, int dstWidth, int dstHeight) {
    std::vector<std::uint8_t> dst(
        static_cast<std::size_t>(dstWidth) *
        static_cast<std::size_t>(dstHeight) *
        static_cast<std::size_t>(kChannels));

    const float xRatio = static_cast<float>(src.width) /
                         static_cast<float>(dstWidth);
    const float yRatio = static_cast<float>(src.height) /
                         static_cast<float>(dstHeight);

    for (int yo = 0; yo < dstHeight; ++yo) {
        // +0.5 / -0.5 -> half-pixel coordinates, matches PIL / torchvision.
        const float ys = ((static_cast<float>(yo) + 0.5F) * yRatio) - 0.5F;
        const int y0Raw = static_cast<int>(std::floor(ys));
        const int y0 = clampToAxis(y0Raw, src.height);
        const int y1 = clampToAxis(y0Raw + 1, src.height);
        const float dy = ys - static_cast<float>(y0Raw);

        for (int xo = 0; xo < dstWidth; ++xo) {
            const float xs = ((static_cast<float>(xo) + 0.5F) * xRatio) - 0.5F;
            const int x0Raw = static_cast<int>(std::floor(xs));
            const int x0 = clampToAxis(x0Raw, src.width);
            const int x1 = clampToAxis(x0Raw + 1, src.width);
            const float dx = xs - static_cast<float>(x0Raw);

            for (int c = 0; c < kChannels; ++c) {
                const float p00 = static_cast<float>(
                    src.pixels[pixelIndex(x0, y0, c, src.width)]);
                const float p10 = static_cast<float>(
                    src.pixels[pixelIndex(x1, y0, c, src.width)]);
                const float p01 = static_cast<float>(
                    src.pixels[pixelIndex(x0, y1, c, src.width)]);
                const float p11 = static_cast<float>(
                    src.pixels[pixelIndex(x1, y1, c, src.width)]);

                const float top = (p00 * (1.0F - dx)) + (p10 * dx);
                const float bot = (p01 * (1.0F - dx)) + (p11 * dx);
                const float out = (top * (1.0F - dy)) + (bot * dy);

                dst[pixelIndex(xo, yo, c, dstWidth)] =
                    static_cast<std::uint8_t>(
                        std::clamp(out, 0.0F, kPixelScaleMax));
            }
        }
    }
    return dst;
}

/// Compute the post-resize dimensions when the short edge is forced to
/// `kResizeShortEdge` and aspect ratio is preserved.
struct ResizedDimensions {
    int width;
    int height;
};

[[nodiscard]] ResizedDimensions
    computeResizedDimensions(int srcWidth, int srcHeight) {
    if (srcWidth <= 0 || srcHeight <= 0) {
        throw std::invalid_argument(
            "ImageNetPreprocessor: source image has non-positive dimensions.");
    }

    if (srcWidth < srcHeight) {
        const float scale = static_cast<float>(kResizeShortEdge) /
                            static_cast<float>(srcWidth);
        return ResizedDimensions{
            kResizeShortEdge,
            static_cast<int>(std::round(
                static_cast<float>(srcHeight) * scale)),
        };
    }
    const float scale = static_cast<float>(kResizeShortEdge) /
                        static_cast<float>(srcHeight);
    return ResizedDimensions{
        static_cast<int>(std::round(
            static_cast<float>(srcWidth) * scale)),
        kResizeShortEdge,
    };
}

}  // namespace

std::vector<float> ImageNetPreprocessor::apply(const Image& image) const {
    if (image.pixels.empty()) {
        throw std::invalid_argument(
            "ImageNetPreprocessor: input image has no pixels.");
    }
    if (image.channels != kChannels) {
        throw std::invalid_argument(
            "ImageNetPreprocessor: expected RGB (3 channels), got " +
            std::to_string(image.channels));
    }
    const std::size_t expectedBytes =
        static_cast<std::size_t>(image.width) *
        static_cast<std::size_t>(image.height) *
        static_cast<std::size_t>(image.channels);
    if (image.pixels.size() != expectedBytes) {
        throw std::invalid_argument(
            "ImageNetPreprocessor: pixel buffer size does not match "
            "width * height * channels.");
    }

    // Step 1: resize short edge to 256, preserving aspect.
    const ResizedDimensions resized =
        computeResizedDimensions(image.width, image.height);
    const std::vector<std::uint8_t> resizedPixels = bilinearResize(
        image, resized.width, resized.height);

    // Step 2: centre-crop kCropSize x kCropSize.
    const int cropOriginX = (resized.width - kCropSize) / 2;
    const int cropOriginY = (resized.height - kCropSize) / 2;

    // Step 3-5: scale to [0,1], normalise, repack HWC -> CHW with batch axis.
    std::vector<float> tensor(
        static_cast<std::size_t>(kBatchSize) *
        static_cast<std::size_t>(kChannels) *
        static_cast<std::size_t>(kCropSize) *
        static_cast<std::size_t>(kCropSize));

    const std::size_t planeSize =
        static_cast<std::size_t>(kCropSize) *
        static_cast<std::size_t>(kCropSize);

    for (int y = 0; y < kCropSize; ++y) {
        for (int x = 0; x < kCropSize; ++x) {
            const int srcX = cropOriginX + x;
            const int srcY = cropOriginY + y;
            for (int c = 0; c < kChannels; ++c) {
                const std::uint8_t raw = resizedPixels[
                    pixelIndex(srcX, srcY, c, resized.width)];
                const float scaled =
                    static_cast<float>(raw) / kPixelScaleMax;
                const float normalised =
                    (scaled - kImageNetMean.at(static_cast<std::size_t>(c))) /
                    kImageNetStd.at(static_cast<std::size_t>(c));

                const std::size_t outIndex =
                    (static_cast<std::size_t>(c) * planeSize) +
                    (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(kCropSize)) +
                    static_cast<std::size_t>(x);
                tensor[outIndex] = normalised;
            }
        }
    }
    return tensor;
}

std::array<std::int64_t, 4> ImageNetPreprocessor::outputShape() const {
    return {kBatchSize,
            static_cast<std::int64_t>(kChannels),
            static_cast<std::int64_t>(kCropSize),
            static_cast<std::int64_t>(kCropSize)};
}

std::string ImageNetPreprocessor::name() const {
    return "ImageNet";
}

}  // namespace app::ml
