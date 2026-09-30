// [utest->req~arch-019~1]
// Covers REQ-ARCH-019 (MCP state-changing tool) at the tool level: the
// descriptor, argument parsing, and -- the interesting part -- the explicit
// authorization pre-check that closes DashboardPresenter::checkRole's
// null-session pass-through and turns a void role refusal into a structured
// JSON-RPC error, while reusing the same operator handlers + audit path a human
// button drives (ADR-0024). Real Session + in-memory SqliteAuditLogger, a
// gmock ProductionModel behind a real DashboardPresenter. No stdio, no GUI.

#include "src/mcp/tools/EquipmentCommandTool.h"
#include "src/mcp/McpError.h"

#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/auth/SqliteAuditLogger.h"
#include "src/auth/User.h"
#include "src/presenter/DashboardPresenter.h"

#include "mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace {

using app::DashboardPresenter;
using app::auth::AuditQuery;
using app::auth::Role;
using app::auth::Session;
using app::auth::SqliteAuditLogger;
using app::auth::User;
using app::mcp::EquipmentCommand;
using app::mcp::McpErrorCode;
using app::mcp::parseEquipmentCommandArgs;
using app::mcp::runEquipmentCommand;
using app::test::MockProductionModel;
using ::testing::NiceMock;

// Fixture wiring the tool the way the composition root does: the SAME synthetic
// agent session is set on the presenter (setAudit) and handed to the tool, over
// a real in-memory audit logger, so a refusal audits through the human path.
class EquipmentCommandToolTest : public ::testing::Test {
protected:
    void SetUp() override {
        audit_ = std::make_unique<SqliteAuditLogger>(
            SqliteAuditLogger::Config{.dbPath = ":memory:"});
        ASSERT_TRUE(audit_->initialize());
        presenter_ = std::make_unique<DashboardPresenter>(model_);
    }

    // Wire the audit + agent session into the presenter (the enabled-writes
    // deployment) and log the agent in with the given role.
    void wireAgent(Role role) {
        presenter_->setAudit(*audit_, session_);
        User agent;
        agent.username = "mcp-agent";
        agent.role     = role;
        session_.setUser(agent);
    }

    [[nodiscard]] std::size_t auditRows(const std::string& result) {
        AuditQuery query;
        query.result = result;
        return audit_->query(query).size();
    }

    NiceMock<MockProductionModel>      model_;
    Session                            session_;
    std::unique_ptr<SqliteAuditLogger> audit_;
    std::unique_ptr<DashboardPresenter> presenter_;
};

}  // namespace

using app::mcp::equipmentCommandDescriptor;
using app::mcp::kEquipmentCommandTool;

TEST(EquipmentCommandDescriptorTest, DescriptorListsThreeCommands) {
    const auto descriptor = equipmentCommandDescriptor();
    EXPECT_EQ(descriptor.at("name"), kEquipmentCommandTool);

    const auto& names = descriptor.at("inputSchema")
                            .at("properties")
                            .at("command")
                            .at("enum");
    ASSERT_TRUE(names.is_array());
    ASSERT_EQ(names.size(), 3U);
    EXPECT_EQ(names[0], "start");
    EXPECT_EQ(names[1], "stop");
    EXPECT_EQ(names[2], "reset");
}

TEST(EquipmentCommandParseTest, UnknownCommandNameIsInvalidParams) {
    auto parsed = parseEquipmentCommandArgs({{"command", "frobnicate"}});
    ASSERT_TRUE(parsed.isErr());
    EXPECT_EQ(parsed.error(), McpErrorCode::InvalidParams);
}

TEST(EquipmentCommandParseTest, NonObjectArgsIsInvalidParams) {
    auto parsed = parseEquipmentCommandArgs(nlohmann::json::array());
    ASSERT_TRUE(parsed.isErr());
    EXPECT_EQ(parsed.error(), McpErrorCode::InvalidParams);
}

TEST(EquipmentCommandParseTest, KnownNamesParseToTheRightCommand) {
    EXPECT_EQ(parseEquipmentCommandArgs({{"command", "start"}}).unwrap(),
              EquipmentCommand::Start);
    EXPECT_EQ(parseEquipmentCommandArgs({{"command", "stop"}}).unwrap(),
              EquipmentCommand::Stop);
    EXPECT_EQ(parseEquipmentCommandArgs({{"command", "reset"}}).unwrap(),
              EquipmentCommand::ResetRestart);
}

