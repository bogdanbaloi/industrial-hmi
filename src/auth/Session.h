#pragma once

#include "src/auth/User.h"

#include <sigc++/signal.h>

#include <mutex>
#include <optional>
#include <string>

namespace app::auth {

/// Process-wide "who is currently logged in" state.
///
/// One active user at a time -- the HMI is a single-operator terminal;
/// multi-tenant sessions would change the UI contract substantially
/// and the OPC-UA stack already separates external read sessions from
/// the operator persona. Inserting a multi-user layer here would
/// duplicate that.
///
/// Thread safety: AuthService writes from the GTK main thread (login
/// dialog response); presenter callbacks may read from worker threads
/// (model ticks, ingest bridges) when audit-logging an action. The
/// internal mutex serialises both sides; reads return a snapshot copy
/// so the caller doesn't hold the lock past the call.
class Session {
public:
    Session()  = default;
    ~Session() = default;

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&)                 = delete;
    Session& operator=(Session&&)      = delete;

    /// Set the currently-logged-in user. AuthService calls this after
    /// a successful login; the LoginDialog reads `currentUser()`
    /// before allowing the main window to spawn.
    ///
    /// Also called by UsersPresenter when the signed-in user's own
    /// row is edited (display name / avatar change) so the cached
    /// snapshot stays in sync with storage. Fires `signalChanged`
    /// after the write so observers (UserBadge) refresh.
    void setUser(User user) {
        {
            const std::scoped_lock lock(mutex_);
            current_ = std::move(user);
        }
        signalChanged_.emit();
    }

    /// Clear the session (logout / startup). Also fires
    /// `signalChanged` so observers can drop their cached view.
    void clear() {
        {
            const std::scoped_lock lock(mutex_);
            current_.reset();
        }
        signalChanged_.emit();
    }

    /// Snapshot copy of the active user, or `nullopt` when no one is
    /// logged in (pre-login or after logout). Returning by value lets
    /// callers iterate without holding the mutex.
    [[nodiscard]] std::optional<User> currentUser() const {
        const std::scoped_lock lock(mutex_);
        return current_;
    }

    /// Convenience: is someone logged in?
    [[nodiscard]] bool isAuthenticated() const {
        const std::scoped_lock lock(mutex_);
        return current_.has_value();
    }

    /// Convenience: short label for audit rows + sidebar widgets.
    /// Returns "unknown" when no user is set; callers that care
    /// distinguish via `isAuthenticated()`.
    [[nodiscard]] std::string currentUsername() const {
        const std::scoped_lock lock(mutex_);
        return current_.has_value() ? current_->username : std::string{"unknown"};
    }

    /// Notification fired after every `setUser` / `clear`. Connect from
    /// view widgets (UserBadge) that mirror the current identity so
    /// they redraw when an admin edits the signed-in user's row mid-
    /// session. Emitted outside the mutex so observers can re-enter
    /// `currentUser()` without deadlocking.
    [[nodiscard]] sigc::signal<void()>& signalChanged() {
        return signalChanged_;
    }

private:
    mutable std::mutex   mutex_;
    std::optional<User>  current_;
    sigc::signal<void()> signalChanged_;
};

}  // namespace app::auth
