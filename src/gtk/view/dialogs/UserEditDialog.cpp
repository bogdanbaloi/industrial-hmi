#include "src/gtk/view/dialogs/UserEditDialog.h"

#include "src/gtk/view/dialogs/UserEditValidation.h"
#include "src/core/i18n.h"

#include <algorithm>
#include <string>

namespace app::view {

namespace {

// Layout constants -- named so the clang-tidy magic-numbers lint stays
// clean and tweaks land in one place.
constexpr int kDialogWidthPx     = 380;
constexpr int kRootMarginPx      = 20;
constexpr int kRootSpacingPx     = 10;
constexpr int kButtonRowSpacing  = 10;
constexpr int kEntryWidthChars   = 24;

// Pure username / role-index helpers live in UserEditValidation.h so
// they can be unit-tested without a GTK widget. Pull them into this TU
// under their original unqualified names so the call sites below are
// unchanged.
using app::view::useredit::indexFromRole;
using app::view::useredit::kRoleIndexAdmin;
using app::view::useredit::kRoleIndexMaintenance;
using app::view::useredit::kRoleIndexOperator;
using app::view::useredit::roleFromIndex;
using app::view::useredit::toLowerCanonical;

}  // namespace

UserEditDialog::UserEditDialog(Mode mode,
                               std::optional<app::auth::User> existing)
    : mode_(mode), existing_(std::move(existing)) {
    buildUi();
}

void UserEditDialog::buildUi() {
    using enum Mode;
    set_title(mode_ == Add ? _("Add User") : _("Edit User"));
    set_resizable(false);
    set_modal(true);
    set_default_size(kDialogWidthPx, -1);
    set_deletable(true);

    signal_close_request().connect(
        [this]() {
            onCancel();
            return true;
        },
        false);

    auto* root = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kRootSpacingPx);
    root->set_margin(kRootMarginPx);

    // --- Username ------------------------------------------------------
    auto* usernameLabel = Gtk::make_managed<Gtk::Label>(_("Username"));
    usernameLabel->set_xalign(0.0F);
    root->append(*usernameLabel);

    usernameEntry_ = Gtk::make_managed<Gtk::Entry>();
    usernameEntry_->set_width_chars(kEntryWidthChars);
    usernameEntry_->set_placeholder_text(_("3-32 chars, starts with a letter"));
    if (mode_ == Edit && existing_.has_value()) {
        usernameEntry_->set_text(existing_->username);
        // Rename would orphan the audit trail's denormalised
        // username column; defence-in-depth is to disable here even
        // though the presenter would reject any UPDATE on username.
        usernameEntry_->set_editable(false);
        usernameEntry_->set_sensitive(false);
    }
    root->append(*usernameEntry_);

    // --- Display Name --------------------------------------------------
    auto* displayLabel = Gtk::make_managed<Gtk::Label>(_("Display name"));
    displayLabel->set_xalign(0.0F);
    root->append(*displayLabel);

    displayNameEntry_ = Gtk::make_managed<Gtk::Entry>();
    displayNameEntry_->set_width_chars(kEntryWidthChars);
    displayNameEntry_->set_placeholder_text(_("e.g. Alice Bloomberg (optional)"));
    if (mode_ == Edit && existing_.has_value()) {
        displayNameEntry_->set_text(existing_->displayName);
    }
    root->append(*displayNameEntry_);

    // --- Role ----------------------------------------------------------
    auto* roleLabel = Gtk::make_managed<Gtk::Label>(_("Role"));
    roleLabel->set_xalign(0.0F);
    root->append(*roleLabel);

    roleCombo_ = Gtk::make_managed<Gtk::ComboBoxText>();
    roleCombo_->append(_("Operator"));
    roleCombo_->append(_("Maintenance"));
    roleCombo_->append(_("Admin"));
    if (mode_ == Edit && existing_.has_value()) {
        roleCombo_->set_active(indexFromRole(existing_->role));
    } else {
        roleCombo_->set_active(kRoleIndexOperator);
    }
    root->append(*roleCombo_);

