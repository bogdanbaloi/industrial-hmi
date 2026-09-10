#pragma once

#include <QDialog>
#include <QString>

#include <memory>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtResetPasswordDialog;
}

namespace app::view {

/// Admin-initiated password reset dialog. Collects a new password twice and
/// only accepts when both match and are non-empty; the page then calls
/// UsersPresenter::resetPassword (which hashes + audits as a distinct action).
/// Kept separate from the edit dialog so the audit trail records RESET_PASSWORD
/// rather than a generic update, mirroring the GTK UsersPage.
class QtResetPasswordDialog : public QDialog {
public:
    explicit QtResetPasswordDialog(QWidget* parent = nullptr);
    ~QtResetPasswordDialog() override;

    QtResetPasswordDialog(const QtResetPasswordDialog&)            = delete;
    QtResetPasswordDialog& operator=(const QtResetPasswordDialog&) = delete;
    QtResetPasswordDialog(QtResetPasswordDialog&&)                 = delete;
    QtResetPasswordDialog& operator=(QtResetPasswordDialog&&)      = delete;

    [[nodiscard]] QString newPassword() const;

private:
    /// Validate the two fields match + are non-empty, then accept() or show a
    /// reason.
    void validateAndAccept();

    std::unique_ptr<Ui::QtResetPasswordDialog> ui_;
};

}  // namespace app::view
