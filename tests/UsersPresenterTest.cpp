// Implements: REQ-AUTH-002 (three-tier RBAC).
//
// Tests for UsersPresenter.
//
// Strategy: exercise the presenter against a real SqliteUserRepository
// (`:memory:`) + real Argon2 hasher. The audit logger is faked so the
// suite can assert on emitted events without spinning up another
// SQLite file.
//
// Audit assertions deliberately check (category, action, result) only
// -- the free-form `details` string is implementation detail that
// shouldn't lock down behaviour at the test level.

#include "src/presenter/UsersPresenter.h"

#include "src/auth/Argon2PasswordHasher.h"
#include "src/auth/AuditLogger.h"
#include "src/auth/Session.h"
#include "src/auth/SqliteUserRepository.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using app::auth::Argon2PasswordHasher;
using app::auth::AuditEvent;
using app::auth::AuditLogger;
using app::auth::AuditQuery;
using app::auth::Role;
using app::auth::Session;
using app::auth::SqliteUserRepository;
using app::auth::User;
using app::presenter::UsersPresenter;
using app::presenter::UsersStatus;

/// In-memory audit logger that records every event. Mutex because the
/// presenter could in principle be called from the GTK main thread
/// while a test thread inspects -- in practice everything is on-thread
/// here, but the mutex is cheap insurance.
class FakeAuditLogger : public AuditLogger {
public:
    bool record(const AuditEvent& event) override {
        const std::scoped_lock lock(mutex_);
        events_.push_back(event);
        return true;
    }
    std::vector<AuditEvent> query(const AuditQuery&) override {
        const std::scoped_lock lock(mutex_);
        return events_;
    }
    std::size_t totalEvents() const override {
        const std::scoped_lock lock(mutex_);
        return events_.size();
    }
    std::vector<AuditEvent> snapshot() const {
        const std::scoped_lock lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex      mutex_;
    std::vector<AuditEvent> events_;
};

class UsersPresenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        repo_ = std::make_unique<SqliteUserRepository>(
            SqliteUserRepository::Config{.dbPath = ":memory:"});
        ASSERT_TRUE(repo_->initialize());
        presenter_ = std::make_unique<UsersPresenter>(
            *repo_, hasher_, session_, audit_);

        // Seed three users so RBAC tests have something to act on.
        // We bypass the presenter here (the presenter needs a logged-in
        // admin to create anything) -- we go straight to the repo +
        // hasher so the fixture stays compact.
        adminId_ = seed("admin", "adminpass", Role::Admin);
        opId_    = seed("operator", "operpass", Role::Operator);
        maintId_ = seed("maint", "maintpass", Role::Maintenance);
    }

    std::int64_t seed(const char* name, const char* password, Role r) {
        User u;
        u.username     = name;
        u.role         = r;
        u.enabled      = true;
        u.passwordHash = hasher_.hash(password);
        const auto created = repo_->create(u);
        EXPECT_TRUE(created.has_value());
        return created.has_value() ? created->id : -1;
    }

    void loginAs(std::int64_t id) {
        const auto u = repo_->findById(id);
        ASSERT_TRUE(u.has_value());
        session_.setUser(*u);
    }

    /// Count events with the given action verb in the audit feed.
    /// Distinguish success vs failure by passing the corresponding
    /// `app::auth::result::kSuccess` / `kFailure` string.
    int countAuditAction(std::string_view action,
                         std::string_view result) const {
        int n = 0;
        for (const auto& e : audit_.snapshot()) {
            if (e.action == action && e.result == result) ++n;
        }
        return n;
    }

    Argon2PasswordHasher                  hasher_;
    Session                               session_;
    FakeAuditLogger                       audit_;
    std::unique_ptr<SqliteUserRepository> repo_;
    std::unique_ptr<UsersPresenter>       presenter_;
    std::int64_t                          adminId_{-1};
    std::int64_t                          opId_{-1};
    std::int64_t                          maintId_{-1};
};

}  // namespace

// --- RBAC: anonymous + non-admin --------------------------------------

TEST_F(UsersPresenterTest, ListReturnsEmptyForUnauthenticated) {
    EXPECT_TRUE(presenter_->list().empty());
}

TEST_F(UsersPresenterTest, ListReturnsEmptyForOperator) {
    loginAs(opId_);
    EXPECT_TRUE(presenter_->list().empty());
}

