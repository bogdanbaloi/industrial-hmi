#include "src/gtk/view/dialogs/ProfileDialog.h"

#include "src/gtk/view/dialogs/AvatarMime.h"
#include "src/core/i18n.h"
#include "src/gtk/view/widgets/AvatarWidget.h"

#include <giomm/file.h>
#include <giomm/fileinputstream.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace app::view {

namespace {

constexpr int kDialogWidthPx     = 460;
constexpr int kRootMarginPx      = 20;
constexpr int kSectionSpacingPx  = 16;
constexpr int kRowSpacingPx      = 8;
constexpr int kButtonRowSpacing  = 10;
constexpr int kEntryWidthChars   = 24;
constexpr int kAvatarPreviewPx   = 64;
constexpr std::size_t kMaxUploadBytes = 256ULL * 1024ULL;  // mirrors repo

/// Load a small file into memory. Returns empty on failure -- the
/// caller surfaces an error to the user. Capped at the same ceiling
/// the repository enforces so an oversized pick fails before we hit
/// SQLite.
std::vector<std::uint8_t> readSmallFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size = static_cast<std::size_t>(in.tellg());
    if (size == 0 || size > kMaxUploadBytes) return {};
    in.seekg(0);
    std::vector<std::uint8_t> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in) return {};
    return bytes;
}

// mimeFromPath lives in AvatarMime.h (pure, unit-tested without a file
// picker). Pulled in under its unqualified name so call sites below are
// unchanged.
using app::view::avatarmime::mimeFromPath;

}  // namespace

ProfileDialog::ProfileDialog(app::presenter::UsersPresenter& presenter)
    : presenter_(presenter) {
    buildUi();
    loadCurrentUser();
}

void ProfileDialog::buildUi() {
    set_title(_("My profile"));
    set_resizable(false);
    set_modal(true);
    set_default_size(kDialogWidthPx, -1);
    set_deletable(true);

    signal_close_request().connect(
        [this]() { onDoneClicked(); return true; }, false);

    auto* root = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::VERTICAL, kSectionSpacingPx);
    root->set_margin(kRootMarginPx);

    // --- Identity row (avatar preview + name) --------------------------
    auto* identityRow = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kRowSpacingPx * 2);
    avatarPreview_ = Gtk::make_managed<AvatarWidget>(kAvatarPreviewPx);
    identityRow->append(*avatarPreview_);

    identityLabel_ = Gtk::make_managed<Gtk::Label>();
    identityLabel_->set_xalign(0.0F);
    identityLabel_->set_yalign(0.5F);
    identityLabel_->set_wrap(true);
    identityRow->append(*identityLabel_);
    root->append(*identityRow);

    // --- Avatar action buttons -----------------------------------------
    auto* avatarRow = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpacing);
    auto* uploadBtn = Gtk::make_managed<Gtk::Button>(_("Upload avatar..."));
    uploadBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProfileDialog::onUploadClicked));
    avatarRow->append(*uploadBtn);

    auto* removeBtn = Gtk::make_managed<Gtk::Button>(_("Remove avatar"));
    removeBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProfileDialog::onRemoveAvatarClicked));
    avatarRow->append(*removeBtn);
    root->append(*avatarRow);

    root->append(*Gtk::make_managed<Gtk::Separator>());

    // --- Change password section ---------------------------------------
    auto* pwHeading = Gtk::make_managed<Gtk::Label>();
    pwHeading->set_markup(std::string{"<b>"} + _("Change password") + "</b>");
    pwHeading->set_xalign(0.0F);
    root->append(*pwHeading);

    auto* oldLabel = Gtk::make_managed<Gtk::Label>(_("Current password"));
    oldLabel->set_xalign(0.0F);
    root->append(*oldLabel);
    oldPasswordEntry_ = Gtk::make_managed<Gtk::Entry>();
    oldPasswordEntry_->set_width_chars(kEntryWidthChars);
    oldPasswordEntry_->set_visibility(false);
    oldPasswordEntry_->set_invisible_char('*');
    root->append(*oldPasswordEntry_);

    auto* newLabel = Gtk::make_managed<Gtk::Label>(_("New password"));
    newLabel->set_xalign(0.0F);
    root->append(*newLabel);
    newPasswordEntry_ = Gtk::make_managed<Gtk::Entry>();
    newPasswordEntry_->set_width_chars(kEntryWidthChars);
    newPasswordEntry_->set_visibility(false);
    newPasswordEntry_->set_invisible_char('*');
    newPasswordEntry_->set_placeholder_text(_("minimum 8 characters"));
    root->append(*newPasswordEntry_);

    auto* confirmLabel = Gtk::make_managed<Gtk::Label>(_("Confirm new password"));
    confirmLabel->set_xalign(0.0F);
    root->append(*confirmLabel);
    confirmPasswordEntry_ = Gtk::make_managed<Gtk::Entry>();
    confirmPasswordEntry_->set_width_chars(kEntryWidthChars);
    confirmPasswordEntry_->set_visibility(false);
    confirmPasswordEntry_->set_invisible_char('*');
    root->append(*confirmPasswordEntry_);

    auto* changePwBtn = Gtk::make_managed<Gtk::Button>(_("Change password"));
    changePwBtn->set_halign(Gtk::Align::START);
    changePwBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProfileDialog::onChangePasswordClicked));
    root->append(*changePwBtn);

    // --- Status line + Done button -------------------------------------
    statusLabel_ = Gtk::make_managed<Gtk::Label>();
    statusLabel_->set_xalign(0.0F);
    statusLabel_->set_wrap(true);
    statusLabel_->set_visible(false);
    root->append(*statusLabel_);

    auto* buttonRow = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kButtonRowSpacing);
    buttonRow->set_halign(Gtk::Align::END);
    auto* doneBtn = Gtk::make_managed<Gtk::Button>(_("Done"));
    doneBtn->add_css_class("suggested-action");
    doneBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProfileDialog::onDoneClicked));
    buttonRow->append(*doneBtn);
    root->append(*buttonRow);

    set_child(*root);
}

