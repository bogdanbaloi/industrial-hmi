#include "src/gtk/view/LoginDialog.h"

#include "src/core/i18n.h"

#if defined(_WIN32)
// GTK4 dropped set_position / move -- placement is the WM's job.
// On Windows MSYS2 GTK4 the WM picks top-left for a small modal
// without a parent. We centre by hand via the Win32 HWND exposed
// through GDK once the surface is realised. Linux X11 path uses
// the WM's own placement (centred by every mainstream WM for a
// small modal); Wayland actively forbids client-driven placement.
#  include <windows.h>
#  include <gdk/win32/gdkwin32.h>
#endif

namespace app::view {

namespace {

// Layout constants -- named so the clang-tidy magic-numbers lint stays
// clean and tweaks land in one place.
constexpr int kDialogWidthPx     = 360;
// Explicit height (rather than -1 / "natural") helps the WM decide
// initial placement -- on Windows MSYS2 GTK4 a window with both
// dimensions known is centred on the active monitor; with -1 it
// often lands top-left.
constexpr int kDialogHeightPx    = 320;
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
    set_default_size(kDialogWidthPx, kDialogHeightPx);
    set_deletable(true);

    // Client-side decoration so the titlebar picks up the GTK CSS
    // theme instead of falling back to native Windows chrome (light
    // grey strip that clashes with our dark content). The
    // .dialog-titlebar-dark class is defined in
    // assets/styles/adwaita-theme.css and matches the rest of the
    // app's dark palette; MainWindow uses the same approach via
    // its .ui file.
    auto* header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_show_title_buttons(true);   // X close button
    header->add_css_class("dialog-titlebar-dark");
    set_titlebar(*header);
    // GTK4 removed the legacy set_position / move APIs -- initial
    // placement is the WM's job. Most desktop WMs centre a small,
    // non-resizable, modal toplevel without a parent on the active
    // monitor when both default-size dimensions are explicit; -1
    // for the height triggers a delayed "natural size" pass which
    // then lands top-left on Windows MSYS2 GTK4. Hence the explicit
    // kDialogHeightPx above.

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

#if defined(_WIN32)
namespace {

/// Centre a realised Gtk::Window on the monitor it currently sits on
/// (work-area, so the taskbar doesn't push us off-screen). Pure Win32
/// because GTK4 removed every positioning API and Windows MSYS2 GTK4
/// places dialogs at top-left otherwise.
void centreOnMonitor(Gtk::Window& window) {
    auto surface = window.get_surface();
    if (!surface) return;
    HWND hwnd = reinterpret_cast<HWND>(
        gdk_win32_surface_get_handle(surface->gobj()));
    if (hwnd == nullptr) return;

    RECT wr{};
    if (GetWindowRect(hwnd, &wr) == 0) return;
    const int width  = wr.right  - wr.left;
    const int height = wr.bottom - wr.top;

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(mon, &mi) == 0) return;

    const int x = mi.rcWork.left
                  + ((mi.rcWork.right  - mi.rcWork.left) - width)  / 2;
    const int y = mi.rcWork.top
                  + ((mi.rcWork.bottom - mi.rcWork.top)  - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace
#endif

LoginDialog::Result LoginDialog::runModal() {
    innerLoop_ = Glib::MainLoop::create();
    present();
#if defined(_WIN32)
    // The window's HWND is only valid after `present()` has realised
    // the surface. Defer the centring to an idle so it runs after
    // GTK has finished laying out + showing the window, then move
    // the native window to the monitor centre. The idle runs once
    // before the modal loop hands control to the operator.
    Glib::signal_idle().connect_once(
        [this]() { centreOnMonitor(*this); });
#endif
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
