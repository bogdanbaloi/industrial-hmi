// Interface-contract tests for `ImageClassifier`, exercised through
// `FakeImageClassifier`. These pin down the behaviour every concrete
// implementation MUST honour (sorting, k-bound, throwing on bad input,
// stable name) so adding a new concrete (TensorRT, libtorch, ...) only
// has to satisfy the same suite.

#include "src/ml/Classification.h"
#include "src/ml/FakeImageClassifier.h"
#include "src/ml/Image.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] app::ml::Image makeDummyImage() {
    app::ml::Image img;
    img.width = 4;
    img.height = 4;
    img.channels = 3;
    img.pixels.assign(4U * 4U * 3U, 0U);
    return img;
}

[[nodiscard]] std::vector<app::ml::Classification> makeFiveCanned() {
    return {
        {0, "alpha",   0.50F},
        {1, "beta",    0.20F},
        {2, "gamma",   0.15F},
        {3, "delta",   0.10F},
        {4, "epsilon", 0.05F},
    };
}

}  // namespace

TEST(ImageClassifierContract, NameIsStable) {
    const app::ml::FakeImageClassifier classifier(
        makeFiveCanned(), "test-model");
    EXPECT_EQ(classifier.name(), "test-model");
    EXPECT_EQ(classifier.name(), "test-model");
}

TEST(ImageClassifierContract, ResultsAreSortedByDescendingConfidence) {
    const std::vector<app::ml::Classification> shuffled = {
        {3, "delta", 0.10F},
        {0, "alpha", 0.50F},
        {2, "gamma", 0.15F},
        {1, "beta",  0.20F},
    };
    const app::ml::FakeImageClassifier classifier(shuffled);
    const auto results = classifier.classifyTopK(makeDummyImage(), 4);

    ASSERT_EQ(results.size(), 4U);
    EXPECT_GT(results[0].confidence, results[1].confidence);
    EXPECT_GT(results[1].confidence, results[2].confidence);
    EXPECT_GT(results[2].confidence, results[3].confidence);
    EXPECT_EQ(results[0].label, "alpha");
}

TEST(ImageClassifierContract, TopKHonoursK) {
    const app::ml::FakeImageClassifier classifier(makeFiveCanned());
    const auto top3 = classifier.classifyTopK(makeDummyImage(), 3);

    ASSERT_EQ(top3.size(), 3U);
    EXPECT_EQ(top3[0].classId, 0);
    EXPECT_EQ(top3[1].classId, 1);
    EXPECT_EQ(top3[2].classId, 2);
}

TEST(ImageClassifierContract, KLargerThanResultsCountTruncatesNotPads) {
    const app::ml::FakeImageClassifier classifier(makeFiveCanned());
    const auto results = classifier.classifyTopK(makeDummyImage(), 50);
    EXPECT_EQ(results.size(), 5U);
}

TEST(ImageClassifierContract, KZeroThrows) {
    const app::ml::FakeImageClassifier classifier(makeFiveCanned());
    EXPECT_THROW(
        (void)classifier.classifyTopK(makeDummyImage(), 0),
        std::invalid_argument);
}

TEST(ImageClassifierContract, KNegativeThrows) {
    const app::ml::FakeImageClassifier classifier(makeFiveCanned());
    EXPECT_THROW(
        (void)classifier.classifyTopK(makeDummyImage(), -3),
        std::invalid_argument);
}

TEST(ImageClassifierContract, ResultsContainResolvedLabels) {
    const app::ml::FakeImageClassifier classifier(makeFiveCanned());
    const auto results = classifier.classifyTopK(makeDummyImage(), 2);

    ASSERT_EQ(results.size(), 2U);
    EXPECT_FALSE(results[0].label.empty());
    EXPECT_FALSE(results[1].label.empty());
}
