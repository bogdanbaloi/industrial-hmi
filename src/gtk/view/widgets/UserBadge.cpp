#include "src/gtk/view/widgets/UserBadge.h"

#include "src/core/i18n.h"
#include "src/gtk/view/dialogs/ProfileDialog.h"
#include "src/gtk/view/widgets/AvatarWidget.h"
#include "src/presenter/UsersPresenter.h"

#include <ctime>
#include <string>

namespace app::view {

namespace {
// Spacing inside the user card. Card chrome (padding + margin) is
// driven by the `sidebar-card` CSS class so the badge sits flush
// with the section headers above + below.
constexpr int kBadgeSpacingPx = 10;   // between identity row + button row
constexpr int kIdentityHSpace = 12;   // between avatar + name column
constexpr int kButtonRowSpace = 6;    // between Profile + Sign out icons
constexpr int kAvatarSizePx   = 40;   // bumped from 32 -- reads as identity
}

UserBadge::UserBadge(app::auth::AuthService&            service,
                     app::auth::Session&                session,
                     app::presenter::UsersPresenter*    users)
    : Gtk::Box(Gtk::Orientation::VERTICAL, kBadgeSpacingPx),
      service_(service), session_(session), users_(users) {
    buildUi();
    refresh();
    // Subscribe so an admin editing their own row (display name,
    // avatar) sees the change in the badge without re-logging in.
    sessionConn_ = session_.signalChanged().connect(
        sigc::mem_fun(*this, &UserBadge::refresh));
}

void UserBadge::buildUi() {
    // Card chrome from the sidebar design system. `user-badge` stays
    // for any legacy CSS that targets it; `sidebar-card` provides
    // the padding + bg + border-radius the rest of the sidebar uses.
    add_css_class("user-badge");
    add_css_class("sidebar-card");

    // --- Identity row: avatar + (username/role) -----------------------
    auto* identity = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kIdentityHSpace);

    avatar_ = Gtk::make_managed<AvatarWidget>(kAvatarSizePx);
    avatar_->set_valign(Gtk::Align::CENTER);
    identity->append(*avatar_);

    auto* textCol = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, 0);
    textCol->set_valign(Gtk::Align::CENTER);
    textCol->set_hexpand(true);

    usernameLabel_ = Gtk::make_managed<Gtk::Label>();
    usernameLabel_->set_xalign(0.0F);
    usernameLabel_->add_css_class("heading");
    usernameLabel_->set_ellipsize(Pango::EllipsizeMode::END);
    textCol->append(*usernameLabel_);

    roleLabel_ = Gtk::make_managed<Gtk::Label>();
    roleLabel_->set_xalign(0.0F);
    roleLabel_->add_css_class("dim-label");
    textCol->append(*roleLabel_);

    identity->append(*textCol);
    append(*identity);

    // --- Button row: Profile + Sign out -------------------------------
    // Plain text labels rather than icon glyphs -- on an industrial
    // sidebar at arm's length, readable text beats compact icons.
    // The row fills the card width so both buttons get equal weight
    // and clear hit targets (touchscreen-friendly).
    auto* buttons = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpace);
    buttons->set_homogeneous(true);

    if (users_ != nullptr) {
        profileButton_ = Gtk::make_managed<Gtk::Button>(_("Profile"));
        profileButton_->add_css_class("sidebar-card-button");
        profileButton_->signal_clicked().connect(
            sigc::mem_fun(*this, &UserBadge::onProfileClicked));
        buttons->append(*profileButton_);
    }

    signOutButton_ = Gtk::make_managed<Gtk::Button>(_("Sign out"));
    signOutButton_->add_css_class("sidebar-card-button");
    signOutButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &UserBadge::onSignOutClicked));
    buttons->append(*signOutButton_);

    append(*buttons);
    set_spacing(kBadgeSpacingPx);
}

