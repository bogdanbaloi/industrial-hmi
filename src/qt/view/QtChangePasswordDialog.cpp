#include "src/qt/view/QtChangePasswordDialog.h"

#include "ui_QtChangePasswordDialog.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace app::view {

QtChangePasswordDialog::QtChangePasswordDialog(QWidget* parent)
    : QDialog(parent),
      ui_(std::make_unique<Ui::QtChangePasswordDialog>()) {
    ui_->setupUi(this);

    connect(ui_->changeButton, &QPushButton::clicked, this,
            [this] { validateAndAccept(); });
    connect(ui_->cancelButton, &QPushButton::clicked, this,
            [this] { reject(); });
}

QtChangePasswordDialog::~QtChangePasswordDialog() = default;

QString QtChangePasswordDialog::currentPassword() const {
    return ui_->currentEdit->text();
}

QString QtChangePasswordDialog::newPassword() const {
    return ui_->newEdit->text();
}

void QtChangePasswordDialog::showError(const QString& message) {
    ui_->errorLabel->setText(message);
    ui_->errorLabel->setVisible(true);
}

void QtChangePasswordDialog::validateAndAccept() {
    const QString current = ui_->currentEdit->text();
    const QString fresh   = ui_->newEdit->text();
    const QString confirm = ui_->confirmEdit->text();
    if (current.isEmpty() || fresh.isEmpty()) {
        showError(tr("All fields are required."));
        return;
    }
    if (fresh != confirm) {
        showError(tr("New passwords do not match."));
        return;
    }
    accept();
}

}  // namespace app::view
