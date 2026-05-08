#include "src/gtk/view/pages/QualityInspectionPage.h"

#include "src/core/i18n.h"
#include "src/gtk/view/DialogManager.h"

#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/main.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace app::view {

namespace {

/// Margins / spacing in pixels. Pulled out as named constants so the
/// page reads as configuration rather than a wall of magic literals.
constexpr int kRootSpacing = 12;
constexpr int kRootMarginPx = 16;
constexpr int kToolbarSpacing = 8;
constexpr int kResultRowSpacing = 6;
constexpr int kResultsBoxSpacing = 4;
constexpr int kPreviewMinHeight = 240;

/// Percentage scale factor: 0.42 confidence -> "42.0%".
constexpr float kPercentScale = 100.0F;

/// Mime types supported by the stb_image-backed decoder. Listed in
/// the file dialog filter so the picker shows only files we can
/// actually decode.
const std::array<const char*, 3> kSupportedMimeTypes = {
    "image/png",
    "image/jpeg",
    "image/bmp",
};

/// Build a single result row -- "1. tench, Tinca tinca   42.0%" with
/// a level bar underneath. Pure helper, no widget reuse: the page
/// rebuilds the result rows on every Completed callback.
[[nodiscard]] std::unique_ptr<Gtk::Box>
    makeResultRow(int rank, const ml::Classification& entry) {
    auto row = std::make_unique<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kResultRowSpacing);
    row->set_margin(kResultRowSpacing);

    auto* heading = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kResultRowSpacing);

    auto* labelText = Gtk::make_managed<Gtk::Label>(
        std::format("{}. {}", rank, entry.label));
    labelText->set_halign(Gtk::Align::START);
    labelText->set_hexpand(true);

    auto* percent = Gtk::make_managed<Gtk::Label>(
        std::format("{:.1f}%", entry.confidence * kPercentScale));
    percent->set_halign(Gtk::Align::END);

    heading->append(*labelText);
    heading->append(*percent);
    row->append(*heading);

    auto* bar = Gtk::make_managed<Gtk::LevelBar>();
    bar->set_min_value(0.0);
    bar->set_max_value(1.0);
    bar->set_value(static_cast<double>(entry.confidence));
    row->append(*bar);

    return row;
}

}  // namespace

QualityInspectionPage::QualityInspectionPage(DialogManager& dialogManager)
    : Page(dialogManager) {
    set_spacing(kRootSpacing);
    set_margin(kRootMarginPx);
    buildUi();
}

QualityInspectionPage::~QualityInspectionPage() {
    if (presenter_) {
        presenter_->removeObserver(this);
    }
    // jthread destructor sends stop_request and joins; the worker
    // body is a synchronous presenter call so it will return on its
    // own. No explicit stop required.
}

void QualityInspectionPage::initialize(
    std::shared_ptr<presenter::QualityInspectionPresenter> presenter) {
    presenter_ = std::move(presenter);
    if (presenter_) {
        presenter_->addObserver(this);
    }
}

Glib::ustring QualityInspectionPage::pageTitle() const {
    return _("Inspection");
}

void QualityInspectionPage::buildUi() {
    // Toolbar row -- choose file + idle status label.
    toolbarRow_.set_spacing(kToolbarSpacing);
    chooseButton_.set_label(_("Choose image..."));
    chooseButton_.signal_clicked().connect(
        sigc::mem_fun(*this, &QualityInspectionPage::onChooseFileClicked));
    statusLabel_.set_label(_("Pick an image to classify."));
    statusLabel_.set_halign(Gtk::Align::START);
    statusLabel_.set_hexpand(true);
    toolbarRow_.append(chooseButton_);
    toolbarRow_.append(statusLabel_);
    append(toolbarRow_);

    // Preview -- empty until the user picks a file.
    preview_.set_can_shrink(true);
    preview_.set_size_request(-1, kPreviewMinHeight);
    preview_.set_vexpand(true);
    append(preview_);

    // Results list inside a frame so the section is visually distinct
    // from the preview above it. The frame label tracks the configured
    // top-K via the presenter once initialise() runs.
    resultsBox_.set_spacing(kResultsBoxSpacing);
    resultsBox_.set_margin(kResultsBoxSpacing);
    resultsFrame_.set_child(resultsBox_);
    resultsFrame_.set_label(_("Top results"));
    append(resultsFrame_);
}