TEST_F(EquipmentCommandToolTest, AuthorizedOperatorStartCallsPresenter) {
    wireAgent(Role::Operator);
    EXPECT_CALL(model_, startProduction()).Times(1);

    auto result = runEquipmentCommand(*presenter_, session_,
                                      EquipmentCommand::Start);
    ASSERT_TRUE(result.isOk());
    const auto payload = result.unwrap();
    EXPECT_EQ(payload.at("command"), "start");
    EXPECT_EQ(payload.at("status"), "accepted");
    EXPECT_EQ(auditRows("SUCCESS"), 1U);
}

TEST_F(EquipmentCommandToolTest, AuthorizedOperatorResetIsUnauthorized) {
    wireAgent(Role::Operator);
    // Reset needs Maintenance; the presenter must never reach the model.
    EXPECT_CALL(model_, resetSystem()).Times(0);

    auto result = runEquipmentCommand(*presenter_, session_,
                                      EquipmentCommand::ResetRestart);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::Unauthorized);
    // The refusal is audited through the presenter's human path, not silently
    // dropped: exactly one FAILURE row, and no SUCCESS row.
    EXPECT_EQ(auditRows("FAILURE"), 1U);
    EXPECT_EQ(auditRows("SUCCESS"), 0U);
}

TEST_F(EquipmentCommandToolTest, MaintenanceRoleResetSucceeds) {
    wireAgent(Role::Maintenance);
    EXPECT_CALL(model_, resetSystem()).Times(1);

    auto result = runEquipmentCommand(*presenter_, session_,
                                      EquipmentCommand::ResetRestart);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.unwrap().at("command"), "reset");
    EXPECT_EQ(auditRows("SUCCESS"), 1U);
}

// The case the test below does NOT reach, reported by the pfa workstream on
// 2026-09-30 and confirmed against main at 148fab0.
//
// `NoSessionWiredIsInternalRefusal` passes because the TOOL's session carries
// no user. The real deployment fails differently: the agent session is fully
// populated, so `currentUser()` hands back `mcp-agent`, while the PRESENTER
// never received a session at all. McpInitRoot only calls `setAudit`, which
// wires audit AND session together, inside `if (auditLogger_->initialize())`.
// When the audit database cannot be opened the intent was to lose the audit
// ROW. What was also lost was the role check, because
// `DashboardPresenter::checkRole` passes a null session through by design.
//
// The result on main: an OPERATOR agent asking for `reset` has the reset
// PERFORMED, is answered `Unauthorized`, and leaves no audit row. The action
// happens, the reply denies it, nothing records it.
TEST_F(EquipmentCommandToolTest, UnwiredPresenterStillRefusesAnUnauthorisedWrite) {
    // Exactly what McpInitRoot does when the audit database fails to open:
    // the agent is logged in, `setAudit` is never called.
    User agent;
    agent.username = "mcp-agent";
    agent.role     = Role::Operator;
    session_.setUser(agent);
    ASSERT_TRUE(session_.currentUser().has_value());

    // Reset needs Maintenance. Losing the audit sink must not lose the gate.
    EXPECT_CALL(model_, resetSystem()).Times(0);

    auto result = runEquipmentCommand(*presenter_, session_,
                                      EquipmentCommand::ResetRestart);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::Unauthorized);
}

// The other half of the same guard: refusing an unwired presenter must not
// become a refusal of everything. A permitted action still has to run, because
// the documented behaviour of a failed audit open is "writes without a
// persisted audit row" rather than "no writes".
TEST_F(EquipmentCommandToolTest, SessionWiredWithoutAuditStillPerformsAllowedWork) {
    presenter_->setSession(session_);  // session only, no audit sink
    User agent;
    agent.username = "mcp-agent";
    agent.role     = Role::Operator;
    session_.setUser(agent);

    EXPECT_CALL(model_, startProduction()).Times(1);

    auto result = runEquipmentCommand(*presenter_, session_,
                                      EquipmentCommand::Start);
    ASSERT_TRUE(result.isOk());
    EXPECT_EQ(result.unwrap().at("status"), "accepted");
}

// The security catch (ADR-0024): if the agent session carries no authenticated
// user -- an internal wiring fault, e.g. setAudit never called so the
// presenter's own gate would pass a null session through and run an ungated
// write -- the tool must refuse BEFORE touching the presenter, never a silent
// no-op or a false success.
TEST_F(EquipmentCommandToolTest, NoSessionWiredIsInternalRefusal) {
    // Deliberately do NOT wireAgent(): the presenter has no session/audit and
    // the session has no user.
    EXPECT_CALL(model_, startProduction()).Times(0);
    ASSERT_FALSE(session_.currentUser().has_value());

    auto result = runEquipmentCommand(*presenter_, session_,
                                      EquipmentCommand::Start);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::Unauthorized);
}