    // --- Password (Add mode only) -------------------------------------
    if (mode_ == Add) {
        auto* passwordLabel = Gtk::make_managed<Gtk::Label>(_("Password"));
        passwordLabel->set_xalign(0.0F);
        root->append(*passwordLabel);

        passwordEntry_ = Gtk::make_managed<Gtk::Entry>();
        passwordEntry_->set_width_chars(kEntryWidthChars);
        passwordEntry_->set_visibility(false);
        passwordEntry_->set_invisible_char('*');
        passwordEntry_->set_placeholder_text(_("minimum 8 characters"));
        root->append(*passwordEntry_);

        auto* confirmLabel = Gtk::make_managed<Gtk::Label>(_("Confirm password"));
        confirmLabel->set_xalign(0.0F);
        root->append(*confirmLabel);

        passwordConfirmEntry_ = Gtk::make_managed<Gtk::Entry>();
        passwordConfirmEntry_->set_width_chars(kEntryWidthChars);
        passwordConfirmEntry_->set_visibility(false);
        passwordConfirmEntry_->set_invisible_char('*');
        root->append(*passwordConfirmEntry_);
    }

    // --- Enabled (Edit mode only) -------------------------------------
    if (mode_ == Edit) {
        enabledCheck_ = Gtk::make_managed<Gtk::CheckButton>(
            _("Account enabled (allow sign-in)"));
        const bool defaultEnabled =
            existing_.has_value() ? existing_->enabled : true;
        enabledCheck_->set_active(defaultEnabled);
        root->append(*enabledCheck_);
    }

    // --- Error label ---------------------------------------------------
    errorLabel_ = Gtk::make_managed<Gtk::Label>();
    errorLabel_->set_xalign(0.0F);
    errorLabel_->add_css_class("error");
    errorLabel_->set_visible(false);
    errorLabel_->set_wrap(true);
    root->append(*errorLabel_);

    // --- Buttons -------------------------------------------------------
    auto* buttonRow = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpacing);
    buttonRow->set_halign(Gtk::Align::END);

    auto* cancel = Gtk::make_managed<Gtk::Button>(_("Cancel"));
    cancel->signal_clicked().connect(
        sigc::mem_fun(*this, &UserEditDialog::onCancel));
    buttonRow->append(*cancel);

    auto* submit = Gtk::make_managed<Gtk::Button>(
        mode_ == Add ? _("Create") : _("Save"));
    submit->add_css_class("suggested-action");
    submit->signal_clicked().connect(
        sigc::mem_fun(*this, &UserEditDialog::onSubmit));
    buttonRow->append(*submit);

    root->append(*buttonRow);
    set_child(*root);
}

UserEditDialog::Result UserEditDialog::runModal() {
    innerLoop_ = Glib::MainLoop::create();
    present();
    usernameEntry_->grab_focus();
    innerLoop_->run();
    return result_;
}

void UserEditDialog::onSubmit() {
    // Pull form fields. Username is lower-cased here so the snapshot
    // already matches the canonical form the presenter would compute
    // (saves a round-trip diff in tests).
    form_.username    = toLowerCanonical(usernameEntry_->get_text().raw());
    form_.displayName = displayNameEntry_->get_text().raw();
    form_.role        = roleFromIndex(roleCombo_->get_active_row_number());

    if (mode_ == Mode::Edit) {
        form_.enabled = enabledCheck_ != nullptr
                            ? enabledCheck_->get_active()
                            : true;
    } else {
        form_.enabled = true;
        const auto pw      = passwordEntry_->get_text().raw();
        const auto confirm = passwordConfirmEntry_->get_text().raw();
        if (pw != confirm) {
            showError(_("Passwords do not match."));
            return;
        }
        form_.password = pw;
    }

    // Minimal client-side sanity. The presenter re-validates server-
    // side so this is just to keep the operator from hitting an
    // obvious "you typed empty" round trip.
    if (form_.username.empty()) {
        showError(_("Username is required."));
        return;
    }
    if (mode_ == Mode::Add && form_.password.empty()) {
        showError(_("Password is required."));
        return;
    }

    result_ = Result::Submitted;
    if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
    set_visible(false);
}

void UserEditDialog::onCancel() {
    result_ = Result::Cancelled;
    if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
    set_visible(false);
}

void UserEditDialog::showError(const std::string& message) {
    errorLabel_->set_text(message);
    errorLabel_->set_visible(true);
}

}  // namespace app::view
