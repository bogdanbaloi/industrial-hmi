#pragma once

#include "src/ml/ImageClassifier.h"
#include "src/ml/ImageDecoder.h"
#include "src/presenter/BasePresenter.h"

#include <chrono>
#include <filesystem>

namespace app::presenter {

/// Drives the Edge AI inspection workflow:
///
///     file path -> decode -> classify -> top-K view model -> observers
///
/// Synchronous on the caller's thread. The view is responsible for
/// wrapping each `inspectFile` call in a worker thread (`std::jthread`)
/// and marshaling the resulting observer callbacks back onto the GTK
/// main loop via `Glib::signal_idle` -- the same pattern
/// `DatabaseManager` uses for its async SQLite calls. Keeping the
/// presenter free of threading primitives makes it trivially
/// unit-testable: a synchronous test can call `inspectFile()` and
/// inspect what was sent to a captured `ViewObserver`.
///
/// SOLID:
///   * S -- one job: orchestrate one inspection. No file dialogs, no
///         GTK widgets, no model loading. Decoder and classifier are
///         injected; the presenter only sequences them.
///   * O -- a different `ImageClassifier` or `ImageDecoder` plugs in
///         through the constructor; nothing here cares which concrete
///         is on the other side.
///   * L -- the presenter respects `BasePresenter`'s observer contract;
///         tests substitute any `ViewObserver` and read back the
///         notifications.
///   * D -- depends on the abstract `ImageClassifier` / `ImageDecoder`
///         and emits via `ViewObserver`. No concrete dependency leaks
///         into this file.
class QualityInspectionPresenter : public BasePresenter {
public:
    /// Default top-K asked of the classifier on each inspection. The
    /// view can override per-instance via `setTopK`; 5 matches what
    /// every standard ImageNet demo has trained operators to expect.
    static constexpr int kDefaultTopK = 5;

    QualityInspectionPresenter(const app::ml::ImageClassifier& classifier,
                               const app::ml::ImageDecoder& decoder);

    ~QualityInspectionPresenter() override = default;

    QualityInspectionPresenter(const QualityInspectionPresenter&) = delete;
    QualityInspectionPresenter&
        operator=(const QualityInspectionPresenter&) = delete;
    QualityInspectionPresenter(QualityInspectionPresenter&&) = delete;
    QualityInspectionPresenter&
        operator=(QualityInspectionPresenter&&) = delete;

    void initialize() override;

    /// Run one inspection synchronously. Notifies observers in this
    /// order:
    ///
    ///     onInspectionStarted(path)
    ///     onInspectionCompleted(viewModel)   on success
    ///     onInspectionFailed(path, message)  on decode / inference error
    ///
    /// Exactly one of `onInspectionCompleted` / `onInspectionFailed`
    /// fires per call. Never throws -- exceptions from the decoder or
    /// classifier are caught and routed to the failed observer so the
    /// caller does not need a try/catch.
    void inspectFile(const std::filesystem::path& path);

    /// Configure how many top-K rows the view receives. Defaults to
    /// `kDefaultTopK`; clamps non-positive values to 1.
    void setTopK(int k);

    /// Read-only accessor for the currently configured top-K. Useful
    /// for tests and for the view's "show top N results" UI text.
    [[nodiscard]] int topK() const { return topK_; }

private:
    const app::ml::ImageClassifier& classifier_;
    const app::ml::ImageDecoder& decoder_;
    int topK_ = kDefaultTopK;
};

}  // namespace app::presenter
