#pragma once

#include "src/gtk/view/pages/Page.h"
#include "src/auth/AuditLogger.h"

#include <gtkmm.h>

#include <cstdint>

namespace app::view {

/// Admin-only audit log viewer.
///
/// Renders a filter row (category, username, time range) plus a
/// scrollable list of rows pulled from the injected AuditLogger.
/// Refresh re-runs the query; an auto-refresh timer keeps the view
/// alive without operator clicks (5 s, mirrors the History page).
///
/// @design Pure View in the MVP sense -- no business logic. Visibility
/// gating (admin only) is enforced at the MainWindow level by
/// conditional registration; this page assumes a non-null reader and
/// trusts the caller for permission. A future user-management page
/// will follow the same pattern.
///
/// @threading GTK main thread only. The reader's query() is fast
/// (compound-indexed range scan capped at 500 rows), so we don't punt
/// to a worker for the MVP.
class AuditLogPage : public Page {
public:
    AuditLogPage(DialogManager& dialogManager,
                 app::auth::AuditLogger& reader);
    ~AuditLogPage() override;

    [[nodiscard]] Glib::ustring pageTitle() const override;

private:
    void buildUi();
    void onRefreshClicked();
    void onFilterChanged();
    void refresh();

    /// Append one row's worth of cells to the ListView model. Builds
    /// a `Gtk::Box` per row so we get cheap horizontal layout without
    /// pulling in `Gtk::ColumnView` (which would require a property
    /// factory per column -- overkill for an MVP page).
    void appendRow(const app::auth::AuditEvent& e);

    app::auth::AuditLogger& reader_;

    Gtk::ComboBoxText*           categoryFilter_{nullptr};
    Gtk::Entry*                  usernameFilter_{nullptr};
    Gtk::Button*                 refreshButton_{nullptr};
    Gtk::ScrolledWindow*         scroller_{nullptr};
    Gtk::Box*                    listBox_{nullptr};
    Gtk::Label*                  footerLabel_{nullptr};

    sigc::connection             autoRefreshConn_;
};

}  // namespace app::view
