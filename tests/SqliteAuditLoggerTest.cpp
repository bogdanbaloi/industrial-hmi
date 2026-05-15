// Tests for SqliteAuditLogger.
//
// All cases run against `:memory:` so the suite is hermetic. The same
// code paths execute on a file-backed deployment because SQLite's
// in-memory mode is bit-identical except for persistence.

#include "src/auth/SqliteAuditLogger.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using app::auth::AuditEvent;
using app::auth::AuditQuery;
using app::auth::SqliteAuditLogger;

std::unique_ptr<SqliteAuditLogger> makeLogger() {
    auto logger = std::make_unique<SqliteAuditLogger>(
        SqliteAuditLogger::Config{.dbPath = ":memory:"});
    EXPECT_TRUE(logger->initialize());
    return logger;
}

AuditEvent sampleEvent(const std::string& category   = "AUTH",
                       const std::string& action     = "LOGIN",
                       const std::string& user       = "alice",
                       const std::string& role       = "OPERATOR",
                       const std::string& outcome    = "SUCCESS") {
    AuditEvent e;
    e.username = user;
    e.role     = role;
    e.category = category;
    e.action   = action;
    e.details  = "";
    e.result   = outcome;
    return e;
}

}  // namespace

TEST(SqliteAuditLoggerTest, EmptyLogReportsZero) {
    auto logger = makeLogger();
    EXPECT_EQ(logger->totalEvents(), 0U);
    EXPECT_TRUE(logger->query(AuditQuery{}).empty());
}

TEST(SqliteAuditLoggerTest, RecordSingleRowRoundTrip) {
    auto logger = makeLogger();
    EXPECT_TRUE(logger->record(sampleEvent()));
    EXPECT_EQ(logger->totalEvents(), 1U);

    const auto rows = logger->query(AuditQuery{});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].username, "alice");
    EXPECT_EQ(rows[0].action,   "LOGIN");
    EXPECT_EQ(rows[0].result,   "SUCCESS");
    // The logger fills the timestamp when caller left it empty.
    EXPECT_FALSE(rows[0].timestamp.empty());
}

TEST(SqliteAuditLoggerTest, RecordPreservesCallerTimestamp) {
    auto logger = makeLogger();
    auto e      = sampleEvent();
    e.timestamp = "2030-01-01T00:00:00Z";
    EXPECT_TRUE(logger->record(e));

    const auto rows = logger->query(AuditQuery{});
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].timestamp, "2030-01-01T00:00:00Z");
}

TEST(SqliteAuditLoggerTest, QueryFiltersByCategory) {
    auto logger = makeLogger();
    logger->record(sampleEvent("AUTH",       "LOGIN"));
    logger->record(sampleEvent("PRODUCTION", "START"));
    logger->record(sampleEvent("AUTH",       "LOGOUT"));

    AuditQuery q;
    q.category = "AUTH";
    const auto rows = logger->query(q);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].category, "AUTH");
    EXPECT_EQ(rows[1].category, "AUTH");
}

TEST(SqliteAuditLoggerTest, QueryFiltersByUsername) {
    auto logger = makeLogger();
    logger->record(sampleEvent("AUTH", "LOGIN", "alice"));
    logger->record(sampleEvent("AUTH", "LOGIN", "bob"));
    logger->record(sampleEvent("AUTH", "LOGOUT", "alice"));

    AuditQuery q;
    q.username = "alice";
    const auto rows = logger->query(q);
    ASSERT_EQ(rows.size(), 2U);
    for (const auto& r : rows) {
        EXPECT_EQ(r.username, "alice");
    }
}

TEST(SqliteAuditLoggerTest, QueryReturnsNewestFirst) {
    auto logger = makeLogger();
    AuditEvent e1 = sampleEvent(); e1.timestamp = "2024-01-01T00:00:00Z";
    AuditEvent e2 = sampleEvent(); e2.timestamp = "2024-06-01T00:00:00Z";
    AuditEvent e3 = sampleEvent(); e3.timestamp = "2024-03-01T00:00:00Z";
    logger->record(e1);
    logger->record(e2);
    logger->record(e3);

    const auto rows = logger->query(AuditQuery{});
    ASSERT_EQ(rows.size(), 3U);
    // Audit UI needs recent activity at the top, so DESC ordering
    // is the contract -- not just convenience.
    EXPECT_EQ(rows[0].timestamp, "2024-06-01T00:00:00Z");
    EXPECT_EQ(rows[1].timestamp, "2024-03-01T00:00:00Z");
    EXPECT_EQ(rows[2].timestamp, "2024-01-01T00:00:00Z");
}

TEST(SqliteAuditLoggerTest, QueryHonoursLimit) {
    auto logger = makeLogger();
    for (int i = 0; i < 10; ++i) {
        logger->record(sampleEvent("AUTH", "LOGIN",
                                   "user" + std::to_string(i)));
    }
    AuditQuery q;
    q.limit = 3;
    EXPECT_EQ(logger->query(q).size(), 3U);
}

TEST(SqliteAuditLoggerTest, QueryByTimeRange) {
    auto logger = makeLogger();
    AuditEvent e1 = sampleEvent(); e1.timestamp = "2024-01-01T00:00:00Z";
    AuditEvent e2 = sampleEvent(); e2.timestamp = "2024-06-01T00:00:00Z";
    AuditEvent e3 = sampleEvent(); e3.timestamp = "2024-12-01T00:00:00Z";
    logger->record(e1);
    logger->record(e2);
    logger->record(e3);

    AuditQuery q;
    q.fromTs = "2024-05-01T00:00:00Z";
    q.toTs   = "2024-07-01T00:00:00Z";
    const auto rows = logger->query(q);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].timestamp, "2024-06-01T00:00:00Z");
}

TEST(SqliteAuditLoggerTest, FailureRowsRecordedDistinctly) {
    auto logger = makeLogger();
    logger->record(sampleEvent("AUTH", "LOGIN", "alice", "OPERATOR",
                               "SUCCESS"));
    logger->record(sampleEvent("AUTH", "LOGIN", "alice", "OPERATOR",
                               "FAILURE"));

    const auto rows = logger->query(AuditQuery{});
    ASSERT_EQ(rows.size(), 2U);
    // Both rows survive without merging; the audit log is a record
    // of EVERY attempt, not a deduped state.
    int successes = 0;
    int failures  = 0;
    for (const auto& r : rows) {
        if (r.result == "SUCCESS") ++successes;
        if (r.result == "FAILURE") ++failures;
    }
    EXPECT_EQ(successes, 1);
    EXPECT_EQ(failures,  1);
}

TEST(SqliteAuditLoggerTest, InitializeIsIdempotent) {
    SqliteAuditLogger logger(
        SqliteAuditLogger::Config{.dbPath = ":memory:"});
    EXPECT_TRUE(logger.initialize());
    // A second call must not throw or fail -- the schema is wrapped
    // in IF NOT EXISTS so a stale process restart succeeds.
    EXPECT_TRUE(logger.initialize());
}
