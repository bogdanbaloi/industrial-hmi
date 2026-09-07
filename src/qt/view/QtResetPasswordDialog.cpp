#include "src/qt/view/QtResetPasswordDialog.h"

#include "ui_QtResetPasswordDialog.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace app::view {

QtResetPasswordDialog::QtResetPasswordDialog(QWidget* parent)
    : QDialog(parent),
      ui_(std::make_unique<Ui::QtResetPasswordDialog>()) {
    ui_->setupUi(this);

    connect(ui_->resetButton, &QPushButton::clicked, this,
            [this] { validateAndAccept(); });
    connect(ui_->cancelButton, &QPushButton::clicked, this,
            [this] { reject(); });
}

QtResetPasswordDialog::~QtResetPasswordDialog() = default;

QString QtResetPasswordDialog::newPassword() const {
    return ui_->passwordEdit->text();
}

void QtResetPasswordDialog::validateAndAccept() {
    const QString password = ui_->passwordEdit->text();
    const QString confirm  = ui_->confirmEdit->text();
    if (password.isEmpty()) {
        ui_->errorLabel->setText(tr("Password cannot be empty."));
        ui_->errorLabel->setVisible(true);
        return;
    }
    if (password != confirm) {
        ui_->errorLabel->setText(tr("Passwords do not match."));
        ui_->errorLabel->setVisible(true);
        return;
    }
    accept();
}

}  // namespace app::view