TEST_F(UsersPresenterTest, ListReturnsEmptyForMaintenance) {
    // Maintenance is below the threshold for user management; the
    // presenter must refuse the read even though the sidebar would
    // already have hidden the page.
    loginAs(maintId_);
    EXPECT_TRUE(presenter_->list().empty());
}

TEST_F(UsersPresenterTest, AdminCanListAllUsers) {
    loginAs(adminId_);
    const auto rows = presenter_->list();
    EXPECT_EQ(rows.size(), 3U);  // three seeded
}

TEST_F(UsersPresenterTest, OperatorCannotCreate) {
    loginAs(opId_);
    EXPECT_EQ(presenter_->create("alice", "alicepass", Role::Operator),
              UsersStatus::Forbidden);
    EXPECT_EQ(countAuditAction("CREATE",
                               app::auth::result::kFailure), 1);
}

TEST_F(UsersPresenterTest, MaintenanceCannotCreate) {
    loginAs(maintId_);
    EXPECT_EQ(presenter_->create("alice", "alicepass", Role::Operator),
              UsersStatus::Forbidden);
}

TEST_F(UsersPresenterTest, OperatorCannotDelete) {
    loginAs(opId_);
    EXPECT_EQ(presenter_->remove(maintId_), UsersStatus::Forbidden);
}

TEST_F(UsersPresenterTest, OperatorCannotResetSomeoneElsesPassword) {
    loginAs(opId_);
    EXPECT_EQ(presenter_->resetPassword(adminId_, "newpassword"),
              UsersStatus::Forbidden);
}

// --- create -----------------------------------------------------------

TEST_F(UsersPresenterTest, AdminCanCreateUser) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->create("alice", "alicepass",
                                 Role::Operator, "Alice"),
              UsersStatus::Ok);
    EXPECT_EQ(countAuditAction("CREATE",
                               app::auth::result::kSuccess), 1);

    const auto rows = presenter_->list();
    bool found = false;
    for (const auto& u : rows) {
        if (u.username == "alice") {
            found = true;
            EXPECT_EQ(u.displayName, "Alice");
            EXPECT_EQ(u.role, Role::Operator);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(UsersPresenterTest, CreateLowercasesUsername) {
    // Storage normalisation must match the COLLATE NOCASE the repo
    // already enforces. Presenter does the canonicalisation so the
    // audit row records "alice", not "ALICE".
    loginAs(adminId_);
    EXPECT_EQ(presenter_->create("ALICE", "alicepass", Role::Operator),
              UsersStatus::Ok);
    EXPECT_TRUE(repo_->findByUsername("alice").has_value());
}

TEST_F(UsersPresenterTest, CreateRejectsShortPassword) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->create("alice", "short", Role::Operator),
              UsersStatus::ValidationFailed);
    EXPECT_EQ(countAuditAction("CREATE",
                               app::auth::result::kFailure), 1);
}

TEST_F(UsersPresenterTest, CreateRejectsInvalidUsername) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->create("", "longenough", Role::Operator),
              UsersStatus::ValidationFailed);
    EXPECT_EQ(presenter_->create("ab", "longenough", Role::Operator),
              UsersStatus::ValidationFailed);
    EXPECT_EQ(presenter_->create("42abc", "longenough", Role::Operator),
              UsersStatus::ValidationFailed);
    EXPECT_EQ(presenter_->create("bad name", "longenough", Role::Operator),
              UsersStatus::ValidationFailed);
}

TEST_F(UsersPresenterTest, CreateRejectsDuplicateUsername) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->create("alice", "alicepass", Role::Operator),
              UsersStatus::Ok);
    EXPECT_EQ(presenter_->create("ALICE", "differentpass", Role::Operator),
              UsersStatus::DuplicateUsername);
}

// --- update -----------------------------------------------------------

TEST_F(UsersPresenterTest, AdminCanUpdateRoleAndDisplayName) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->update(opId_, Role::Maintenance,
                                 "The Operator", true),
              UsersStatus::Ok);
    const auto refreshed = repo_->findById(opId_);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_EQ(refreshed->role,        Role::Maintenance);
    EXPECT_EQ(refreshed->displayName, "The Operator");
}

TEST_F(UsersPresenterTest, UpdateRefusesSelfDisable) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->update(adminId_, Role::Admin, "", false),
              UsersStatus::SelfMutationRefused);
    // The row must remain enabled -- defence-in-depth, the binary
    // can't lock itself out.
    EXPECT_TRUE(repo_->findById(adminId_)->enabled);
}

