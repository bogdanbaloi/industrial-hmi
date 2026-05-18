#include "src/gtk/view/dialogs/ResetPasswordDialog.h"

#include "src/core/i18n.h"

#include <format>

namespace app::view {

namespace {

constexpr int kDialogWidthPx    = 360;
constexpr int kRootMarginPx     = 20;
constexpr int kRootSpacingPx    = 10;
constexpr int kButtonRowSpacing = 10;
constexpr int kEntryWidthChars  = 24;

}  // namespace

ResetPasswordDialog::ResetPasswordDialog(std::string username)
    : targetUsername_(std::move(username)) {
    buildUi();
}

void ResetPasswordDialog::buildUi() {
    set_title(_("Reset password"));
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

    // Heading carries the target username so an admin who Tab-walked
    // across a row by mistake notices before submitting.
    auto* heading = Gtk::make_managed<Gtk::Label>();
    heading->set_markup(std::format("<b>{}</b>",
                                    std::string{_("Reset password for ")}
                                        + targetUsername_));
    heading->set_xalign(0.0F);
    root->append(*heading);

    auto* subtitle = Gtk::make_managed<Gtk::Label>(
        _("The user will use this password on next sign-in."));
    subtitle->set_xalign(0.0F);
    subtitle->add_css_class("dim-label");
    subtitle->set_wrap(true);
    root->append(*subtitle);

    auto* newLabel = Gtk::make_managed<Gtk::Label>(_("New password"));
    newLabel->set_xalign(0.0F);
    root->append(*newLabel);

    newEntry_ = Gtk::make_managed<Gtk::Entry>();
    newEntry_->set_width_chars(kEntryWidthChars);
    newEntry_->set_visibility(false);
    newEntry_->set_invisible_char('*');
    newEntry_->set_placeholder_text(_("minimum 8 characters"));
    root->append(*newEntry_);

    auto* confirmLabel = Gtk::make_managed<Gtk::Label>(_("Confirm new password"));
    confirmLabel->set_xalign(0.0F);
    root->append(*confirmLabel);

    confirmEntry_ = Gtk::make_managed<Gtk::Entry>();
    confirmEntry_->set_width_chars(kEntryWidthChars);
    confirmEntry_->set_visibility(false);
    confirmEntry_->set_invisible_char('*');
    root->append(*confirmEntry_);

    errorLabel_ = Gtk::make_managed<Gtk::Label>();
    errorLabel_->set_xalign(0.0F);
    errorLabel_->add_css_class("error");
    errorLabel_->set_visible(false);
    errorLabel_->set_wrap(true);
    root->append(*errorLabel_);

    auto* buttonRow = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpacing);
    buttonRow->set_halign(Gtk::Align::END);

    auto* cancel = Gtk::make_managed<Gtk::Button>(_("Cancel"));
    cancel->signal_clicked().connect(
        sigc::mem_fun(*this, &ResetPasswordDialog::onCancel));
    buttonRow->append(*cancel);

    auto* submit = Gtk::make_managed<Gtk::Button>(_("Reset"));
    submit->add_css_class("destructive-action");
    submit->signal_clicked().connect(
        sigc::mem_fun(*this, &ResetPasswordDialog::onSubmit));
    buttonRow->append(*submit);

    root->append(*buttonRow);
    set_child(*root);
}

ResetPasswordDialog::Result ResetPasswordDialog::runModal() {
    innerLoop_ = Glib::MainLoop::create();
    present();
    newEntry_->grab_focus();
    innerLoop_->run();
    return result_;
}

void ResetPasswordDialog::onSubmit() {
    const auto pw      = newEntry_->get_text().raw();
    const auto confirm = confirmEntry_->get_text().raw();
    if (pw.empty()) {
        showError(_("New password is required."));
        return;
    }
    if (pw != confirm) {
        showError(_("Passwords do not match."));
        return;
    }
    newPassword_ = pw;
    result_      = Result::Submitted;
    if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
    set_visible(false);
}

void ResetPasswordDialog::onCancel() {
    result_ = Result::Cancelled;
    if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
    set_visible(false);
}

void ResetPasswordDialog::showError(const std::string& message) {
    errorLabel_->set_text(message);
    errorLabel_->set_visible(true);
}

}  // namespace app::view
