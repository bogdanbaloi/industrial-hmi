#pragma once

#include "src/gtk/view/pages/Page.h"
#include "src/presenter/QualityInspectionPresenter.h"
#include "src/presenter/ViewObserver.h"
#include "src/presenter/modelview/InspectionResultViewModel.h"

#include <gtkmm.h>
#include <gtkmm/cssprovider.h>
#include <glibmm/refptr.h>

#include <memory>
#include <string>
#include <thread>

class QualityInspectionPageTest;  // forward-declare gtest fixture

namespace app::view {

class DialogManager;

/// Edge AI quality inspection page.
///
/// Layout (top to bottom):
///   * Toolbar row: "Choose image..." button + status label.
///   * Image preview (`Gtk::Picture`) -- scales to fit, preserves aspect.
///   * Top-K results list -- one row per classification, each row showing
///     label + percentage and a horizontal progress bar for visual scan.
///
/// Threading model:
///
///     UI thread              Worker thread (std::jthread)
///     ---------              -----------------------------
///     onChooseFile           presenter_->inspectFile(path)
///       file dialog
///       spawn worker  -----> decode + classify
///       (returns)            notify ViewObserver (this)  -- on worker
///                                              |
///                                              v
///     onInspection*         Glib::signal_idle.connect_once(render*)
///     callback (on worker)  -- arrives back on main loop
///       render*  --------- pure widget updates, GTK-safe
///
/// `Glib::signal_idle` is the same marshalling primitive `DatabaseManager`
/// uses for SQLite callbacks; the presenter is unaware of threading.
///
/// SOLID:
///   * S -- one job: present an inspection workflow. Decoding,
///         classification, label resolution all live in the
///         presenter / classifier / decoder behind the scenes.
///   * L -- substitutable wherever a `Page` is required; MainWindow
///         registers it like any other tab.
///   * D -- depends on `QualityInspectionPresenter` (concrete only
///         because we need DI of a specific presenter type at the
///         page boundary; the presenter itself depends on the
///         `ImageClassifier` / `ImageDecoder` abstractions).
class QualityInspectionPage : public Page, public ViewObserver {
    friend class ::QualityInspectionPageTest;

public:
    explicit QualityInspectionPage(DialogManager& dialogManager);
    ~QualityInspectionPage() override;

    QualityInspectionPage(const QualityInspectionPage&) = delete;
    QualityInspectionPage& operator=(const QualityInspectionPage&) = delete;
    QualityInspectionPage(QualityInspectionPage&&) = delete;
    QualityInspectionPage& operator=(QualityInspectionPage&&) = delete;

    /// Wire the page to a presenter and register as an observer. Calling
    /// `initialize` more than once is a programmer error -- the previous
    /// observer is left registered.
    void initialize(
        std::shared_ptr<presenter::QualityInspectionPresenter> presenter);

    [[nodiscard]] Glib::ustring pageTitle() const override;

    /// ViewObserver -- only the three inspection callbacks. Everything
    /// else inherits the empty default.
    void onInspectionStarted(const std::string& sourcePath) override;
    void onInspectionCompleted(
        const presenter::InspectionResultViewModel& viewModel) override;
    void onInspectionFailed(const std::string& sourcePath,
                            const std::string& errorMessage) override;

private:
    void buildUi();
    void applyStyles();
    void onChooseFileClicked();
    void runInspection(const std::string& path);

    /// All three render* helpers run on the GTK main loop. They take
    /// values by value so the lambdas posted via `Glib::signal_idle`
    /// are independent of the presenter-side lifetime.
    void renderStarted(std::string sourcePath);
    void renderCompleted(presenter::InspectionResultViewModel viewModel);
    void renderFailed(std::string sourcePath, std::string errorMessage);

    /// Replace the result list contents. `clearResultRows` is shared
    /// between Started (empty list, "Inspecting..." placeholder),
    /// Completed (top-K rows), and Failed (empty + error label).
    void clearResultRows();

    std::shared_ptr<presenter::QualityInspectionPresenter> presenter_;

    // Widgets -- pointers into the GtkBuilder-owned tree loaded from
    // assets/ui/inspection-page.ui. Lifetime is tied to the page (we
    // append the root Box; everything else hangs off it).
    Gtk::Button*  chooseButton_ = nullptr;
    Gtk::Label*   statusLabel_  = nullptr;
    Gtk::Picture* preview_      = nullptr;
    Gtk::Box*     resultsBox_   = nullptr;

    /// Page-scoped CSS provider so the inspection.css rules only
    /// apply while this page is alive. Same pattern Dashboard /
    /// Products pages use.
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;

    /// Background thread that drives one inspection. Storing it as a
    /// member means the previous run is joined automatically when the
    /// user fires a new one (jthread destructor on assignment), which
    /// also serialises inspections by construction. The button is also
    /// disabled while running for obvious UX reasons.
    std::jthread worker_;
};

}  // namespace app::view