void UserBadge::setHorizontal() {
    // Wipe the current vertical layout and rebuild flat. Called by
    // MainWindow when mounting the badge into the horizontal sidebar
    // of the multistation layout. We can't simply set_orientation()
    // on the outer Box -- the inner structure (identity row + button
    // row) needs to flatten too.
    set_orientation(Gtk::Orientation::HORIZONTAL);
    set_spacing(10);
    set_valign(Gtk::Align::CENTER);

    // Drop the card chrome -- in the horizontal bar the row backdrop
    // is provided by the sidebar itself; per-widget cards would
    // clash visually.
    remove_css_class("sidebar-card");
    add_css_class("user-badge-horizontal");

    // Remove existing children (the two original boxes built by
    // buildUi). GTK4: iterate via get_first_child() / remove().
    while (auto* child = get_first_child()) {
        remove(*child);
    }

    constexpr int kHorizontalAvatarPx = 28;

    avatar_ = Gtk::make_managed<AvatarWidget>(kHorizontalAvatarPx);
    avatar_->set_valign(Gtk::Align::CENTER);
    append(*avatar_);

    auto* textCol = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, 0);
    textCol->set_valign(Gtk::Align::CENTER);

    usernameLabel_ = Gtk::make_managed<Gtk::Label>();
    usernameLabel_->set_xalign(0.0F);
    usernameLabel_->add_css_class("heading");
    usernameLabel_->set_ellipsize(Pango::EllipsizeMode::END);
    textCol->append(*usernameLabel_);

    roleLabel_ = Gtk::make_managed<Gtk::Label>();
    roleLabel_->set_xalign(0.0F);
    roleLabel_->add_css_class("dim-label");
    textCol->append(*roleLabel_);

    append(*textCol);

    if (users_ != nullptr) {
        profileButton_ = Gtk::make_managed<Gtk::Button>(_("Profile"));
        profileButton_->add_css_class("sidebar-card-button");
        profileButton_->set_valign(Gtk::Align::CENTER);
        profileButton_->signal_clicked().connect(
            sigc::mem_fun(*this, &UserBadge::onProfileClicked));
        append(*profileButton_);
    }

    signOutButton_ = Gtk::make_managed<Gtk::Button>(_("Sign out"));
    signOutButton_->add_css_class("sidebar-card-button");
    signOutButton_->set_valign(Gtk::Align::CENTER);
    signOutButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &UserBadge::onSignOutClicked));
    append(*signOutButton_);

    // Re-populate from the current session.
    refresh();
}

void UserBadge::refresh() {
    const auto userOpt = session_.currentUser();
    if (!userOpt.has_value()) {
        // Should not happen by construction (auth-gated entry into
        // MainWindow guarantees a session), but defensive: hide the
        // widget if somehow torn down.
        usernameLabel_->set_text("");
        roleLabel_->set_text("");
        signOutButton_->set_sensitive(false);
        if (profileButton_ != nullptr) profileButton_->set_sensitive(false);
        avatar_->clear();
        set_visible(false);
        return;
    }
    set_visible(true);

    // Prefer the display name when set -- "Alice Bloomberg" reads
    // better in a busy sidebar than "alice".
    const auto& u = *userOpt;
    usernameLabel_->set_text(u.displayName.empty() ? u.username
                                                   : u.displayName);

    // Role line composes "<ROLE> . since HH:MM" -- the session start
    // is captured at widget construction (which fires right after a
    // successful LoginDialog). Mid-dot separator U+00B7 keeps the
    // pair visually unified instead of two competing labels.
    {
        std::string roleLine = std::string(app::auth::roleName(u.role));
        const auto tt = std::chrono::system_clock::to_time_t(sessionStart_);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M", &tm);
        roleLine += " \xC2\xB7 ";          // U+00B7 MIDDLE DOT
        roleLine += _("since ");
        roleLine += buf;
        roleLabel_->set_text(roleLine);
    }
    signOutButton_->set_sensitive(true);
    if (profileButton_ != nullptr) profileButton_->set_sensitive(true);

    if (users_ != nullptr) {
        avatar_->setUser(u, users_->getAvatar(u.id));
    } else {
        avatar_->setUser(u, std::nullopt);
    }
}

void UserBadge::onSignOutClicked() {
    service_.logout();
    refresh();
    if (signOutAction_) signOutAction_();
}

void UserBadge::onProfileClicked() {
    if (users_ == nullptr) return;
    ProfileDialog dlg(*users_);
    dlg.set_transient_for(
        *dynamic_cast<Gtk::Window*>(get_root()));
    dlg.runModal();
    // Whatever the operator did in the dialog (avatar swap, password
    // change) may have changed the badge's view -- refresh so the
    // sidebar reflects it immediately.
    refresh();
}

}  // namespace app::view
