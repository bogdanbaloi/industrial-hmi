#include "src/gtk/view/pages/UsersPage.h"

#include "src/core/TimeFormat.h"
#include "src/core/i18n.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/dialogs/ResetPasswordDialog.h"
#include "src/gtk/view/dialogs/UserEditDialog.h"
#include "src/gtk/view/widgets/Toast.h"

#include <format>
#include <string>

namespace app::view {

namespace {

constexpr int kPageSpacingPx    = 12;
constexpr int kToolbarSpacingPx = 12;
constexpr int kPageMarginPx     = 16;
constexpr int kRowSpacingPx     = 8;
constexpr int kRowPaddingPx     = 4;

// Column widths in characters -- sized so a typical user row fits
// without horizontal scrolling at the default window width.
constexpr int kColUsername    = 14;
constexpr int kColDisplayName = 22;
constexpr int kColRole        = 12;
constexpr int kColEnabled     = 10;
constexpr int kColCreated     = 20;

// One column reserved per per-row action button.
constexpr int kColumnCount    = 8;  // username, display, role, enabled,
                                    // created, edit, reset-pw, delete

/// Plain text cell with a fixed character width + ellipsis. Same shape
/// as AuditLogPage::makeCell -- factoring it into a shared widgets/
/// header is a follow-up.
Gtk::Label* makeCell(const std::string& text, int widthChars) {
    auto* l = Gtk::make_managed<Gtk::Label>(text);
    l->set_xalign(0.0F);
    l->set_width_chars(widthChars);
    l->set_max_width_chars(widthChars);
    l->set_ellipsize(Pango::EllipsizeMode::END);
    return l;
}

std::string formatRole(app::auth::Role r) {
    return std::string{app::auth::roleName(r)};
}

}  // namespace

UsersPage::UsersPage(DialogManager& dialogManager,
                     app::presenter::UsersPresenter& presenter)
    : Page(dialogManager), presenter_(presenter) {
    buildUi();
    refresh();
}

Glib::ustring UsersPage::pageTitle() const {
    return _("Users");
}

void UsersPage::buildUi() {
    set_orientation(Gtk::Orientation::VERTICAL);
    set_spacing(kPageSpacingPx);
    set_margin(kPageMarginPx);

    // --- Toolbar -------------------------------------------------------
    auto* toolbar = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kToolbarSpacingPx);

    // Toast above the toolbar so success/error feedback is the first
    // thing the operator's eye lands on after submitting a dialog.
    toast_ = Gtk::make_managed<Toast>();
    append(*toast_);

    addButton_ = Gtk::make_managed<Gtk::Button>(_("Add user"));
    addButton_->add_css_class("suggested-action");
    addButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &UsersPage::onAddClicked));
    toolbar->append(*addButton_);

    refreshButton_ = Gtk::make_managed<Gtk::Button>(_("Refresh"));
    refreshButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &UsersPage::refresh));
    toolbar->append(*refreshButton_);

    append(*toolbar);

    // --- Grid ----------------------------------------------------------
    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_vexpand(true);
    grid_ = Gtk::make_managed<Gtk::Grid>();
    grid_->set_column_spacing(kRowSpacingPx);
    grid_->set_row_spacing(kRowPaddingPx);
    scroller_->set_child(*grid_);
    append(*scroller_);

    auto attachHeader = [&](const std::string& text, int col,
                            int widthChars) {
        auto* l = makeCell(text, widthChars);
        l->add_css_class("heading");
        grid_->attach(*l, col, 0, 1, 1);
    };
    attachHeader(_("Username"),     0, kColUsername);
    attachHeader(_("Display name"), 1, kColDisplayName);
    attachHeader(_("Role"),         2, kColRole);
    attachHeader(_("Enabled"),      3, kColEnabled);
    attachHeader(_("Created"),      4, kColCreated);
    // Columns 5-7 are action buttons -- no header text.

    footerLabel_ = Gtk::make_managed<Gtk::Label>("");
    footerLabel_->set_xalign(0.0F);
    append(*footerLabel_);
}

void UsersPage::refresh() {
    // Wipe data rows + re-fetch. Same pattern as AuditLogPage::refresh.
    for (int r = nextRow_ - 1; r >= 1; --r) {
        for (int c = 0; c < kColumnCount; ++c) {
            if (auto* child = grid_->get_child_at(c, r)) {
                grid_->remove(*child);
            }
        }
    }
    nextRow_ = 1;

    const auto rows = presenter_.list();
    for (const auto& u : rows) {
        appendRow(u);
    }
    footerLabel_->set_text(std::format("{} user(s)", rows.size()));
}