TEST_F(UsersPresenterTest, UpdateOnMissingIdReportsNotFound) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->update(9999, Role::Operator, "", true),
              UsersStatus::NotFound);
}

// --- remove -----------------------------------------------------------

TEST_F(UsersPresenterTest, AdminCanDeleteAnotherUser) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->remove(opId_), UsersStatus::Ok);
    EXPECT_FALSE(repo_->findById(opId_).has_value());
    EXPECT_EQ(countAuditAction("DELETE",
                               app::auth::result::kSuccess), 1);
}

TEST_F(UsersPresenterTest, RemoveRefusesSelfDelete) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->remove(adminId_),
              UsersStatus::SelfMutationRefused);
    EXPECT_TRUE(repo_->findById(adminId_).has_value());
}

// --- resetPassword ---------------------------------------------------

TEST_F(UsersPresenterTest, AdminCanResetSomeoneElsesPassword) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->resetPassword(opId_, "brand-new-pass"),
              UsersStatus::Ok);
    // Verify the new hash actually works -- defence-in-depth that the
    // password truly changed, not that we merely wrote ".".
    const auto refreshed = repo_->findById(opId_);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_TRUE(hasher_.verify("brand-new-pass",
                               refreshed->passwordHash));
}

TEST_F(UsersPresenterTest, ResetPasswordRejectsShort) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->resetPassword(opId_, "short"),
              UsersStatus::ValidationFailed);
}

// --- changeOwnPassword -----------------------------------------------

TEST_F(UsersPresenterTest, AnyAuthenticatedUserCanChangeOwnPassword) {
    loginAs(opId_);   // operator -- the lowest privilege role
    EXPECT_EQ(presenter_->changeOwnPassword("operpass", "new-good-pass"),
              UsersStatus::Ok);
    EXPECT_TRUE(hasher_.verify("new-good-pass",
                               repo_->findById(opId_)->passwordHash));
}

TEST_F(UsersPresenterTest, ChangeOwnPasswordRejectsWrongOldPassword) {
    loginAs(opId_);
    EXPECT_EQ(presenter_->changeOwnPassword("not-the-real-one",
                                            "new-good-pass"),
              UsersStatus::WrongPassword);
    // The stored hash must NOT have changed.
    EXPECT_TRUE(hasher_.verify("operpass",
                               repo_->findById(opId_)->passwordHash));
}

TEST_F(UsersPresenterTest, ChangeOwnPasswordRejectsShortNew) {
    loginAs(opId_);
    EXPECT_EQ(presenter_->changeOwnPassword("operpass", "short"),
              UsersStatus::ValidationFailed);
}

TEST_F(UsersPresenterTest, ChangeOwnPasswordRequiresLogin) {
    EXPECT_EQ(presenter_->changeOwnPassword("anything", "longenough"),
              UsersStatus::Forbidden);
}

// --- avatars ---------------------------------------------------------

TEST_F(UsersPresenterTest, AnyUserCanSetTheirOwnAvatar) {
    loginAs(opId_);
    const std::vector<std::uint8_t> bytes(64U, 0xAA);
    EXPECT_EQ(presenter_->setOwnAvatar(bytes, "image/png"),
              UsersStatus::Ok);
    const auto av = presenter_->getAvatar(opId_);
    ASSERT_TRUE(av.has_value());
    EXPECT_EQ(av->bytes, bytes);
}

TEST_F(UsersPresenterTest, SetOwnAvatarRejectsBadMime) {
    loginAs(opId_);
    const std::vector<std::uint8_t> bytes(64U, 0xAA);
    EXPECT_EQ(presenter_->setOwnAvatar(bytes, "image/gif"),
              UsersStatus::ValidationFailed);
}

TEST_F(UsersPresenterTest, ClearOwnAvatarDropsBlob) {
    loginAs(opId_);
    const std::vector<std::uint8_t> bytes(64U, 0xAA);
    ASSERT_EQ(presenter_->setOwnAvatar(bytes, "image/png"),
              UsersStatus::Ok);
    EXPECT_EQ(presenter_->clearOwnAvatar(), UsersStatus::Ok);
    EXPECT_FALSE(presenter_->getAvatar(opId_).has_value());
}

