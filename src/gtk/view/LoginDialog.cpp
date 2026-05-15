#include "src/gtk/view/LoginDialog.h"

#include "src/core/i18n.h"

namespace app::view {

namespace {

// Layout constants -- named so the clang-tidy magic-numbers lint stays
// clean and tweaks land in one place.
constexpr int kDialogWidthPx     = 360;
constexpr int kRootMarginPx      = 24;
constexpr int kRootSpacingPx     = 12;
constexpr int kButtonRowSpacing  = 12;
constexpr int kEntryWidthChars   = 24;

}  // namespace

LoginDialog::LoginDialog(app::auth::AuthService& service,
                         app::auth::Session& /*session*/)
    : service_(service) {
    buildUi();
}

void LoginDialog::buildUi() {
    set_title(_("Sign in"));
    set_resizable(false);
    set_modal(true);
    set_default_size(kDialogWidthPx, -1);
    set_deletable(true);

    // ESC / window-close maps to cancel so the operator can opt out
    // without juggling buttons.
    signal_close_request().connect(
        [this]() {
            onCancelClicked();
            return true;  // signal handled; we tore down ourselves
        },
        false);

    auto* root = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kRootSpacingPx);
    root->set_margin(kRootMarginPx);

    auto* heading = Gtk::make_managed<Gtk::Label>();
    heading->set_markup(std::string("<b>") + _("Industrial HMI sign in")
                        + "</b>");
    heading->set_xalign(0.0F);
    root->append(*heading);

    auto* subtitle = Gtk::make_managed<Gtk::Label>(
        _("Operator / Maintenance / Admin"));
    subtitle->set_xalign(0.0F);
    subtitle->add_css_class("dim-label");
    root->append(*subtitle);

    auto* usernameLabel = Gtk::make_managed<Gtk::Label>(_("Username"));
    usernameLabel->set_xalign(0.0F);
    root->append(*usernameLabel);

    usernameEntry_ = Gtk::make_managed<Gtk::Entry>();
    usernameEntry_->set_width_chars(kEntryWidthChars);
    usernameEntry_->set_placeholder_text(_("e.g. operator"));
    root->append(*usernameEntry_);

    auto* passwordLabel = Gtk::make_managed<Gtk::Label>(_("Password"));
    passwordLabel->set_xalign(0.0F);
    root->append(*passwordLabel);

    passwordEntry_ = Gtk::make_managed<Gtk::Entry>();
    passwordEntry_->set_width_chars(kEntryWidthChars);
    passwordEntry_->set_visibility(false);    // mask characters
    passwordEntry_->set_invisible_char('*');
    // Enter on the password field is the natural "submit" gesture --
    // operators on a touchscreen rarely tab to the button.
    passwordEntry_->signal_activate().connect(
        sigc::mem_fun(*this, &LoginDialog::onEnterPressedInPassword));
    root->append(*passwordEntry_);

    errorLabel_ = Gtk::make_managed<Gtk::Label>();
    errorLabel_->set_xalign(0.0F);
    errorLabel_->add_css_class("error");
    errorLabel_->set_visible(false);
    root->append(*errorLabel_);

    auto* buttonRow = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpacing);
    buttonRow->set_halign(Gtk::Align::END);

    cancelButton_ = Gtk::make_managed<Gtk::Button>(_("Cancel"));
    cancelButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &LoginDialog::onCancelClicked));
    buttonRow->append(*cancelButton_);

    loginButton_ = Gtk::make_managed<Gtk::Button>(_("Sign in"));
    loginButton_->add_css_class("suggested-action");
    loginButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &LoginDialog::onLoginClicked));
    buttonRow->append(*loginButton_);

    root->append(*buttonRow);
    set_child(*root);
}

LoginDialog::Result LoginDialog::runModal() {
    innerLoop_ = Glib::MainLoop::create();
    present();
    usernameEntry_->grab_focus();
    innerLoop_->run();
    return result_;
}

void LoginDialog::onLoginClicked() {
    const auto username = usernameEntry_->get_text();
    const auto password = passwordEntry_->get_text();

    if (username.empty() || password.empty()) {
        showError(_("Username and password are required."));
        return;
    }

    using app::auth::LoginResult;
    const auto outcome = service_.login(username.raw(), password.raw());
    switch (outcome) {
        case LoginResult::Success:
            result_ = Result::Success;
            if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
            set_visible(false);
            return;
        case LoginResult::InvalidCredentials:
            // Same wording for wrong-user and wrong-password -- the
            // service layer already collapses them for user-
            // enumeration mitigation; the UI must not leak the
            // distinction either.
            showError(_("Invalid username or password."));
            break;
        case LoginResult::AccountDisabled:
            showError(_("Account is disabled. Contact your administrator."));
            break;
        case LoginResult::LockedOut:
            showError(_("Too many failed attempts. Try again later."));
            break;
        case LoginResult::HasherFailure:
            showError(_("Internal error during sign-in. See logs."));
            break;
    }
    // On any failure: clear the password but keep the username so the
    // operator can fix a typo without re-entering both.
    passwordEntry_->set_text("");
    passwordEntry_->grab_focus();
}

void LoginDialog::onCancelClicked() {
    result_ = Result::Cancelled;
    if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
    set_visible(false);
}

void LoginDialog::onEnterPressedInPassword() {
    onLoginClicked();
}

void LoginDialog::showError(const std::string& message) {
    errorLabel_->set_text(message);
    errorLabel_->set_visible(true);
}

}  // namespace app::view
