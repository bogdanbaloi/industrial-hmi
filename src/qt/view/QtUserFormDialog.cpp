#include "src/qt/view/QtUserFormDialog.h"

#include "src/auth/User.h"
#include "src/qt/view/QtRoleLabel.h"

#include "ui_QtUserFormDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVariant>

#include <array>
#include <utility>

namespace app::view {

namespace {

// The roles offered in the combo. Their labels come from the shared
// roleLabel() so the dialog and the Users table never disagree.
constexpr std::array<auth::Role, 3> kRoles{
    auth::Role::Operator, auth::Role::Maintenance, auth::Role::Admin};

}  // namespace

QtUserFormDialog::QtUserFormDialog(std::optional<auth::User> existing,
                                   QWidget* parent)
    : QDialog(parent),
      ui_(std::make_unique<Ui::QtUserFormDialog>()),
      editMode_(existing.has_value()) {
    ui_->setupUi(this);

    for (const auto role : kRoles) {
        ui_->roleCombo->addItem(roleLabel(role), static_cast<int>(role));
    }

    if (editMode_) {
        setWindowTitle(tr("Edit user"));
        const auto& user = *existing;
        ui_->usernameEdit->setText(QString::fromStdString(user.username));
        ui_->usernameEdit->setReadOnly(true);
        ui_->displayNameEdit->setText(QString::fromStdString(user.displayName));
        const int roleIndex = ui_->roleCombo->findData(static_cast<int>(user.role));
        ui_->roleCombo->setCurrentIndex(roleIndex >= 0 ? roleIndex : 0);
        ui_->enabledCheck->setChecked(user.enabled);
        // Password is changed through the dedicated reset dialog, not here.
        ui_->passwordLabel->setVisible(false);
        ui_->passwordEdit->setVisible(false);
    } else {
        setWindowTitle(tr("Add user"));
    }

    connect(ui_->saveButton, &QPushButton::clicked, this, [this] { accept(); });
    connect(ui_->cancelButton, &QPushButton::clicked, this,
            [this] { reject(); });
}

QtUserFormDialog::~QtUserFormDialog() = default;

QString QtUserFormDialog::username() const {
    return ui_->usernameEdit->text().trimmed();
}

QString QtUserFormDialog::displayName() const {
    return ui_->displayNameEdit->text().trimmed();
}

auth::Role QtUserFormDialog::role() const {
    return static_cast<auth::Role>(ui_->roleCombo->currentData().toInt());
}

bool QtUserFormDialog::enabled() const {
    return ui_->enabledCheck->isChecked();
}

QString QtUserFormDialog::password() const {
    return editMode_ ? QString() : ui_->passwordEdit->text();
}

void QtUserFormDialog::showError(const QString& message) {
    ui_->errorLabel->setText(message);
    ui_->errorLabel->setVisible(true);
}

}  // namespace app::view
