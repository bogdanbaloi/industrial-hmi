#include "src/gtk/view/pages/MultiStationDashboardPage.h"

#include "src/core/i18n.h"

namespace app::view {

namespace {
// Horizontal split spacing between the two station panes. Hugged
// down to 2 px so every spare pixel goes to the panes themselves --
// the per-pane "PRIMARY STATION" / "SECONDARY STATION" headers and
// the dashboard-compact card chrome are enough visual separation
// without a wide gutter.
constexpr int kPaneSpacingPx   = 0;
constexpr int kHeaderSpacingPx = 1;
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

    // Primary pane: header + DashboardPage in a vertical box.
    auto* primaryColumn = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kHeaderSpacingPx);
    primaryColumn->set_hexpand(true);
    auto* primaryHeader = Gtk::make_managed<Gtk::Label>(_("PRIMARY STATION"));
    primaryHeader->set_xalign(0.0F);
    primaryHeader->add_css_class("multistation-pane-header");
    primaryColumn->append(*primaryHeader);

    primaryPage_ = Gtk::make_managed<DashboardPage>(dialogManager_);
    primaryPage_->set_hexpand(true);
    primaryPage_->set_vexpand(true);
    // Compact CSS variant -- reduces card padding, font sizes, button
    // sizing so the full DashboardPage fits the ~860px-per-pane budget
    // a 1920x1080 terminal leaves after the sidebar takes 200px and
    // the split is 50/50. Class drives .dashboard-compact rules in
    // adwaita-theme.css.
    primaryPage_->add_css_class("dashboard-compact");
    primaryColumn->append(*primaryPage_);
    split->append(*primaryColumn);

    // Secondary pane: same shape.
    auto* secondaryColumn = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kHeaderSpacingPx);
    secondaryColumn->set_hexpand(true);
    auto* secondaryHeader = Gtk::make_managed<Gtk::Label>(_("SECONDARY STATION"));
    secondaryHeader->set_xalign(0.0F);
    secondaryHeader->add_css_class("multistation-pane-header");
    secondaryColumn->append(*secondaryHeader);

    secondaryPage_ = Gtk::make_managed<DashboardPage>(dialogManager_);
    secondaryPage_->set_hexpand(true);
    secondaryPage_->set_vexpand(true);
    secondaryPage_->add_css_class("dashboard-compact");
    secondaryColumn->append(*secondaryPage_);
    split->append(*secondaryColumn);

    append(*split);
}

void MultiStationDashboardPage::initialize(
        std::shared_ptr<DashboardPresenter> primaryPresenter,
        std::shared_ptr<DashboardPresenter> secondaryPresenter) {
    // Each child page wires up to its own presenter independently.
    // Observer registration happens inside DashboardPage::initialize,
    // identical to single-station deployment.
    if (primaryPage_ != nullptr) {
        primaryPage_->initialize(std::move(primaryPresenter));
        // Shrink the wide Cairo widgets so three quality cards fit
        // in the ~860px-per-pane budget without clipping. The
        // dashboard-compact CSS class handles paddings + fonts; this
        // call handles gauge + trend-chart explicit sizing.
        primaryPage_->setCompact(true);
    }
    if (secondaryPage_ != nullptr) {
        secondaryPage_->initialize(std::move(secondaryPresenter));
        secondaryPage_->setCompact(true);
    }
}

void MultiStationDashboardPage::applyRole(app::auth::Role role) {
    if (primaryPage_ != nullptr) primaryPage_->applyRole(role);
    if (secondaryPage_ != nullptr) secondaryPage_->applyRole(role);
}

Glib::ustring MultiStationDashboardPage::pageTitle() const {
    // Single notebook tab title -- this page replaces the standalone
    // Dashboard tab when multi-station mode is enabled.
    return _("Dashboard");
}

void MultiStationDashboardPage::onThemeChanged() {
    if (primaryPage_ != nullptr) primaryPage_->onThemeChanged();
    if (secondaryPage_ != nullptr) secondaryPage_->onThemeChanged();
}

void MultiStationDashboardPage::onLanguageChanged() {
    if (primaryPage_ != nullptr) primaryPage_->onLanguageChanged();
    if (secondaryPage_ != nullptr) secondaryPage_->onLanguageChanged();
}

void MultiStationDashboardPage::refreshThemedWidgets() {
    if (primaryPage_ != nullptr) primaryPage_->refreshThemedWidgets();
    if (secondaryPage_ != nullptr) secondaryPage_->refreshThemedWidgets();
}

}  // namespace app::view
