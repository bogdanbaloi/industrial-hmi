#pragma once

// ViewObserver.h (and the view-model headers it pulls) MUST precede any Qt
// header (the wingdi ERROR macro gotcha).
#include "src/presenter/ViewObserver.h"

#include <QWidget>

#include <memory>
#include <string>

class QEvent;

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtGoodsReceiptPage;
}

namespace app::presenter {
class QualityInspectionPresenter;
}

namespace app::view {

/// Goods-receipt inspection page: pick an image of an incoming shipment and run
/// it through the real `QualityInspectionPresenter` (decode -> classify ->
/// top-K), the same presenter the GTK Quality Inspection page drives. Reusing
/// the Edge-AI inspection presenter behind a second toolkit is another slice of
/// the toolkit-independence proof.
///
/// The classifier injected here is the project's `FakeImageClassifier` (canned
/// results), so the page decodes the image for real but the classification is a
/// fixed demo sample -- surfaced plainly in the UI, never passed off as a
/// trained model's verdict. Wiring a real ONNX classifier would not change this
/// view (dependency inversion).
///
/// Threading: inspection is synchronous on the UI thread (run from the button
/// click), so the callbacks arrive on the UI thread and render directly.
class QtGoodsReceiptPage : public QWidget, public app::ViewObserver {
public:
    explicit QtGoodsReceiptPage(presenter::QualityInspectionPresenter& presenter,
                                QWidget* parent = nullptr);
    ~QtGoodsReceiptPage() override;

    QtGoodsReceiptPage(const QtGoodsReceiptPage&)            = delete;
    QtGoodsReceiptPage& operator=(const QtGoodsReceiptPage&) = delete;
    QtGoodsReceiptPage(QtGoodsReceiptPage&&)                 = delete;
    QtGoodsReceiptPage& operator=(QtGoodsReceiptPage&&)      = delete;

    void onInspectionStarted(const std::string& sourcePath) override;
    void onInspectionCompleted(
        const presenter::InspectionResultViewModel& viewModel) override;
    void onInspectionFailed(const std::string& sourcePath,
                            const std::string& message) override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void chooseAndInspect();
    void clearResults();

    presenter::QualityInspectionPresenter&  presenter_;
    std::unique_ptr<Ui::QtGoodsReceiptPage> ui_;
};

}  // namespace app::view