void UsersPage::appendRow(const app::auth::User& u) {
    const int r = nextRow_++;
    grid_->attach(*makeCell(u.username,                  kColUsername),    0, r, 1, 1);
    grid_->attach(*makeCell(u.displayName,               kColDisplayName), 1, r, 1, 1);
    grid_->attach(*makeCell(formatRole(u.role),          kColRole),        2, r, 1, 1);
    grid_->attach(*makeCell(u.enabled ? "yes" : "no",    kColEnabled),     3, r, 1, 1);
    grid_->attach(*makeCell(app::core::formatIso8601Local(u.createdAt),
                                                         kColCreated),     4, r, 1, 1);

    // Per-row action buttons. Captures `u.id` + `u.username` by value
    // so they survive past the row's lifetime if the grid is rebuilt
    // mid-action (refresh() removes widgets; the captured copies are
    // already on the stack of the lambda's bound parameters).
    const auto id   = u.id;
    const auto name = u.username;

    auto* editBtn = Gtk::make_managed<Gtk::Button>(_("Edit"));
    editBtn->signal_clicked().connect(
        [this, id]() { onEditClicked(id); });
    grid_->attach(*editBtn, 5, r, 1, 1);

    auto* resetBtn = Gtk::make_managed<Gtk::Button>(_("Reset password"));
    resetBtn->signal_clicked().connect(
        [this, id, name]() { onResetPasswordClicked(id, name); });
    grid_->attach(*resetBtn, 6, r, 1, 1);

    auto* deleteBtn = Gtk::make_managed<Gtk::Button>(_("Delete"));
    deleteBtn->add_css_class("destructive-action");
    deleteBtn->signal_clicked().connect(
        [this, id, name]() { onDeleteClicked(id, name); });
    grid_->attach(*deleteBtn, 7, r, 1, 1);
}

void UsersPage::onAddClicked() {
    UserEditDialog dlg(UserEditDialog::Mode::Add, std::nullopt);
    const auto outcome = dlg.runModal();
    if (outcome != UserEditDialog::Result::Submitted) return;

    const auto& form = dlg.formData();
    const auto status = presenter_.create(form.username, form.password,
                                          form.role, form.displayName);
    reportStatus(status,
                 std::format("{} \xe2\x80\x9C{}\xe2\x80\x9D",
                             std::string{_("User created:")},
                             form.username));
    if (status == app::presenter::UsersStatus::Ok) refresh();
}

void UsersPage::onEditClicked(std::int64_t id) {
    // Fetch the current row from the presenter so the dialog
    // prefill reflects whatever an admin in another session might
    // have just changed.
    const auto rows = presenter_.list();
    std::optional<app::auth::User> target;
    for (const auto& u : rows) {
        if (u.id == id) { target = u; break; }
    }
    if (!target.has_value()) {
        reportStatus(app::presenter::UsersStatus::NotFound, {});
        refresh();   // sync stale grid
        return;
    }

    UserEditDialog dlg(UserEditDialog::Mode::Edit, target);
    const auto outcome = dlg.runModal();
    if (outcome != UserEditDialog::Result::Submitted) return;

    const auto& form = dlg.formData();
    const auto status = presenter_.update(id, form.role,
                                          form.displayName, form.enabled);
    reportStatus(status,
                 std::format("{} \xe2\x80\x9C{}\xe2\x80\x9D",
                             std::string{_("User updated:")},
                             target->username));
    if (status == app::presenter::UsersStatus::Ok) refresh();
}

void UsersPage::onResetPasswordClicked(std::int64_t id,
                                       const std::string& username) {
    ResetPasswordDialog dlg(username);
    const auto outcome = dlg.runModal();
    if (outcome != ResetPasswordDialog::Result::Submitted) return;

    const auto status = presenter_.resetPassword(id, dlg.newPassword());
    reportStatus(status,
                 std::format("{} \xe2\x80\x9C{}\xe2\x80\x9D",
                             std::string{_("Password reset for")},
                             username));
}

void UsersPage::onDeleteClicked(std::int64_t id,
                                const std::string& username) {
    const bool confirmed = dialogManager_.showConfirm(
        std::string{_("Delete user")},
        std::format("{} {} ({})", _("Permanently delete"),
                    username, _("This cannot be undone.")));
    if (!confirmed) return;

    const auto status = presenter_.remove(id);
    reportStatus(status,
                 std::format("{} \xe2\x80\x9C{}\xe2\x80\x9D",
                             std::string{_("User deleted:")},
                             username));
    if (status == app::presenter::UsersStatus::Ok) refresh();
}

void UsersPage::reportStatus(app::presenter::UsersStatus s,
                             const Glib::ustring& successText) {
    using app::presenter::UsersStatus;
    if (toast_ == nullptr) return;
    if (s == UsersStatus::Ok) {
        // Auto-dismissing success toast -- positive feedback without
        // blocking the operator's next click.
        toast_->showSuccess(successText);
        return;
    }
    // Error toast stays visible until dismissed so the operator
    // catches WHY the action was rejected even when looking away
    // (e.g. clipboard transcription).
    toast_->showError(std::string{app::presenter::statusMessage(s)});
}

}  // namespace app::view
