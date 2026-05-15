#pragma once

#include "src/auth/AuditLogger.h"
#include "src/core/LoggerBase.h"

#include <mutex>
#include <string>

struct sqlite3;

namespace app::auth {

/// SQLite-backed AuditLogger.
///
/// Shares a database file with the user repository by default
/// (data/auth.sqlite). Two tables, two domains, one file -- keeping
/// auth-related state co-located simplifies backup + permission
/// management on the deployed terminal. A future deployment that
/// wants to ship audit to a centralised SIEM swaps the concrete here
/// without touching the call sites.
///
/// Schema:
/// @code
///   CREATE TABLE audit_log (
///       id        INTEGER PRIMARY KEY AUTOINCREMENT,
///       ts        TEXT NOT NULL,   -- ISO 8601 UTC
///       username  TEXT NOT NULL,
///       role      TEXT NOT NULL,
///       category  TEXT NOT NULL,
///       action    TEXT NOT NULL,
///       details   TEXT NOT NULL,
///       result    TEXT NOT NULL
///   );
///   CREATE INDEX idx_audit_ts       ON audit_log(ts);
///   CREATE INDEX idx_audit_category ON audit_log(category, ts);
/// @endcode
///
/// The `(category, ts)` index covers the dominant query shape: "every
/// PRODUCTION event in this hour" / "every LOGIN in the last day".
class SqliteAuditLogger : public AuditLogger {
public:
    struct Config {
        /// Filesystem path. `:memory:` for tests; relative paths
        /// resolve against the binary's cwd. Same default as the user
        /// repo so they share one file.
        std::string dbPath{":memory:"};
    };

    explicit SqliteAuditLogger(Config config);
    ~SqliteAuditLogger() override;

    SqliteAuditLogger(const SqliteAuditLogger&)            = delete;
    SqliteAuditLogger& operator=(const SqliteAuditLogger&) = delete;
    SqliteAuditLogger(SqliteAuditLogger&&)                 = delete;
    SqliteAuditLogger& operator=(SqliteAuditLogger&&)      = delete;

    void setLogger(app::core::Logger& logger) { logger_ = &logger; }

    /// One-shot schema bootstrap. Idempotent. Returns false on file-
    /// open failure -- caller logs a warning and the logger pointer
    /// is left as nullopt by the composition root, so all `record()`
    /// call sites short-circuit.
    [[nodiscard]] bool initialize();

    bool record(const AuditEvent& event) override;
    [[nodiscard]] std::vector<AuditEvent> query(const AuditQuery& q) override;
    [[nodiscard]] std::size_t totalEvents() const override;

private:
    Config              config_;
    sqlite3*            db_{nullptr};
    mutable std::mutex  mutex_;
    app::core::Logger*  logger_{nullptr};
};

}  // namespace app::auth
