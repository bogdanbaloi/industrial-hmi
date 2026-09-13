// [utest->req~arch-018~1]
// [utest->req~arch-020~1]
// Covers REQ-ARCH-018 (MCP server) at the protocol/tool level: the initialize
// handshake, tools/list enumeration, the read-only tools over an AlertCenter,
// a stub HistoryReader and a mock ProductionModel, argument validation and the
// boundary error mapping. Also covers REQ-ARCH-020 (production_metrics): the
// descriptor is listed, the throughput/OEE snapshot, the derived minutesPerUnit
// and its omission at non-positive throughput, and the tools/call routing.
// Pure logic over fakes -- no stdio loop, no GUI.

#include "src/mcp/McpProtocol.h"
#include "src/mcp/McpError.h"
#include "src/mcp/tools/AlarmsSnapshotTool.h"
#include "src/mcp/tools/HistorianQueryTool.h"
#include "src/mcp/tools/ProductionMetricsTool.h"

#include "src/presenter/AlertCenter.h"
#include "src/historian/HistoryReader.h"
#include "src/historian/HistoryRecord.h"
#include "src/model/ProductionTypes.h"
#include "tests/mocks/MockProductionModel.h"

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
using app::model::OeeMetrics;
using app::model::WorkUnit;
using app::presenter::AlertCenter;
using app::presenter::AlertSeverity;
using app::presenter::AlertViewModel;
using app::test::MockProductionModel;
using testing::NiceMock;
using testing::Return;

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

// Stub a mock ProductionModel so its throughput and OEE queries return fixed
// values, letting the production_metrics tests assert on the derived JSON.
void stubProduction(NiceMock<MockProductionModel>& production,
                    double throughputUph, float oeePct) {
    WorkUnit unit;
    unit.throughputUnitsPerHour = throughputUph;
    OeeMetrics oee;
    oee.oeePct = oeePct;
    ON_CALL(production, getWorkUnit()).WillByDefault(Return(unit));
    ON_CALL(production, oeeSnapshot()).WillByDefault(Return(oee));
}

}  // namespace

using namespace app::mcp;

TEST(McpProtocolTest, InitializeReturnsProtocolVersionAndCapabilities) {
    const auto result = handleInitialize(nlohmann::json::object());
    EXPECT_EQ(result.at("protocolVersion"), kMcpProtocolVersion);
    EXPECT_TRUE(result.at("capabilities").contains("tools"));
    EXPECT_EQ(result.at("serverInfo").at("name"), kServerName);
}

TEST(McpProtocolTest, ToolsListEnumeratesAllReadOnlyTools) {
    const auto result = handleToolsList();
    const auto& tools = result.at("tools");
    ASSERT_EQ(tools.size(), 3U);

    std::vector<std::string> names;
    for (const auto& tool : tools) {
        names.push_back(tool.at("name").get<std::string>());
    }
    EXPECT_NE(std::find(names.begin(), names.end(), kAlarmsSnapshotTool),
              names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), kHistorianQueryTool),
              names.end());
}

TEST(McpProtocolTest, ToolsListIncludesProductionMetrics) {
    const auto result = handleToolsList();
    const auto& tools = result.at("tools");

    std::vector<std::string> names;
    for (const auto& tool : tools) {
        names.push_back(tool.at("name").get<std::string>());
    }
    EXPECT_NE(std::find(names.begin(), names.end(), kProductionMetricsTool),
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
    NiceMock<MockProductionModel> production;
    const nlohmann::json params = {{"name", kHistorianQueryTool},
                                   {"arguments", {{"field", "bogus"}}}};

    auto result = handleToolsCall(params, alerts, reader, production);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::InvalidParams);
    EXPECT_EQ(jsonRpcCode(result.error()), kJsonRpcInvalidParams);
}

TEST(McpProtocolTest, ToolsCallWithUnknownToolNameReturnsMethodNotFound) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    NiceMock<MockProductionModel> production;
    const nlohmann::json params = {{"name", "no_such_tool"}};

    auto result = handleToolsCall(params, alerts, reader, production);
    ASSERT_TRUE(result.isErr());
    EXPECT_EQ(result.error(), McpErrorCode::MethodNotFound);
}

TEST(McpProtocolTest, ProductionMetricsReturnsThroughputAndOee) {
    NiceMock<MockProductionModel> production;
    stubProduction(production, 30.0, 85.0F);

    const auto metrics = runProductionMetrics(production);
    EXPECT_DOUBLE_EQ(metrics.at("throughputUph").get<double>(), 30.0);
    EXPECT_FLOAT_EQ(metrics.at("oeePct").get<float>(), 85.0F);
    ASSERT_TRUE(metrics.contains("minutesPerUnit"));
    EXPECT_DOUBLE_EQ(metrics.at("minutesPerUnit").get<double>(), 2.0);
}

TEST(McpProtocolTest, ProductionMetricsOmitsMinutesPerUnitWhenThroughputIsZero) {
    NiceMock<MockProductionModel> production;
    stubProduction(production, 0.0, 40.0F);

    const auto metrics = runProductionMetrics(production);
    EXPECT_DOUBLE_EQ(metrics.at("throughputUph").get<double>(), 0.0);
    EXPECT_FALSE(metrics.contains("minutesPerUnit"));
}

TEST(McpProtocolTest, ProductionMetricsOmitsMinutesPerUnitWhenThroughputIsNegative) {
    NiceMock<MockProductionModel> production;
    stubProduction(production, -5.0, 40.0F);

    const auto metrics = runProductionMetrics(production);
    EXPECT_FALSE(metrics.contains("minutesPerUnit"));
}

TEST(McpProtocolTest, ToolsCallRoutesProductionMetrics) {
    StubHistoryReader reader;
    AlertCenter       alerts;
    NiceMock<MockProductionModel> production;
    stubProduction(production, 30.0, 85.0F);
    const nlohmann::json params = {{"name", kProductionMetricsTool}};

    auto result = handleToolsCall(params, alerts, reader, production);
    ASSERT_TRUE(result.isOk());
    // The tool payload is wrapped in the MCP content envelope as pretty JSON.
    const auto& content = result.unwrap().at("content");
    ASSERT_TRUE(content.is_array());
    ASSERT_EQ(content.size(), 1U);
    const auto payload =
        nlohmann::json::parse(content[0].at("text").get<std::string>());
    EXPECT_DOUBLE_EQ(payload.at("throughputUph").get<double>(), 30.0);
    EXPECT_DOUBLE_EQ(payload.at("minutesPerUnit").get<double>(), 2.0);
}
