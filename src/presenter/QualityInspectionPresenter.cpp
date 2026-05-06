#include "src/presenter/QualityInspectionPresenter.h"

#include "src/ml/Classification.h"
#include "src/ml/Image.h"
#include "src/ml/ImageClassifier.h"
#include "src/ml/ImageDecoder.h"
#include "src/presenter/ViewObserver.h"
#include "src/presenter/modelview/InspectionResultViewModel.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace app::presenter {

QualityInspectionPresenter::QualityInspectionPresenter(
    const app::ml::ImageClassifier& classifier,
    const app::ml::ImageDecoder& decoder)
    : classifier_(classifier),
      decoder_(decoder) {}

void QualityInspectionPresenter::initialize() {
    // No model subscriptions -- the inspection workflow is fully
    // request/response: the view triggers `inspectFile`, the presenter
    // produces the result. There is nothing to wire in advance.
}

void QualityInspectionPresenter::setTopK(int k) {
    topK_ = std::max(1, k);
}

void QualityInspectionPresenter::inspectFile(
    const std::filesystem::path& path) {
    const std::string sourcePath = path.string();
    notifyAll(&ViewObserver::onInspectionStarted, sourcePath);

    const auto startTime = std::chrono::steady_clock::now();

    try {
        const app::ml::Image image = decoder_.decodeFile(path);
        std::vector<app::ml::Classification> classifications =
            classifier_.classifyTopK(image, topK_);

        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);

        InspectionResultViewModel viewModel;
        viewModel.sourcePath = sourcePath;
        viewModel.results = std::move(classifications);
        viewModel.latency = elapsed;

        notifyAll(&ViewObserver::onInspectionCompleted, viewModel);
    } catch (const std::exception& exc) {
        notifyAll(&ViewObserver::onInspectionFailed,
                  sourcePath, std::string(exc.what()));
    } catch (...) {
        notifyAll(&ViewObserver::onInspectionFailed,
                  sourcePath, std::string("unknown error"));
    }
}

}  // namespace app::presenter