void ProfileDialog::loadCurrentUser() {
    const auto me = presenter_.currentUser();
    if (!me.has_value()) {
        identityLabel_->set_text(_("Not signed in"));
        avatarPreview_->clear();
        return;
    }
    const std::string label =
        me->displayName.empty()
            ? me->username
            : me->displayName + " (" + me->username + ")";
    identityLabel_->set_text(label + "\n"
                             + std::string{app::auth::roleName(me->role)});
    avatarPreview_->setUser(*me, presenter_.getAvatar(me->id));
}

void ProfileDialog::onUploadClicked() {
    // Gtk::FileDialog (GTK 4.10+) is the modern picker -- async, no
    // file-chooser dialog lifecycle to juggle.
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(_("Choose avatar image"));

    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("Images (PNG, JPEG)"));
    filter->add_mime_type("image/png");
    filter->add_mime_type("image/jpeg");
    auto filterList = Gio::ListStore<Gtk::FileFilter>::create();
    filterList->append(filter);
    dialog->set_filters(filterList);
    dialog->set_default_filter(filter);

    dialog->open(
        *this,
        [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto file = dialog->open_finish(result);
                if (!file) return;
                const auto path = file->get_path();
                const auto bytes = readSmallFile(path);
                if (bytes.empty()) {
                    showStatus(_("File is empty or larger than 256 KB."),
                               false);
                    return;
                }
                const auto mime = mimeFromPath(path);
                if (mime.empty()) {
                    showStatus(_("Unsupported file type."), false);
                    return;
                }
                const auto status = presenter_.setOwnAvatar(bytes, mime);
                if (status == app::presenter::UsersStatus::Ok) {
                    showStatus(_("Avatar updated."), true);
                    loadCurrentUser();      // refresh preview
                } else {
                    showStatus(std::string{
                                   app::presenter::statusMessage(status)},
                               false);
                }
            } catch (const Glib::Error& e) {
                // Gtk::FileDialog also reports "user dismissed the
                // picker" through Glib::Error. We can't cleanly
                // distinguish dismissal from a real I/O failure
                // across gtkmm versions, so surface every code as a
                // status line and let the operator dismiss it if it
                // was just a cancel. (clang-tidy bugprone-empty-catch
                // rightly dislikes a silent catch-all -- this gives
                // the caller a visible signal.)
                showStatus(e.what(), false);
            }
        });
}

void ProfileDialog::onRemoveAvatarClicked() {
    const auto status = presenter_.clearOwnAvatar();
    if (status == app::presenter::UsersStatus::Ok) {
        showStatus(_("Avatar removed."), true);
        loadCurrentUser();
    } else {
        showStatus(std::string{app::presenter::statusMessage(status)}, false);
    }
}

void ProfileDialog::onChangePasswordClicked() {
    const auto oldPw     = oldPasswordEntry_->get_text().raw();
    const auto newPw     = newPasswordEntry_->get_text().raw();
    const auto confirmPw = confirmPasswordEntry_->get_text().raw();

    if (oldPw.empty() || newPw.empty()) {
        showStatus(_("All password fields are required."), false);
        return;
    }
    if (newPw != confirmPw) {
        showStatus(_("New passwords do not match."), false);
        return;
    }

    const auto status = presenter_.changeOwnPassword(oldPw, newPw);
    if (status == app::presenter::UsersStatus::Ok) {
        showStatus(_("Password changed."), true);
        oldPasswordEntry_->set_text("");
        newPasswordEntry_->set_text("");
        confirmPasswordEntry_->set_text("");
    } else {
        showStatus(std::string{app::presenter::statusMessage(status)}, false);
    }
}

void ProfileDialog::onDoneClicked() {
    if (innerLoop_ && innerLoop_->is_running()) innerLoop_->quit();
    set_visible(false);
}

void ProfileDialog::runModal() {
    innerLoop_ = Glib::MainLoop::create();
    present();
    innerLoop_->run();
}

void ProfileDialog::showStatus(const std::string& message, bool ok) {
    statusLabel_->remove_css_class("success");
    statusLabel_->remove_css_class("error");
    statusLabel_->add_css_class(ok ? "success" : "error");
    statusLabel_->set_text(message);
    statusLabel_->set_visible(true);
}

}  // namespace app::view