void QualityInspectionPage::onChooseFileClicked() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(_("Choose image"));

    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("Images (PNG / JPEG / BMP)"));
    for (const char* mime : kSupportedMimeTypes) {
        filter->add_mime_type(mime);
    }
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    auto* parentWindow = dynamic_cast<Gtk::Window*>(get_root());
    auto onFinish = [this, dialog](
            const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto file = dialog->open_finish(result);
            if (file) {
                runInspection(file->get_path());
            }
        }
        // User cancelled the picker -- Gtk::FileDialog signals this by
        // throwing Glib::Error("Dismissed by user") and there is no
        // separate API to detect cancellation, so an empty catch is
        // the documented happy-path handler.
        // NOLINTNEXTLINE(bugprone-empty-catch)
        catch (const Glib::Error&) {}
    };

    if (parentWindow != nullptr) {
        dialog->open(*parentWindow, onFinish);
    } else {
        dialog->open(onFinish);
    }
}

void QualityInspectionPage::runInspection(const std::string& path) {
    if (!presenter_) {
        renderFailed(path,
                     std::string("Presenter not initialised"));
        return;
    }

    // Disable the button while the worker runs; the renderCompleted /
    // renderFailed callbacks re-enable it. set_filename loads the
    // preview synchronously so the operator sees the chosen image
    // immediately, even before the model returns.
    chooseButton_.set_sensitive(false);
    preview_.set_filename(path);

    // jthread destructor on reassignment joins the previous worker
    // (no overlap), then we kick off the new one. The lambda owns
    // its captured path string.
    worker_ = std::jthread([this, path]() {
        presenter_->inspectFile(path);
    });
}

void QualityInspectionPage::onInspectionStarted(
    const std::string& sourcePath) {
    Glib::signal_idle().connect_once(
        [this, sourcePath]() {
            renderStarted(sourcePath);
        });
}

void QualityInspectionPage::onInspectionCompleted(
    const presenter::InspectionResultViewModel& viewModel) {
    Glib::signal_idle().connect_once(
        [this, viewModel]() {
            renderCompleted(viewModel);
        });
}

void QualityInspectionPage::onInspectionFailed(
    const std::string& sourcePath,
    const std::string& errorMessage) {
    Glib::signal_idle().connect_once(
        [this, sourcePath, errorMessage]() {
            renderFailed(sourcePath, errorMessage);
        });
}

void QualityInspectionPage::renderStarted(std::string /*sourcePath*/) {
    statusLabel_.set_label(_("Inspecting..."));
    clearResultRows();
}

void QualityInspectionPage::renderCompleted(
    presenter::InspectionResultViewModel viewModel) {
    chooseButton_.set_sensitive(true);

    statusLabel_.set_label(
        Glib::ustring::compose("%1 ms", viewModel.latency.count()));

    clearResultRows();
    int rank = 1;
    for (const auto& entry : viewModel.results) {
        resultsBox_.append(*makeResultRow(rank, entry).release());
        ++rank;
    }
}

void QualityInspectionPage::renderFailed(std::string /*sourcePath*/,
                                         std::string errorMessage) {
    chooseButton_.set_sensitive(true);
    statusLabel_.set_label(
        Glib::ustring::compose("%1: %2",
                               _("Inspection failed"),
                               errorMessage));
    clearResultRows();
}

void QualityInspectionPage::clearResultRows() {
    auto* child = resultsBox_.get_first_child();
    while (child != nullptr) {
        auto* next = child->get_next_sibling();
        resultsBox_.remove(*child);
        child = next;
    }
}

}  // namespace app::view
