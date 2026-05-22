#include "src/gtk/view/pages/MultiStationDashboardPage.h"

#include "src/core/i18n.h"

namespace app::view {

namespace {
// Horizontal split spacing between the two station panes. Kept tight
// so each pane gets as much canvas as possible on a 1920x1080
// terminal; the per-pane "MASTER" / "SLAVE" headers and the panes'
// own padding provide enough visual separation.
constexpr int kPaneSpacingPx   = 8;
constexpr int kHeaderSpacingPx = 4;
}  // namespace

MultiStationDashboardPage::MultiStationDashboardPage(
        DialogManager& dialogManager)
    : Page(dialogManager) {
    buildUi();
}

void MultiStationDashboardPage::buildUi() {
    add_css_class("multistation-page");

    auto* split = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kPaneSpacingPx);
    split->set_hexpand(true);
    split->set_vexpand(true);
    split->set_homogeneous(true);   // 50/50 split; revisit if visual review wants 60/40

    // Master pane: header + DashboardPage in a vertical box.
    auto* masterColumn = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kHeaderSpacingPx);
    masterColumn->set_hexpand(true);
    auto* masterHeader = Gtk::make_managed<Gtk::Label>(_("MASTER STATION"));
    masterHeader->set_xalign(0.0F);
    masterHeader->add_css_class("multistation-pane-header");
    masterColumn->append(*masterHeader);

    masterPage_ = Gtk::make_managed<DashboardPage>(dialogManager_);
    masterPage_->set_hexpand(true);
    masterPage_->set_vexpand(true);
    masterColumn->append(*masterPage_);
    split->append(*masterColumn);

    // Slave pane: same shape.
    auto* slaveColumn = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kHeaderSpacingPx);
    slaveColumn->set_hexpand(true);
    auto* slaveHeader = Gtk::make_managed<Gtk::Label>(_("SLAVE STATION"));
    slaveHeader->set_xalign(0.0F);
    slaveHeader->add_css_class("multistation-pane-header");
    slaveColumn->append(*slaveHeader);

    slavePage_ = Gtk::make_managed<DashboardPage>(dialogManager_);
    slavePage_->set_hexpand(true);
    slavePage_->set_vexpand(true);
    slaveColumn->append(*slavePage_);
    split->append(*slaveColumn);

    append(*split);
}

void MultiStationDashboardPage::initialize(
        std::shared_ptr<DashboardPresenter> masterPresenter,
        std::shared_ptr<DashboardPresenter> slavePresenter) {
    // Each child page wires up to its own presenter independently.
    // Observer registration happens inside DashboardPage::initialize,
    // identical to single-station deployment.
    if (masterPage_ != nullptr) {
        masterPage_->initialize(std::move(masterPresenter));
    }
    if (slavePage_ != nullptr) {
        slavePage_->initialize(std::move(slavePresenter));
    }
}

void MultiStationDashboardPage::applyRole(app::auth::Role role) {
    if (masterPage_ != nullptr) masterPage_->applyRole(role);
    if (slavePage_ != nullptr) slavePage_->applyRole(role);
}

Glib::ustring MultiStationDashboardPage::pageTitle() const {
    // Single notebook tab title -- this page replaces the standalone
    // Dashboard tab when multi-station mode is enabled.
    return _("Dashboard");
}

void MultiStationDashboardPage::onThemeChanged() {
    if (masterPage_ != nullptr) masterPage_->onThemeChanged();
    if (slavePage_ != nullptr) slavePage_->onThemeChanged();
}

void MultiStationDashboardPage::onLanguageChanged() {
    if (masterPage_ != nullptr) masterPage_->onLanguageChanged();
    if (slavePage_ != nullptr) slavePage_->onLanguageChanged();
}

void MultiStationDashboardPage::refreshThemedWidgets() {
    if (masterPage_ != nullptr) masterPage_->refreshThemedWidgets();
    if (slavePage_ != nullptr) slavePage_->refreshThemedWidgets();
}

}  // namespace app::view
