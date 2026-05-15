#include "src/gtk/view/widgets/UserBadge.h"

#include "src/core/i18n.h"

namespace app::view {

namespace {
constexpr int kBadgeMarginPx = 8;
constexpr int kBadgeSpacingPx = 4;
}

UserBadge::UserBadge(app::auth::AuthService& service,
                     app::auth::Session& session)
    : Gtk::Box(Gtk::Orientation::VERTICAL, kBadgeSpacingPx),
      service_(service), session_(session) {
    buildUi();
    refresh();
}

void UserBadge::buildUi() {
    set_margin(kBadgeMarginPx);
    add_css_class("user-badge");

    usernameLabel_ = Gtk::make_managed<Gtk::Label>();
    usernameLabel_->set_xalign(0.0F);
    usernameLabel_->add_css_class("heading");
    append(*usernameLabel_);

    roleLabel_ = Gtk::make_managed<Gtk::Label>();
    roleLabel_->set_xalign(0.0F);
    roleLabel_->add_css_class("dim-label");
    append(*roleLabel_);

    signOutButton_ = Gtk::make_managed<Gtk::Button>(_("Sign out"));
    signOutButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &UserBadge::onSignOutClicked));
    append(*signOutButton_);
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
        set_visible(false);
        return;
    }
    set_visible(true);
    usernameLabel_->set_text(userOpt->username);
    roleLabel_->set_text(std::string(app::auth::roleName(userOpt->role)));
    signOutButton_->set_sensitive(true);
}

void UserBadge::onSignOutClicked() {
    service_.logout();
    refresh();
    if (signOutAction_) signOutAction_();
}

}  // namespace app::view
