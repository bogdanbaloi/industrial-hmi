#pragma once

// User.h (pulls Role.h) must be a complete type here: the ctor takes
// std::optional<auth::User>, and std::optional needs a complete T.
#include "src/auth/User.h"

#include <QDialog>
#include <QString>

#include <memory>
#include <optional>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtUserFormDialog;
}

namespace app::view {

/// Add / edit user dialog. One .ui reused for both intents (the GTK UsersPage
/// uses one modal for the same reason): in Add mode the username is editable and
/// the password field shows; in Edit mode the username is read-only and the
/// password field is hidden (a password change is the separate reset dialog, so
/// the audit trail records a distinct action). The dialog collects input only --
/// the page calls UsersPresenter::create / update with the getters below, so all
/// policy + RBAC + audit stay in the presenter.
class QtUserFormDialog : public QDialog {
public:
    /// Construct in Add mode (no existing user) or Edit mode (existing user
    /// pre-fills the fields).
    explicit QtUserFormDialog(std::optional<auth::User> existing = std::nullopt,
                              QWidget* parent = nullptr);
    ~QtUserFormDialog() override;

    QtUserFormDialog(const QtUserFormDialog&)            = delete;
    QtUserFormDialog& operator=(const QtUserFormDialog&) = delete;
    QtUserFormDialog(QtUserFormDialog&&)                 = delete;
    QtUserFormDialog& operator=(QtUserFormDialog&&)      = delete;

    [[nodiscard]] QString    username() const;
    [[nodiscard]] QString    displayName() const;
    [[nodiscard]] auth::Role role() const;
    [[nodiscard]] bool       enabled() const;
    /// Only meaningful in Add mode (Edit hides the field and returns empty).
    [[nodiscard]] QString    password() const;

    void showError(const QString& message);

private:
    std::unique_ptr<Ui::QtUserFormDialog> ui_;
    bool                                  editMode_{false};
};

}  // namespace app::view
