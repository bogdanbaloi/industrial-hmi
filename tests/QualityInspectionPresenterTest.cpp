#include "src/ml/Classification.h"
#include "src/ml/FakeImageClassifier.h"
#include "src/ml/ImageDecoder.h"
#include "src/presenter/QualityInspectionPresenter.h"
#include "src/presenter/ViewObserver.h"
#include "src/presenter/modelview/InspectionResultViewModel.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

/// Captures every observer notification the presenter emits. The
/// presenter is synchronous, so the test reads back the deltas
/// directly after each call without any threading machinery.
class CapturingObserver : public app::ViewObserver {
public:
    std::vector<std::string> startedPaths;
    std::vector<app::presenter::InspectionResultViewModel> completed;
    std::vector<std::pair<std::string, std::string>> failed;

    void onInspectionStarted(const std::string& sourcePath) override {
        startedPaths.push_back(sourcePath);
    }
    void onInspectionCompleted(
        const app::presenter::InspectionResultViewModel& vm) override {
        completed.push_back(vm);
    }
    void onInspectionFailed(const std::string& sourcePath,
                            const std::string& errorMessage) override {
        failed.emplace_back(sourcePath, errorMessage);
    }
};

/// 1x1 RGB BMP -- same fixture the ImageDecoder test uses, repeated
/// here so the presenter test does not depend on a separate file.
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

class TempBmpFile {
public:
    TempBmpFile() {
        path_ = std::filesystem::temp_directory_path() /
                ("inspection-fixture-" +
                 std::to_string(std::rand()) + ".bmp");
        std::ofstream out(path_, std::ios::binary);
        out.write(reinterpret_cast<const char*>(kOnePixelRedBmp.data()),
                  static_cast<std::streamsize>(kOnePixelRedBmp.size()));
    }
    ~TempBmpFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempBmpFile(const TempBmpFile&) = delete;
    TempBmpFile& operator=(const TempBmpFile&) = delete;
    TempBmpFile(TempBmpFile&&) = delete;
    TempBmpFile& operator=(TempBmpFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<app::ml::Classification> twoCannedRows() {
    return {
        {7, "tench",    0.80F},
        {1, "goldfish", 0.15F},
    };
}

}  // namespace

TEST(QualityInspectionPresenterTest, SuccessEmitsStartedThenCompleted) {
    app::ml::FakeImageClassifier classifier(twoCannedRows());
    const app::ml::ImageDecoder decoder;
    app::presenter::QualityInspectionPresenter presenter(classifier, decoder);

    CapturingObserver observer;
    presenter.addObserver(&observer);

    const TempBmpFile fixture;
    presenter.inspectFile(fixture.path());

    ASSERT_EQ(observer.startedPaths.size(), 1U);
    EXPECT_EQ(observer.startedPaths[0], fixture.path().string());

    ASSERT_EQ(observer.completed.size(), 1U);
    EXPECT_TRUE(observer.failed.empty());
    EXPECT_EQ(observer.completed[0].sourcePath, fixture.path().string());
    EXPECT_FALSE(observer.completed[0].results.empty());
    EXPECT_GE(observer.completed[0].latency.count(), 0);
}

TEST(QualityInspectionPresenterTest, ResultsPreserveTopKOrdering) {
    app::ml::FakeImageClassifier classifier(twoCannedRows());
    const app::ml::ImageDecoder decoder;
    app::presenter::QualityInspectionPresenter presenter(classifier, decoder);

    CapturingObserver observer;
    presenter.addObserver(&observer);

    const TempBmpFile fixture;
    presenter.inspectFile(fixture.path());

    ASSERT_EQ(observer.completed.size(), 1U);
    const auto& results = observer.completed[0].results;
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].classId, 7);
    EXPECT_EQ(results[0].label, "tench");
    EXPECT_GE(results[0].confidence, results[1].confidence);
}

TEST(QualityInspectionPresenterTest, MissingFileEmitsFailed) {
    app::ml::FakeImageClassifier classifier(twoCannedRows());
    const app::ml::ImageDecoder decoder;
    app::presenter::QualityInspectionPresenter presenter(classifier, decoder);

    CapturingObserver observer;
    presenter.addObserver(&observer);

    const std::filesystem::path missing("/no/such/image/at/all.png");
    presenter.inspectFile(missing);

    ASSERT_EQ(observer.startedPaths.size(), 1U);
    EXPECT_TRUE(observer.completed.empty());
    ASSERT_EQ(observer.failed.size(), 1U);
    EXPECT_EQ(observer.failed[0].first, missing.string());
    EXPECT_FALSE(observer.failed[0].second.empty());
}

TEST(QualityInspectionPresenterTest, SetTopKClampsToOneOrMore) {
    app::ml::FakeImageClassifier classifier(twoCannedRows());
    const app::ml::ImageDecoder decoder;
    app::presenter::QualityInspectionPresenter presenter(classifier, decoder);

    presenter.setTopK(0);
    EXPECT_EQ(presenter.topK(), 1);

    presenter.setTopK(-3);
    EXPECT_EQ(presenter.topK(), 1);

    presenter.setTopK(7);
    EXPECT_EQ(presenter.topK(), 7);
}

TEST(QualityInspectionPresenterTest, TopKControlsClassifierAsk) {
    app::ml::FakeImageClassifier classifier(twoCannedRows());
    const app::ml::ImageDecoder decoder;
    app::presenter::QualityInspectionPresenter presenter(classifier, decoder);

    presenter.setTopK(1);

    CapturingObserver observer;
    presenter.addObserver(&observer);

    const TempBmpFile fixture;
    presenter.inspectFile(fixture.path());

    ASSERT_EQ(observer.completed.size(), 1U);
    EXPECT_EQ(observer.completed[0].results.size(), 1U);
}

TEST(QualityInspectionPresenterTest, RemoveObserverStopsNotifications) {
    app::ml::FakeImageClassifier classifier(twoCannedRows());
    const app::ml::ImageDecoder decoder;
    app::presenter::QualityInspectionPresenter presenter(classifier, decoder);

    CapturingObserver observer;
    presenter.addObserver(&observer);
    presenter.removeObserver(&observer);

    const TempBmpFile fixture;
    presenter.inspectFile(fixture.path());

    EXPECT_TRUE(observer.startedPaths.empty());
    EXPECT_TRUE(observer.completed.empty());
    EXPECT_TRUE(observer.failed.empty());
}
