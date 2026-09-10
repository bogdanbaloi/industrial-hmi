#pragma once

#include <QDialog>
#include <QString>

#include <memory>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtChangePasswordDialog;
}

namespace app::view {

/// Self-service "change my password" dialog for any signed-in user. Collects the
/// current password plus a new one (twice) and only accepts when the new pair
/// matches and both fields are filled; the composition root then calls
/// UsersPresenter::changeOwnPassword, which verifies the old password, hashes the
/// new one and audits the change. Distinct from the admin reset dialog so the
/// audit trail records CHANGE_PASSWORD rather than an admin reset.
class QtChangePasswordDialog : public QDialog {
public:
    explicit QtChangePasswordDialog(QWidget* parent = nullptr);
    ~QtChangePasswordDialog() override;

    QtChangePasswordDialog(const QtChangePasswordDialog&)            = delete;
    QtChangePasswordDialog& operator=(const QtChangePasswordDialog&) = delete;
    QtChangePasswordDialog(QtChangePasswordDialog&&)                 = delete;
    QtChangePasswordDialog& operator=(QtChangePasswordDialog&&)      = delete;

    [[nodiscard]] QString currentPassword() const;
    [[nodiscard]] QString newPassword() const;

    void showError(const QString& message);

private:
    /// Validate the new pair matches + all fields are non-empty, then accept().
    void validateAndAccept();

    std::unique_ptr<Ui::QtChangePasswordDialog> ui_;
};

}  // namespace app::view