TEST_F(UsersPresenterTest, AvatarReadRequiresAuth) {
    EXPECT_FALSE(presenter_->getAvatar(opId_).has_value());
}

// --- audit format (the canonical row shape compliance walks rely on) -
//
// These tests pin down WHAT lands in the audit log, not just how
// many rows show up. The on-screen filter dropdowns + the CSV export
// both depend on stable values for category, action, and result;
// breaking those fields silently would corrupt historical exports.
//
// The `details` field is free-form by design (operator-friendly
// summary), so the tests assert it CONTAINS the key identifiers
// rather than matching the exact string -- that lets us adjust
// wording later without churning every test.

namespace {

/// Find the LAST audit event matching the given action verb. Returns
/// nullopt when no such row exists. Last-not-first because each test
/// sets up some state (which itself may emit audit rows for the
/// seeded actions) and the assertion targets the rows generated by
/// the test's own action.
std::optional<AuditEvent> lastAction(const FakeAuditLogger& a,
                                     std::string_view action) {
    const auto snapshot = a.snapshot();
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
        if (it->action == action) return *it;
    }
    return std::nullopt;
}

bool detailsContain(const AuditEvent& e, std::string_view needle) {
    return e.details.find(needle) != std::string::npos;
}

}  // namespace

TEST_F(UsersPresenterTest, AuditCreateRecordsCategoryAndIdentifiers) {
    loginAs(adminId_);
    ASSERT_EQ(presenter_->create("alice", "alicepass",
                                 Role::Maintenance, "Alice B."),
              UsersStatus::Ok);

    const auto e = lastAction(audit_, "CREATE");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->result,   "SUCCESS");
    EXPECT_EQ(e->username, "admin");
    EXPECT_EQ(e->role,     "ADMIN");
    EXPECT_TRUE(detailsContain(*e, "username=alice"));
    EXPECT_TRUE(detailsContain(*e, "role=MAINTENANCE"));
}

TEST_F(UsersPresenterTest, AuditCreateFailureRecordsRejectReason) {
    // Forbidden -- operator attempts create. The audit row must
    // carry SUCCESS=false + the attempted username so an admin
    // sees the escalation attempt at a glance.
    loginAs(opId_);
    EXPECT_EQ(presenter_->create("alice", "longenough", Role::Operator),
              UsersStatus::Forbidden);

    const auto e = lastAction(audit_, "CREATE");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->result,   "FAILURE");
    EXPECT_EQ(e->username, "operator");
    EXPECT_EQ(e->role,     "OPERATOR");
    EXPECT_TRUE(detailsContain(*e, "alice"));
}

TEST_F(UsersPresenterTest, AuditUpdateRecordsAfterImage) {
    loginAs(adminId_);
    ASSERT_EQ(presenter_->update(opId_, Role::Maintenance,
                                 "Promoted Operator", true),
              UsersStatus::Ok);

    const auto e = lastAction(audit_, "UPDATE");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->result,   "SUCCESS");
    EXPECT_TRUE(detailsContain(*e, std::format("id={}", opId_)));
    EXPECT_TRUE(detailsContain(*e, "role=MAINTENANCE"));
    EXPECT_TRUE(detailsContain(*e, "enabled=true"));
}

TEST_F(UsersPresenterTest, AuditDeleteRecordsTargetSnapshot) {
    loginAs(adminId_);
    ASSERT_EQ(presenter_->remove(maintId_), UsersStatus::Ok);

    const auto e = lastAction(audit_, "DELETE");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->result,   "SUCCESS");
    // Details captured BEFORE the row was deleted so an auditor
    // can still see what was removed.
    EXPECT_TRUE(detailsContain(*e, std::format("id={}", maintId_)));
    EXPECT_TRUE(detailsContain(*e, "username=maint"));
}

TEST_F(UsersPresenterTest, AuditSelfDeleteRecordsRefusal) {
    loginAs(adminId_);
    EXPECT_EQ(presenter_->remove(adminId_),
              UsersStatus::SelfMutationRefused);

    const auto e = lastAction(audit_, "DELETE");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->result, "FAILURE");
    EXPECT_TRUE(detailsContain(*e, "self-delete"));
}

