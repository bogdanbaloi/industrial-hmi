#include "src/ml/Image.h"
#include "src/ml/ImageNetPreprocessor.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr int kCropSize = 224;
constexpr int kChannels = 3;
constexpr std::int64_t kBatch = 1;
constexpr std::size_t kExpectedTensorSize =
    static_cast<std::size_t>(kBatch) *
    static_cast<std::size_t>(kChannels) *
    static_cast<std::size_t>(kCropSize) *
    static_cast<std::size_t>(kCropSize);

/// Build a uniform-colour RGB image of the requested dimensions.
/// Tests use this to feed the preprocessor a known input and check the
/// per-channel normalisation maths.
[[nodiscard]] app::ml::Image makeUniformImage(int width,
                                              int height,
                                              std::uint8_t r,
                                              std::uint8_t g,
                                              std::uint8_t b) {
    app::ml::Image image;
    image.width = width;
    image.height = height;
    image.channels = kChannels;
    image.pixels.reserve(static_cast<std::size_t>(width) *
                         static_cast<std::size_t>(height) *
                         static_cast<std::size_t>(kChannels));
    for (int i = 0; i < width * height; ++i) {
        image.pixels.push_back(r);
        image.pixels.push_back(g);
        image.pixels.push_back(b);
    }
    return image;
}

}  // namespace

TEST(ImageNetPreprocessorTest, OutputShapeIs1x3x224x224) {
    const app::ml::ImageNetPreprocessor pre;
    const auto shape = pre.outputShape();

    ASSERT_EQ(shape.size(), 4U);
    EXPECT_EQ(shape[0], 1);
    EXPECT_EQ(shape[1], 3);
    EXPECT_EQ(shape[2], 224);
    EXPECT_EQ(shape[3], 224);
}

TEST(ImageNetPreprocessorTest, NameIsImageNet) {
    const app::ml::ImageNetPreprocessor pre;
    EXPECT_EQ(pre.name(), "ImageNet");
}

TEST(ImageNetPreprocessorTest, EmptyImageThrows) {
    const app::ml::ImageNetPreprocessor pre;
    const app::ml::Image empty;
    EXPECT_THROW((void)pre.apply(empty), std::invalid_argument);
}

TEST(ImageNetPreprocessorTest, WrongChannelCountThrows) {
    const app::ml::ImageNetPreprocessor pre;

    app::ml::Image grayscale;
    grayscale.width = 256;
    grayscale.height = 256;
    grayscale.channels = 1;
    grayscale.pixels.assign(256U * 256U, 128U);

    EXPECT_THROW((void)pre.apply(grayscale), std::invalid_argument);
}

TEST(ImageNetPreprocessorTest, MismatchedBufferSizeThrows) {
    const app::ml::ImageNetPreprocessor pre;

    app::ml::Image bad;
    bad.width = 32;
    bad.height = 32;
    bad.channels = 3;
    bad.pixels.assign(10U, 0U);  // Far too small for 32x32x3.

    EXPECT_THROW((void)pre.apply(bad), std::invalid_argument);
}

TEST(ImageNetPreprocessorTest, OutputBufferHasExpectedFlatSize) {
    const app::ml::ImageNetPreprocessor pre;
    const app::ml::Image source = makeUniformImage(300, 300, 128U, 128U, 128U);
    const auto tensor = pre.apply(source);

    EXPECT_EQ(tensor.size(), kExpectedTensorSize);
}

TEST(ImageNetPreprocessorTest, UniformImageNormalisesToExpectedPerChannel) {
    // ImageNet normalisation:  (pixel/255 - mean[c]) / std[c]
    // For a uniform pixel value 128:
    //   scaled = 128/255 = 0.50196...
    //   ch0 (R, mean 0.485, std 0.229): (0.50196 - 0.485) / 0.229 ~= 0.0741
    //   ch1 (G, mean 0.456, std 0.224): (0.50196 - 0.456) / 0.224 ~= 0.2052
    //   ch2 (B, mean 0.406, std 0.225): (0.50196 - 0.406) / 0.225 ~= 0.4265
    constexpr float kExpectedR = 0.0741F;
    constexpr float kExpectedG = 0.2052F;
    constexpr float kExpectedB = 0.4265F;
    constexpr float kTolerance = 1e-3F;

    const app::ml::ImageNetPreprocessor pre;
    const app::ml::Image source = makeUniformImage(300, 300, 128U, 128U, 128U);
    const auto tensor = pre.apply(source);

    // CHW layout: plane c starts at index c * 224 * 224.
    constexpr std::size_t kPlaneSize =
        static_cast<std::size_t>(kCropSize) *
        static_cast<std::size_t>(kCropSize);

    // Check the centre pixel of each plane.
    constexpr std::size_t kCentre = (kPlaneSize / 2) + (kCropSize / 2);

    EXPECT_NEAR(tensor[(0U * kPlaneSize) + kCentre], kExpectedR, kTolerance);
    EXPECT_NEAR(tensor[(1U * kPlaneSize) + kCentre], kExpectedG, kTolerance);
    EXPECT_NEAR(tensor[(2U * kPlaneSize) + kCentre], kExpectedB, kTolerance);
}

TEST(ImageNetPreprocessorTest, ZeroPixelImageGivesNegativeMeanOverStd) {
    // pixel = 0  =>  scaled = 0  =>  output = -mean[c] / std[c].
    constexpr float kExpectedR = -0.485F / 0.229F;
    constexpr float kExpectedG = -0.456F / 0.224F;
    constexpr float kExpectedB = -0.406F / 0.225F;
    constexpr float kTolerance = 1e-4F;

    const app::ml::ImageNetPreprocessor pre;
    const app::ml::Image source = makeUniformImage(256, 256, 0U, 0U, 0U);
    const auto tensor = pre.apply(source);

    constexpr std::size_t kPlaneSize =
        static_cast<std::size_t>(kCropSize) *
        static_cast<std::size_t>(kCropSize);

    EXPECT_NEAR(tensor[0], kExpectedR, kTolerance);
    EXPECT_NEAR(tensor[1U * kPlaneSize], kExpectedG, kTolerance);
    EXPECT_NEAR(tensor[2U * kPlaneSize], kExpectedB, kTolerance);
}

TEST(ImageNetPreprocessorTest, SmallSourceImageStillProducesCorrectOutputSize) {
    // Even a tiny image must yield the canonical 1x3x224x224 tensor; the
    // resize step is responsible for stretching the short edge to 256
    // before centre-cropping to 224.
    const app::ml::ImageNetPreprocessor pre;
    const app::ml::Image tiny = makeUniformImage(32, 48, 200U, 100U, 50U);
    const auto tensor = pre.apply(tiny);

    EXPECT_EQ(tensor.size(), kExpectedTensorSize);
    EXPECT_TRUE(std::isfinite(tensor.front()));
    EXPECT_TRUE(std::isfinite(tensor.back()));
}
