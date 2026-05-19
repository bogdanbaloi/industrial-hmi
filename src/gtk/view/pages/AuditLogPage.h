#pragma once

#include "src/gtk/view/pages/Page.h"
#include "src/auth/AuditLogger.h"

#include <gtkmm.h>

#include <cstdint>

namespace app::view {

class Toast;

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
    void onExportClicked();
    void refresh();

    /// Translate the current filter widgets into an AuditQuery. The
    /// row cap is parameterised so the on-screen path passes
    /// kQueryLimit (UI-friendly slice) while CSV export passes 0
    /// (no cap, full match).
    [[nodiscard]] app::auth::AuditQuery buildQuery(std::size_t limit) const;

    /// Run the current filter set against the audit logger with no
    /// row cap and write the resulting events to `path` as RFC 4180
    /// CSV (UTF-8 BOM so Excel detects encoding). Returns the number
    /// of rows written, or `-1` on I/O failure. `std::int64_t` (not
    /// `long`) for portable width -- `long` is 32-bit on Windows LLP64
    /// and clang-tidy google-runtime-int rejects it.
    [[nodiscard]] std::int64_t writeCsvExport(const std::string& path);

    /// Append one row's worth of cells to the grid at the next row
    /// index. A Gtk::Grid is used (rather than a stack of Boxes)
    /// because it guarantees column alignment across rows even when
    /// the header row has a bolder / larger font than the data
    /// rows -- a Box+set_width_chars would let bold characters
    /// blow the column wider than the regular-weight data, and the
    /// misalignment would accumulate left-to-right.
    void appendRow(const app::auth::AuditEvent& e);

    app::auth::AuditLogger& reader_;

    Gtk::ComboBoxText*           categoryFilter_{nullptr};
    Gtk::ComboBoxText*           actionFilter_{nullptr};
    Gtk::ComboBoxText*           resultFilter_{nullptr};
    Gtk::ComboBoxText*           rangeFilter_{nullptr};
    Gtk::Entry*                  usernameFilter_{nullptr};
    Gtk::Button*                 refreshButton_{nullptr};
    Gtk::Button*                 exportButton_{nullptr};
    Toast*                       toast_{nullptr};
    Gtk::ScrolledWindow*         scroller_{nullptr};
    Gtk::Grid*                   grid_{nullptr};
    Gtk::Label*                  footerLabel_{nullptr};

    /// Next free grid row. Header at row 0, data starts at 1; bumped
    /// after every appendRow().
    int                          nextRow_{1};

    sigc::connection             autoRefreshConn_;
};

}  // namespace app::view
