// [utest->req~arch-018~1]
// [utest->req~arch-019~1]
// Covers REQ-ARCH-018 (MCP server) at the protocol/tool level: the initialize
// handshake, tools/list enumeration, both read-only tools over an AlertCenter
// and a stub HistoryReader, argument validation and the boundary error mapping.
// Also covers REQ-ARCH-019 dispatch: the write tool is absent from tools/list
// and unreachable (MethodNotFound) when writes are disabled, routes to the
// presenter when enabled, and refuses an under-privileged agent role.
// Pure logic over fakes -- no stdio loop, no GUI.

#include "src/mcp/McpProtocol.h"
#include "src/mcp/McpError.h"
#include "src/mcp/tools/AlarmsSnapshotTool.h"
#include "src/mcp/tools/EquipmentCommandTool.h"
#include "src/mcp/tools/HistorianQueryTool.h"

#include "src/auth/AuditEvent.h"
#include "src/auth/AuditLogger.h"
#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/auth/User.h"
#include "src/presenter/AlertCenter.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/historian/HistoryReader.h"
#include "src/historian/HistoryRecord.h"

#include "mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using app::historian::FieldKind;
using app::historian::HistoryReader;
using app::historian::HistoryRecord;
using app::historian::QueryRange;
using app::presenter::AlertCenter;
using app::presenter::AlertSeverity;
using app::presenter::AlertViewModel;

// Minimal reader that returns staged rows and records the last query, so the
// tool tests assert on both the emitted JSON and the call it made.
class StubHistoryReader : public HistoryReader {
public:
    std::vector<HistoryRecord> query(FieldKind field, std::uint32_t entityId,
                                     QueryRange range) override {
        lastField  = field;
        lastEntity = entityId;
        lastRange  = range;
        return rows;
    }
    [[nodiscard]] std::size_t totalSamples() const override { return rows.size(); }

    std::vector<HistoryRecord> rows;
    FieldKind                  lastField{FieldKind::QualityPassRate};
    std::uint32_t              lastEntity{0};
    QueryRange                 lastRange{};
};

AlertViewModel makeAlert(const std::string& key, AlertSeverity severity,
                         const std::string& title) {
    AlertViewModel alert;
    alert.key      = key;
    alert.severity = severity;
    alert.title    = title;
    return alert;
}

// Counts audit rows without a database, so the protocol dispatch tests stay
// pure logic (the persisted-audit path is covered by EquipmentCommandToolTest).
class CountingAuditLogger : public app::auth::AuditLogger {
public:
    bool record(const app::auth::AuditEvent& event) override {
        if (event.result == app::auth::result::kFailure) {
            ++failures;
        }
        return true;
    }
    [[nodiscard]] std::vector<app::auth::AuditEvent>
    query(const app::auth::AuditQuery&) override {
        return {};
    }
    [[nodiscard]] std::size_t totalEvents() const override { return 0; }
    int failures{0};
};

// Owns a real DashboardPresenter over a nice mock model plus a logged-in agent
// session, wired exactly as the composition root does: the SAME session is set
// on the presenter (setAudit) and handed to the dispatch. The read-only tool
// tests ignore it and pass writeEnabled=false.
struct WriteContext {
    explicit WriteContext(app::auth::Role role) : presenter(model) {
        app::auth::User agent;
        agent.username = "mcp-agent";
        agent.role     = role;
        session.setUser(agent);
        presenter.setAudit(audit, session);
    }
    ::testing::NiceMock<app::test::MockProductionModel> model;
    CountingAuditLogger                                 audit;
    app::DashboardPresenter                             presenter;
    app::auth::Session                                  session;
};

}  // namespace

using namespace app::mcp;

TEST(McpProtocolTest, InitializeReturnsProtocolVersionAndCapabilities) {
    const auto result = handleInitialize(nlohmann::json::object());
    EXPECT_EQ(result.at("protocolVersion"), kMcpProtocolVersion);
    EXPECT_TRUE(result.at("capabilities").contains("tools"));
    EXPECT_EQ(result.at("serverInfo").at("name"), kServerName);
}

TEST(McpProtocolTest, ToolsListOmitsWriteToolWhenWriteDisabled) {
    const auto result = handleToolsList(/*writeEnabled=*/false);
    const auto& tools = result.at("tools");
    ASSERT_EQ(tools.size(), 2U);

    std::vector<std::string> names{tools[0].at("name").get<std::string>(),
                                   tools[1].at("name").get<std::string>()};
    EXPECT_NE(std::find(names.begin(), names.end(), kAlarmsSnapshotTool),
              names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), kHistorianQueryTool),
              names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), kEquipmentCommandTool),
              names.end());
}

TEST(McpProtocolTest, ToolsListIncludesWriteToolWhenWriteEnabled) {
    const auto result = handleToolsList(/*writeEnabled=*/true);
    const auto& tools = result.at("tools");
    ASSERT_EQ(tools.size(), 3U);

    std::vector<std::string> names;
    for (const auto& tool : tools) {
        names.push_back(tool.at("name").get<std::string>());
    }
    EXPECT_NE(std::find(names.begin(), names.end(), kEquipmentCommandTool),
              names.end());
}

