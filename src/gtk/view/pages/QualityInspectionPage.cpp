#include "src/gtk/view/pages/QualityInspectionPage.h"

#include "src/config/config_defaults.h"
#include "src/core/i18n.h"
#include "src/gtk/view/DialogManager.h"

#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/main.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
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

/// Mime types supported by the stb_image-backed decoder. Listed in
/// the file dialog filter so the picker shows only files we can
/// actually decode.
const std::array<const char*, 3> kSupportedMimeTypes = {
    "image/png",
    "image/jpeg",
    "image/bmp",
};

/// Percentage scale: confidence in [0, 1] -> "42.0%".
constexpr float kPercentScale = 100.0F;

/// Build a single result row in the right-side results panel. Layout:
///
///     [#1]  tench, Tinca tinca                    42.0%
///           [============================----------------]   (LevelBar)
///
/// Pure helper -- the page rebuilds the result rows on every Completed
/// callback, so each row is a fresh widget tree.
[[nodiscard]] std::unique_ptr<Gtk::Box>
    makeResultRow(int rank, const ml::Classification& entry) {
    auto row = std::make_unique<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    row->add_css_class("inspection-row");

    auto* heading = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, 8);

    auto* rankLabel = Gtk::make_managed<Gtk::Label>(
        std::format("#{}", rank));
    rankLabel->add_css_class("inspection-rank");
    rankLabel->set_halign(Gtk::Align::START);
    heading->append(*rankLabel);

    auto* labelText = Gtk::make_managed<Gtk::Label>(entry.label);
    labelText->add_css_class("inspection-label");
    labelText->set_halign(Gtk::Align::START);
    labelText->set_hexpand(true);
    labelText->set_ellipsize(Pango::EllipsizeMode::END);
    heading->append(*labelText);

    auto* percent = Gtk::make_managed<Gtk::Label>(
        std::format("{:.1f}%", entry.confidence * kPercentScale));
    percent->add_css_class("inspection-confidence");
    percent->set_halign(Gtk::Align::END);
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
    buildUi();
    applyStyles();
}

void QualityInspectionPage::applyStyles() {
    cssProvider_ = Gtk::CssProvider::create();
    try {
        cssProvider_->load_from_path(app::config::defaults::kInspectionCSS);
        Gtk::StyleContext::add_provider_for_display(
            Gdk::Display::get_default(),
            cssProvider_,
            GTK_STYLE_PROVIDER_PRIORITY_USER);
    }
    // Stylesheet missing is non-fatal -- the page falls back to the
    // default GTK theme. No logger is wired in at this point so we
    // cannot report; an absent CSS file shows up as a visual hint
    // anyway. NOLINT silences clang-tidy's bugprone-empty-catch.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (const Glib::Error&) {}
}

QualityInspectionPage::~QualityInspectionPage() {
    if (presenter_) {
        presenter_->removeObserver(this);
    }
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
    auto builder = Gtk::Builder::create_from_file(
        app::config::defaults::kInspectionPageUI);

    if (auto* root = builder->get_widget<Gtk::Box>("inspection_root")) {
        append(*root);
    }

    chooseButton_ = builder->get_widget<Gtk::Button>("btn_choose_image");
    statusLabel_  = builder->get_widget<Gtk::Label>("lbl_status");
    preview_      = builder->get_widget<Gtk::Picture>("preview_image");
    resultsBox_   = builder->get_widget<Gtk::Box>("results_box");

    if (chooseButton_ != nullptr) {
        chooseButton_->signal_clicked().connect(
            sigc::mem_fun(*this,
                          &QualityInspectionPage::onChooseFileClicked));
    }
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
        renderFailed(path, std::string("Presenter not initialised"));
        return;
    }

    if (chooseButton_ != nullptr) {
        chooseButton_->set_sensitive(false);
    }
    if (preview_ != nullptr) {
        preview_->set_filename(path);
    }

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
    if (statusLabel_ != nullptr) {
        statusLabel_->set_label(_("Inspecting..."));
        statusLabel_->remove_css_class("error");
        statusLabel_->remove_css_class("success");
    }
    clearResultRows();
}

void QualityInspectionPage::renderCompleted(
    presenter::InspectionResultViewModel viewModel) {
    if (chooseButton_ != nullptr) {
        chooseButton_->set_sensitive(true);
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->remove_css_class("error");
        statusLabel_->add_css_class("success");
        statusLabel_->set_label(
            Glib::ustring::compose(
                "%1: %2 ms",
                _("Inferred in"),
                viewModel.latency.count()));
    }

    clearResultRows();
    if (resultsBox_ != nullptr) {
        int rank = 1;
        for (const auto& entry : viewModel.results) {
            resultsBox_->append(*makeResultRow(rank, entry).release());
            ++rank;
        }
    }
}

void QualityInspectionPage::renderFailed(std::string /*sourcePath*/,
                                         std::string errorMessage) {
    if (chooseButton_ != nullptr) {
        chooseButton_->set_sensitive(true);
    }
    if (statusLabel_ != nullptr) {
        statusLabel_->remove_css_class("success");
        statusLabel_->add_css_class("error");
        statusLabel_->set_label(
            Glib::ustring::compose("%1: %2",
                                   _("Inspection failed"),
                                   errorMessage));
    }
    clearResultRows();
}

void QualityInspectionPage::clearResultRows() {
    if (resultsBox_ == nullptr) {
        return;
    }
    auto* child = resultsBox_->get_first_child();
    while (child != nullptr) {
        auto* next = child->get_next_sibling();
        resultsBox_->remove(*child);
        child = next;
    }
}

}  // namespace app::view
