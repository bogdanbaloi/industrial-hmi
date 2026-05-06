// Live integration test for `OnnxImageClassifier`.
//
// Loads the INT8 MobileNetV2 model produced by
// `scripts/ml/quantize_model.py`, feeds it a synthetic 1x1 BMP that
// the preprocessor stretches to 224x224, and verifies the classifier
// returns a non-empty, well-formed top-K result.
//
// We deliberately do NOT assert on a specific class id -- the input
// is deterministic but meaningless to the network, so the predicted
// label is essentially noise. The point is to exercise the full glue:
// model load, preprocessing, ORT Run, softmax, label resolution,
// throwing on bad input.
//
// Skipped at run time when the model file is not present (e.g. on a
// fresh checkout that has not yet executed the Python pipeline) so the
// test binary stays compilable and runnable in any environment that
// also has BUILD_ML_CLASSIFIER turned on.

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageDecoder.h"
#include "src/ml/ImageNetLabels.h"
#include "src/ml/ImageNetPreprocessor.h"
#include "src/ml/OnnxImageClassifier.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

namespace {

/// The integration model lives next to the FP32 export under
/// assets/models/. Allow override via env so CI and developer machines
/// can point elsewhere (model in artifact cache, model in /tmp, ...).
[[nodiscard]] std::filesystem::path resolveModelPath() {
    if (const char* override = std::getenv("INDUSTRIAL_HMI_INT8_MODEL");
        override != nullptr) {
        return std::filesystem::path(override);
    }
    return std::filesystem::path("assets/models/mobilenetv2_int8.onnx");
}

[[nodiscard]] std::filesystem::path resolveLabelsPath() {
    if (const char* override = std::getenv("INDUSTRIAL_HMI_LABELS");
        override != nullptr) {
        return std::filesystem::path(override);
    }
    return std::filesystem::path("assets/models/imagenet_labels.txt");
}

[[nodiscard]] bool prerequisitesPresent() {
    return std::filesystem::exists(resolveModelPath()) &&
           std::filesystem::exists(resolveLabelsPath());
}

[[nodiscard]] app::ml::Image loadFixtureOrSynth() {
    // Reuse the 1x1 red BMP from ImageDecoderTest by hand-rolling it
    // here (small, deterministic, no extra fixture file needed). The
    // preprocessor stretches it to 224x224 so the network sees a
    // single colour fed through the ImageNet pipeline.
    constexpr std::array<std::uint8_t, 58> kOnePixelRedBmp = {
        0x42, 0x4D,
        0x3A, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x01, 0x00,
        0x18, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF,
        0x00,
    };
    const app::ml::ImageDecoder decoder;
    return decoder.decodeMemory(
        std::span<const std::uint8_t>(kOnePixelRedBmp));
}

}  // namespace

TEST(OnnxImageClassifierTest, LoadsModelAndProducesTopK) {
    if (!prerequisitesPresent()) {
        GTEST_SKIP() << "Model or labels not found; run "
                     << "scripts/ml/quantize_model.py first.";
    }

    const app::ml::ImageNetPreprocessor preprocessor;
    const app::ml::ImageNetLabels labels(resolveLabelsPath());

    const app::ml::OnnxImageClassifier classifier(
        resolveModelPath(), preprocessor, labels);

    EXPECT_FALSE(classifier.name().empty());

    const auto image = loadFixtureOrSynth();
    const auto results = classifier.classifyTopK(image, 5);

    ASSERT_EQ(results.size(), 5U);

    // Top entry must beat the rest.
    for (std::size_t i = 1; i < results.size(); ++i) {
        EXPECT_GE(results[0].confidence, results[i].confidence);
    }

    // Probabilities are bounded.
    for (const auto& entry : results) {
        EXPECT_GE(entry.confidence, 0.0F);
        EXPECT_LE(entry.confidence, 1.0F);
        EXPECT_FALSE(entry.label.empty());
        EXPECT_GE(entry.classId, 0);
    }
}

TEST(OnnxImageClassifierTest, MissingModelPathThrowsAtConstruction) {
    if (!std::filesystem::exists(resolveLabelsPath())) {
        GTEST_SKIP() << "Labels missing; cannot construct classifier "
                     << "to test the model-not-found path.";
    }
    const app::ml::ImageNetPreprocessor preprocessor;
    const app::ml::ImageNetLabels labels(resolveLabelsPath());

    EXPECT_THROW(
        app::ml::OnnxImageClassifier(
            std::filesystem::path("/no/such/model.onnx"),
            preprocessor, labels),
        std::runtime_error);
}

TEST(OnnxImageClassifierTest, KZeroThrows) {
    if (!prerequisitesPresent()) {
        GTEST_SKIP() << "Model or labels not found.";
    }
    const app::ml::ImageNetPreprocessor preprocessor;
    const app::ml::ImageNetLabels labels(resolveLabelsPath());
    const app::ml::OnnxImageClassifier classifier(
        resolveModelPath(), preprocessor, labels);

    EXPECT_THROW(
        (void)classifier.classifyTopK(loadFixtureOrSynth(), 0),
        std::invalid_argument);
}
