#include "src/gtk/view/widgets/UserBadge.h"

#include "src/core/i18n.h"
#include "src/gtk/view/dialogs/ProfileDialog.h"
#include "src/gtk/view/widgets/AvatarWidget.h"
#include "src/presenter/UsersPresenter.h"

namespace app::view {

namespace {
constexpr int kBadgeMarginPx  = 8;
constexpr int kBadgeSpacingPx = 6;
constexpr int kIdentityHSpace = 10;
constexpr int kButtonRowSpace = 6;
constexpr int kAvatarSizePx   = 32;
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
    set_margin(kBadgeMarginPx);
    add_css_class("user-badge");

    // --- Identity row: avatar + (username/role) -----------------------
    auto* identity = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kIdentityHSpace);

    avatar_ = Gtk::make_managed<AvatarWidget>(kAvatarSizePx);
    identity->append(*avatar_);

    auto* textCol = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, 0);
    textCol->set_valign(Gtk::Align::CENTER);

    usernameLabel_ = Gtk::make_managed<Gtk::Label>();
    usernameLabel_->set_xalign(0.0F);
    usernameLabel_->add_css_class("heading");
    textCol->append(*usernameLabel_);

    roleLabel_ = Gtk::make_managed<Gtk::Label>();
    roleLabel_->set_xalign(0.0F);
    roleLabel_->add_css_class("dim-label");
    textCol->append(*roleLabel_);

    identity->append(*textCol);
    append(*identity);

    // --- Button row: Profile + Sign out -------------------------------
    auto* buttons = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpace);

    if (users_ != nullptr) {
        profileButton_ = Gtk::make_managed<Gtk::Button>(_("Profile"));
        profileButton_->signal_clicked().connect(
            sigc::mem_fun(*this, &UserBadge::onProfileClicked));
        buttons->append(*profileButton_);
    }

    signOutButton_ = Gtk::make_managed<Gtk::Button>(_("Sign out"));
    signOutButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &UserBadge::onSignOutClicked));
    buttons->append(*signOutButton_);

    append(*buttons);
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
    roleLabel_->set_text(std::string(app::auth::roleName(u.role)));
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