TEST_F(UsersPresenterTest, AuditResetPasswordOmitsPlaintext) {
    // Defence-in-depth: the audit row must NOT contain the
    // password the admin chose. Leaking it via details would
    // turn the audit log into an attractive plaintext store.
    loginAs(adminId_);
    constexpr std::string_view kSecret = "extremely-secret-pass";
    ASSERT_EQ(presenter_->resetPassword(opId_, std::string{kSecret}),
              UsersStatus::Ok);

    const auto e = lastAction(audit_, "RESET_PASSWORD");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->result,   "SUCCESS");
    EXPECT_TRUE(detailsContain(*e, std::format("id={}", opId_)));
    EXPECT_FALSE(detailsContain(*e, kSecret))
        << "audit details must never echo the plaintext password";
}

TEST_F(UsersPresenterTest, AuditChangeOwnPasswordAttributesToSelf) {
    loginAs(opId_);
    ASSERT_EQ(presenter_->changeOwnPassword("operpass", "new-good-pass"),
              UsersStatus::Ok);

    const auto e = lastAction(audit_, "CHANGE_PASSWORD");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->username, "operator");
    EXPECT_EQ(e->role,     "OPERATOR");
    EXPECT_FALSE(detailsContain(*e, "operpass"));
    EXPECT_FALSE(detailsContain(*e, "new-good-pass"));
}

TEST_F(UsersPresenterTest, AuditWrongOldPasswordRecordsFailure) {
    // A wrong-old-password attempt is a useful security signal --
    // multiple failures from one account suggest a brute-force
    // attempt. The row must reach the log with FAILURE.
    loginAs(opId_);
    EXPECT_EQ(presenter_->changeOwnPassword("not-the-real-one",
                                            "new-good-pass"),
              UsersStatus::WrongPassword);

    const auto e = lastAction(audit_, "CHANGE_PASSWORD");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->result, "FAILURE");
    EXPECT_TRUE(detailsContain(*e, "wrong"));
}

TEST_F(UsersPresenterTest, AuditChangeAvatarRecordsMimeAndSize) {
    loginAs(opId_);
    const std::vector<std::uint8_t> bytes(96U, 0xAA);
    ASSERT_EQ(presenter_->setOwnAvatar(bytes, "image/png"),
              UsersStatus::Ok);

    const auto e = lastAction(audit_, "CHANGE_AVATAR");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->result,   "SUCCESS");
    EXPECT_TRUE(detailsContain(*e, "mime=image/png"));
    EXPECT_TRUE(detailsContain(*e, "bytes=96"));
}

TEST_F(UsersPresenterTest, AuditChangeAvatarRejectionRecordsFailure) {
    loginAs(opId_);
    const std::vector<std::uint8_t> bytes(32U, 0xAA);
    EXPECT_EQ(presenter_->setOwnAvatar(bytes, "image/gif"),
              UsersStatus::ValidationFailed);

    const auto e = lastAction(audit_, "CHANGE_AVATAR");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->result, "FAILURE");
    EXPECT_TRUE(detailsContain(*e, "image/gif"));
}

TEST_F(UsersPresenterTest, AuditClearAvatarRecordsIdentity) {
    loginAs(opId_);
    const std::vector<std::uint8_t> bytes(32U, 0xAA);
    ASSERT_EQ(presenter_->setOwnAvatar(bytes, "image/png"),
              UsersStatus::Ok);
    ASSERT_EQ(presenter_->clearOwnAvatar(), UsersStatus::Ok);

    const auto e = lastAction(audit_, "CLEAR_AVATAR");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->category, "USER");
    EXPECT_EQ(e->result,   "SUCCESS");
    EXPECT_TRUE(detailsContain(*e, std::format("id={}", opId_)));
    EXPECT_TRUE(detailsContain(*e, "username=operator"));
}

TEST_F(UsersPresenterTest, EveryUserCategoryEventCarriesNonEmptyDetails) {
    // Cross-cutting invariant: no USER-category row should ship with
    // an empty details column -- it would be useless to an auditor
    // walking the log six months from now.
    loginAs(adminId_);
    presenter_->create("alice", "alicepass", Role::Operator);
    presenter_->update(opId_, Role::Maintenance, "Op", true);
    presenter_->resetPassword(opId_, "another-pass-1");
    presenter_->remove(opId_);

    int checked = 0;
    for (const auto& e : audit_.snapshot()) {
        if (e.category != "USER") continue;
        EXPECT_FALSE(e.details.empty())
            << "USER " << e.action << "/" << e.result
            << " row left details empty";
        ++checked;
    }
    EXPECT_GT(checked, 0) << "no USER rows seen -- test misconfigured";
}