TEST(McpProtocolTest, AlarmsSnapshotToolReturnsCurrentAlarmsAsJson) {
    AlertCenter alerts;
    alerts.raise(makeAlert("equip-1", AlertSeverity::Critical, "Equipment 1 error"));

    const auto array = runAlarmsSnapshot(alerts);
    ASSERT_TRUE(array.is_array());
    ASSERT_EQ(array.size(), 1U);
    EXPECT_EQ(array[0].at("key"), "equip-1");
    EXPECT_EQ(array[0].at("severity"), "critical");
    EXPECT_EQ(array[0].at("title"), "Equipment 1 error");
}

TEST(McpProtocolTest, AlarmsSnapshotToolReturnsEmptyArrayWhenNoAlarms) {
    AlertCenter alerts;
    const auto array = runAlarmsSnapshot(alerts);
    EXPECT_TRUE(array.is_array());
    EXPECT_TRUE(array.empty());
}

TEST(McpProtocolTest, HistorianQueryToolReturnsRecordsInRange) {
    StubHistoryReader reader;
    reader.rows.push_back(HistoryRecord{.timestampMs = 1000,
                                        .field    = FieldKind::QualityPassRate,
                                        .entityId = 2U,
                                        .value    = 97.5F});

    const nlohmann::json args = {
        {"field", "quality"}, {"entityId", 2}, {"fromMs", 0}, {"toMs", 5000}};
    auto parsed = parseHistorianArgs(args);
    ASSERT_TRUE(parsed.isOk());

    const auto array = runHistorianQuery(reader, parsed.unwrap());
    ASSERT_EQ(array.size(), 1U);
    EXPECT_EQ(array[0].at("field"), "quality");
    EXPECT_EQ(array[0].at("entityId"), 2U);
    EXPECT_EQ(reader.lastField, FieldKind::QualityPassRate);
    EXPECT_EQ(reader.lastEntity, 2U);
}

TEST(McpProtocolTest, HistorianQueryToolClampsLimitToDefaultCap) {
    const nlohmann::json args = {{"field", "supply"},
                                 {"limit", QueryRange::kDefaultLimit + 1}};
    auto parsed = parseHistorianArgs(args);
    ASSERT_TRUE(parsed.isOk());
    EXPECT_EQ(parsed.unwrap().range.limit, QueryRange::kDefaultLimit);
}

TEST(McpProtocolTest, HistorianQueryToolRejectsUnknownFieldWithJsonRpcError) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    WriteContext      ctx{app::auth::Role::Operator};
    const nlohmann::json params = {{"name", kHistorianQueryTool},
                                   {"arguments", {{"field", "bogus"}}}};

    auto result = handleToolsCall(params, alerts, reader, ctx.presenter,
                                  ctx.session, /*writeEnabled=*/false);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::InvalidParams);
    EXPECT_EQ(jsonRpcCode(result.error()), kJsonRpcInvalidParams);
}

TEST(McpProtocolTest, ToolsCallWithUnknownToolNameReturnsMethodNotFound) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    WriteContext      ctx{app::auth::Role::Operator};
    const nlohmann::json params = {{"name", "no_such_tool"}};

    auto result = handleToolsCall(params, alerts, reader, ctx.presenter,
                                  ctx.session, /*writeEnabled=*/false);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::MethodNotFound);
}

TEST(McpProtocolTest, EquipmentCommandUnreachableWhenWriteDisabled) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    WriteContext      ctx{app::auth::Role::Operator};
    // Even with valid arguments, a read-only deployment must treat the write
    // tool as if it does not exist (ADR-0024): no ungated write is reachable.
    EXPECT_CALL(ctx.model, startProduction()).Times(0);
    const nlohmann::json params = {{"name", kEquipmentCommandTool},
                                   {"arguments", {{"command", "start"}}}};

    auto result = handleToolsCall(params, alerts, reader, ctx.presenter,
                                  ctx.session, /*writeEnabled=*/false);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::MethodNotFound);
}

TEST(McpProtocolTest, EquipmentCommandRoutesToPresenterWhenWriteEnabled) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    WriteContext      ctx{app::auth::Role::Operator};
    EXPECT_CALL(ctx.model, startProduction()).Times(1);
    const nlohmann::json params = {{"name", kEquipmentCommandTool},
                                   {"arguments", {{"command", "start"}}}};

    auto result = handleToolsCall(params, alerts, reader, ctx.presenter,
                                  ctx.session, /*writeEnabled=*/true);
    ASSERT_TRUE(result.isOk());
    // The MCP content envelope wraps the tool's acknowledgement payload.
    EXPECT_TRUE(result.unwrap().at("content").is_array());
}

TEST(McpProtocolTest, EquipmentCommandUnauthorizedRoleWhenWriteEnabled) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    WriteContext      ctx{app::auth::Role::Operator};
    // Reset needs Maintenance; an Operator agent is refused with a structured
    // error and never reaches the model.
    EXPECT_CALL(ctx.model, resetSystem()).Times(0);
    const nlohmann::json params = {{"name", kEquipmentCommandTool},
                                   {"arguments", {{"command", "reset"}}}};

    auto result = handleToolsCall(params, alerts, reader, ctx.presenter,
                                  ctx.session, /*writeEnabled=*/true);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::Unauthorized);
    // The refused attempt is audited through the presenter's human path.
    EXPECT_EQ(ctx.audit.failures, 1);
}
