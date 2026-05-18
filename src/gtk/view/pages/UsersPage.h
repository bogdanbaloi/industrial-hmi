#pragma once

#include "src/gtk/view/pages/Page.h"
#include "src/presenter/UsersPresenter.h"

#include <gtkmm.h>

namespace app::view {

class Toast;

/// Admin-only user management page.
///
/// Mirrors AuditLogPage in shape: scrolling Gtk::Grid for the listing
/// (per-column alignment between the bold header and the
/// regular-weight data), a toolbar with the "Add user" affordance, and
/// per-row buttons that open modal dialogs for edit / reset-password /
/// delete actions.
///
/// @design The page does NOT know about the auth model directly -- it
/// talks to UsersPresenter which gates everything by role + emits
/// audit. The presenter's `list()` already returns an empty vector if
/// the caller isn't admin, so even a forced registration of this page
/// would render blank for an operator (defence-in-depth).
///
/// @threading GTK main thread only. The presenter's calls are
/// synchronous (no async write paths -- writes go through SQLite on
/// the calling thread, latency is sub-ms).
class UsersPage : public Page {
public:
    UsersPage(DialogManager& dialogManager,
              app::presenter::UsersPresenter& presenter);
    ~UsersPage() override = default;

    [[nodiscard]] Glib::ustring pageTitle() const override;

private:
    void buildUi();
    void refresh();
    void onAddClicked();
    void onEditClicked(std::int64_t id);
    void onResetPasswordClicked(std::int64_t id, const std::string& username);
    void onDeleteClicked(std::int64_t id, const std::string& username);

    /// Append one user's worth of cells to the grid at the next row
    /// index. See AuditLogPage::appendRow for the Grid-vs-Box choice.
    void appendRow(const app::auth::User& u);

    /// Centralised result feedback: maps a UsersStatus to a localised
    /// message and surfaces it via the page's Toast banner. Ok runs
    /// `successText` through the toast on success; every other code
    /// reports the failure message in the error tone so the operator
    /// sees WHY the action was rejected.
    void reportStatus(app::presenter::UsersStatus s,
                      const Glib::ustring& successText);

    app::presenter::UsersPresenter& presenter_;

    Toast*               toast_{nullptr};
    Gtk::Button*         addButton_{nullptr};
    Gtk::Button*         refreshButton_{nullptr};
    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::Grid*           grid_{nullptr};
    Gtk::Label*          footerLabel_{nullptr};

    /// Next free grid row. Header at row 0, data starts at 1; bumped
    /// after every appendRow().
    int                  nextRow_{1};
};

}  // namespace app::view
