// Presenter header (pulls the auth DTOs) before any Qt header, matching the
// include-order convention the rest of the Qt frontend uses.
#include "src/presenter/UsersPresenter.h"

#include "src/qt/view/QtUsersPage.h"

#include "src/auth/Role.h"
#include "src/qt/view/QtResetPasswordDialog.h"
#include "src/qt/view/QtRoleLabel.h"
#include "src/qt/view/QtTheme.h"
#include "src/qt/view/QtUserFormDialog.h"

#include "ui_QtUsersPage.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>

#include <libintl.h>

#include <string>

namespace app::view {

namespace {

// Table column layout. 0 is not in clang-tidy's magic-number ignore list only
// as a literal in arithmetic; as named column indices these read clearly and
// keep the setItem calls self-documenting.
constexpr int kColUsername    = 0;
constexpr int kColDisplayName = 1;
constexpr int kColRole        = 2;
constexpr int kColEnabled     = 3;
constexpr int kColCreated     = 4;
constexpr int kColumnCount    = 5;

// A presenter status maps to a localised message via the same gettext catalog
// the rest of the UI uses (statusMessage returns the msgid).
QString localizedStatus(presenter::UsersStatus status) {
    const std::string msgid{presenter::statusMessage(status)};
    return QString::fromUtf8(gettext(msgid.c_str()));
}

}  // namespace

QtUsersPage::QtUsersPage(presenter::UsersPresenter& presenter, QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::QtUsersPage>()),
      presenter_(presenter) {
    ui_->setupUi(this);

    auto* table = ui_->usersTable;
    table->setColumnCount(kColumnCount);
    applyHeaderLabels();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui_->addButton, &QPushButton::clicked, this, [this] { onAdd(); });
    connect(ui_->editButton, &QPushButton::clicked, this, [this] { onEdit(); });
    connect(ui_->resetButton, &QPushButton::clicked, this,
            [this] { onResetPassword(); });
    connect(ui_->deleteButton, &QPushButton::clicked, this,
            [this] { onDelete(); });
    connect(ui_->refreshButton, &QPushButton::clicked, this,
            [this] { refresh(); });
    connect(table, &QTableWidget::itemSelectionChanged, this,
            [this] { updateActionButtons(); });

    refresh();
}

QtUsersPage::~QtUsersPage() = default;

void QtUsersPage::applyHeaderLabels() {
    ui_->usersTable->setHorizontalHeaderLabels(
        {tr("Username"), tr("Display name"), tr("Role"), tr("Enabled"),
         tr("Created")});
}

void QtUsersPage::refresh() {
    users_ = presenter_.list();
    auto* table = ui_->usersTable;
    table->setRowCount(static_cast<int>(users_.size()));

    int row = 0;
    for (const auto& user : users_) {
        auto* nameItem =
            new QTableWidgetItem(QString::fromStdString(user.username));
        nameItem->setData(Qt::UserRole,
                          QVariant::fromValue<qlonglong>(user.id));
        table->setItem(row, kColUsername, nameItem);
        table->setItem(row, kColDisplayName,
                       new QTableWidgetItem(QString::fromStdString(
                           user.displayName.empty() ? user.username
                                                    : user.displayName)));
        table->setItem(row, kColRole, new QTableWidgetItem(roleLabel(user.role)));
        table->setItem(row, kColEnabled,
                       new QTableWidgetItem(user.enabled ? tr("Yes") : tr("No")));
        table->setItem(row, kColCreated,
                       new QTableWidgetItem(QString::fromStdString(user.createdAt)));
        ++row;
    }
    ui_->footerLabel->setText(tr("%1 users").arg(users_.size()));
    updateActionButtons();
}

void QtUsersPage::updateActionButtons() {
    const bool hasSelection = selectedUserId().has_value();
    ui_->editButton->setEnabled(hasSelection);
    ui_->resetButton->setEnabled(hasSelection);
    ui_->deleteButton->setEnabled(hasSelection);
}

std::optional<std::int64_t> QtUsersPage::selectedUserId() const {
    const int row = ui_->usersTable->currentRow();
    if (row < 0) {
        return std::nullopt;
    }
    const auto* item = ui_->usersTable->item(row, kColUsername);
    if (item == nullptr) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(item->data(Qt::UserRole).toLongLong());
}

std::optional<auth::User> QtUsersPage::findUser(std::int64_t id) const {
    for (const auto& user : users_) {
        if (user.id == id) {
            return user;
        }
    }
    return std::nullopt;
}

void QtUsersPage::onAdd() {
    QtUserFormDialog dialog(std::nullopt, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto status = presenter_.create(
        dialog.username().toStdString(), dialog.password().toStdString(),
        dialog.role(), dialog.displayName().toStdString());
    reportStatus(status, tr("User created."));
    if (status == presenter::UsersStatus::Ok) {
        refresh();
    }
}

void QtUsersPage::onEdit() {
    const auto id = selectedUserId();
    if (!id.has_value()) {
        return;
    }
    const auto user = findUser(*id);
    if (!user.has_value()) {
        return;
    }
    QtUserFormDialog dialog(user, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto status = presenter_.update(*id, dialog.role(),
                                          dialog.displayName().toStdString(),
                                          dialog.enabled());
    reportStatus(status, tr("User updated."));
    if (status == presenter::UsersStatus::Ok) {
        refresh();
    }
}

void QtUsersPage::onResetPassword() {
    const auto id = selectedUserId();
    if (!id.has_value()) {
        return;
    }
    QtResetPasswordDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto status =
        presenter_.resetPassword(*id, dialog.newPassword().toStdString());
    reportStatus(status, tr("Password reset."));
}

void QtUsersPage::onDelete() {
    const auto id = selectedUserId();
    if (!id.has_value()) {
        return;
    }
    const auto user = findUser(*id);
    if (!user.has_value()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete user"),
        tr("Delete user \"%1\"? This cannot be undone.")
            .arg(QString::fromStdString(user->username)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    const auto status = presenter_.remove(*id);
    reportStatus(status, tr("User deleted."));
    if (status == presenter::UsersStatus::Ok) {
        refresh();
    }
}

void QtUsersPage::reportStatus(presenter::UsersStatus status,
                               const QString& successText) {
    if (status == presenter::UsersStatus::Ok) {
        ui_->statusLabel->setStyleSheet(theme::coloredBold(theme::kColorOk));
        ui_->statusLabel->setText(successText);
    } else {
        ui_->statusLabel->setStyleSheet(theme::coloredBold(theme::kColorAlarm));
        ui_->statusLabel->setText(localizedStatus(status));
    }
}

void QtUsersPage::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        ui_->retranslateUi(this);
        applyHeaderLabels();
        refresh();  // re-localise the role / enabled cells + footer
    }
    QWidget::changeEvent(event);
}

}  // namespace app::view
