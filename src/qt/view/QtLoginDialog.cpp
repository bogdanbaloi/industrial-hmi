#include "src/qt/view/QtLoginDialog.h"

#include "src/auth/AuthService.h"

#include "ui_QtLoginDialog.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

namespace app::view {

QtLoginDialog::QtLoginDialog(auth::AuthService& service, QWidget* parent)
    : QDialog(parent),
      service_(service),
      ui_(std::make_unique<Ui::QtLoginDialog>()) {
    ui_->setupUi(this);

    connect(ui_->signInButton, &QPushButton::clicked, this,
            [this] { attemptLogin(); });
    connect(ui_->cancelButton, &QPushButton::clicked, this,
            [this] { reject(); });
    // Enter in either field submits, matching the GTK dialog's touchscreen flow.
    connect(ui_->passwordEdit, &QLineEdit::returnPressed, this,
            [this] { attemptLogin(); });
    connect(ui_->usernameEdit, &QLineEdit::returnPressed, this,
            [this] { attemptLogin(); });

    ui_->usernameEdit->setFocus();
}

QtLoginDialog::~QtLoginDialog() = default;

void QtLoginDialog::attemptLogin() {
    const QString username = ui_->usernameEdit->text();
    const QString password = ui_->passwordEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        showError(tr("Username and password are required."));
        return;
    }

    switch (service_.login(username.toStdString(), password.toStdString())) {
        case auth::LoginResult::Success:
            accept();  // Session now holds the authenticated user.
            return;
        case auth::LoginResult::InvalidCredentials:
            // One message for wrong-user and wrong-password: the service already
            // merges them (user-enumeration mitigation), so the UI must not leak
            // the distinction either.
            showError(tr("Invalid username or password."));
            break;
        case auth::LoginResult::AccountDisabled:
            showError(tr("Account is disabled. Contact your administrator."));
            break;
        case auth::LoginResult::LockedOut:
            showError(tr("Too many failed attempts. Try again later."));
            break;
        case auth::LoginResult::HasherFailure:
            showError(tr("Internal error during sign-in. See logs."));
            break;
    }
    // Keep the username, clear the password so a typo is a one-field fix.
    ui_->passwordEdit->clear();
    ui_->passwordEdit->setFocus();
}

void QtLoginDialog::showError(const QString& message) {
    ui_->errorLabel->setText(message);
    ui_->errorLabel->setVisible(true);
}

}  // namespace app::view
