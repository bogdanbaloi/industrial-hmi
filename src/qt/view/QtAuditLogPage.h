#pragma once

#include <QWidget>

#include <memory>

class QEvent;
class QTimer;

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtAuditLogPage;
}

namespace app::auth {
class AuditLogger;
struct AuditQuery;
}

namespace app::view {

/// Admin-only audit log viewer. A pure View over the auth::AuditLogger read
/// surface (the same reader the GTK AuditLogPage renders): a filter row drives
/// an AuditQuery, the table shows the matching events, and a timer auto-refreshes
/// like the History page. No business logic -- visibility is gated at mount time
/// (Admin only). Mounted only when the audit log opened.
class QtAuditLogPage : public QWidget {
public:
    explicit QtAuditLogPage(auth::AuditLogger& reader, QWidget* parent = nullptr);
    ~QtAuditLogPage() override;

    QtAuditLogPage(const QtAuditLogPage&)            = delete;
    QtAuditLogPage& operator=(const QtAuditLogPage&) = delete;
    QtAuditLogPage(QtAuditLogPage&&)                 = delete;
    QtAuditLogPage& operator=(QtAuditLogPage&&)      = delete;

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyHeaderLabels();
    void populateFilters();
    void refresh();
    /// Save the currently-filtered events (no row cap) as RFC 4180 CSV at an
    /// operator-chosen path. Native save dialog -- the operator picks the file.
    void exportCsv();
    [[nodiscard]] auth::AuditQuery buildQuery() const;

    std::unique_ptr<Ui::QtAuditLogPage> ui_;
    auth::AuditLogger&                  reader_;
    QTimer*                             autoRefresh_{nullptr};
};

}  // namespace app::view
