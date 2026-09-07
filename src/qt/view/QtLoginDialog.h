#pragma once

#include <QDialog>

#include <memory>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtLoginDialog;
}

namespace app::auth {
class AuthService;
}

namespace app::view {

/// Modal sign-in dialog for the Qt frontend. The direct counterpart to the GTK
/// LoginDialog: it drives the same toolkit-agnostic AuthService (Argon2id verify
/// over the SQLite user store) and, on success, the service writes the
/// authenticated principal into the shared Session. The composition root shows
/// this before building the main window and treats a rejected dialog as "the
/// operator declined to sign in" (exit).
///
/// A QDialog (not a bare page), so `exec()` gives the modal, blocking loop the
/// login gate needs before the event loop proper starts.
class QtLoginDialog : public QDialog {
public:
    explicit QtLoginDialog(auth::AuthService& service,
                           QWidget* parent = nullptr);
    ~QtLoginDialog() override;

    QtLoginDialog(const QtLoginDialog&)            = delete;
    QtLoginDialog& operator=(const QtLoginDialog&) = delete;
    QtLoginDialog(QtLoginDialog&&)                 = delete;
    QtLoginDialog& operator=(QtLoginDialog&&)      = delete;

private:
    /// Validate the fields, call AuthService::login, and either accept() the
    /// dialog (Session now set) or show a reason and keep it open.
    void attemptLogin();
    void showError(const QString& message);

    auth::AuthService&               service_;
    std::unique_ptr<Ui::QtLoginDialog> ui_;
};

}  // namespace app::view
