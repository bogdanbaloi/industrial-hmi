#pragma once

#include "src/auth/User.h"

#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QEvent;

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtUsersPage;
}

namespace app::presenter {
class UsersPresenter;
enum class UsersStatus : std::uint8_t;
}

namespace app::view {

/// Admin-only user management page. A pure View over UsersPresenter: it renders
/// the user table and opens the add / edit / reset / delete dialogs, but every
/// mutation goes through the presenter (which enforces RBAC + writes the audit
/// trail). The same presenter the GTK UsersPage drives, so reusing it behind Qt
/// continues the toolkit-independence proof. Mounted only for an Admin session.
class QtUsersPage : public QWidget {
public:
    explicit QtUsersPage(presenter::UsersPresenter& presenter,
                         QWidget* parent = nullptr);
    ~QtUsersPage() override;

    QtUsersPage(const QtUsersPage&)            = delete;
    QtUsersPage& operator=(const QtUsersPage&) = delete;
    QtUsersPage(QtUsersPage&&)                 = delete;
    QtUsersPage& operator=(QtUsersPage&&)      = delete;

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyHeaderLabels();
    void refresh();
    void updateActionButtons();
    void onAdd();
    void onEdit();
    void onResetPassword();
    void onDelete();

    /// The user id of the selected row, or nullopt when nothing is selected.
    [[nodiscard]] std::optional<std::int64_t> selectedUserId() const;
    /// Find a cached user by id (the row-selection handlers need the full row).
    [[nodiscard]] std::optional<auth::User> findUser(std::int64_t id) const;
    /// Surface a presenter result: the success text on Ok, the mapped failure
    /// message otherwise.
    void reportStatus(presenter::UsersStatus status, const QString& successText);

    std::unique_ptr<Ui::QtUsersPage> ui_;
    presenter::UsersPresenter&       presenter_;
    std::vector<auth::User>          users_;
};

}  // namespace app::view
