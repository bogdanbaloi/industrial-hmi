// [utest->req~arch-019~1]
// Covers the MCP stdio request/response loop end to end over string streams --
// the pump that ADR-0023 shipped but REQ-ARCH-018 never unit-tested (it stopped
// at the protocol handlers). Exercises: one response line per request, the
// initialize / tools-list / tools-call happy paths, a malformed-JSON parse
// error with a null id, a notification (no id) producing no response, an unknown
// method, and the write tool surfacing over stdio only when writes are enabled
// (ADR-0024). No real stdio, no GUI.

#include "src/mcp/McpServer.h"
#include "src/mcp/McpError.h"
#include "src/mcp/McpProtocol.h"
#include "src/mcp/tools/EquipmentCommandTool.h"

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

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

using app::DashboardPresenter;
using app::historian::FieldKind;
using app::historian::HistoryReader;
using app::historian::HistoryRecord;
using app::historian::QueryRange;
using app::mcp::McpServer;
using app::mcp::kEquipmentCommandTool;
using app::presenter::AlertCenter;

// Inert reader: the stdio loop tests do not care about historian rows.
class EmptyHistoryReader : public HistoryReader {
public:
    std::vector<HistoryRecord> query(FieldKind /*field*/,
                                     std::uint32_t /*entityId*/,
                                     QueryRange /*range*/) override {
        return {};
    }
    [[nodiscard]] std::size_t totalSamples() const override { return 0; }
};

// Everything a McpServer needs, wired for a logged-in agent so the write path
// can be driven when a test enables it.
struct ServerFixture {
    explicit ServerFixture(bool writeEnabled,
                           app::auth::Role role = app::auth::Role::Operator)
        : presenter(model), server(alerts, reader, presenter, session,
                                   writeEnabled) {
        app::auth::User agent;
        agent.username = "mcp-agent";
        agent.role     = role;
        session.setUser(agent);
    }
    AlertCenter                                         alerts;
    EmptyHistoryReader                                  reader;
    ::testing::NiceMock<app::test::MockProductionModel> model;
    DashboardPresenter                                  presenter;
    app::auth::Session                                  session;
    McpServer                                           server;
};

// Split the server's output stream into the parsed JSON of each response line.
std::vector<nlohmann::json> responseLines(const std::string& raw) {
    std::vector<nlohmann::json> out;
    std::istringstream stream(raw);
    std::string        line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            out.push_back(nlohmann::json::parse(line));
        }
    }
    return out;
}

}  // namespace

TEST(McpServerStdioTest, InitializeRequestGetsOneResponseLine) {
    ServerFixture fx{/*writeEnabled=*/false};
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
        "\n");
    std::ostringstream output;

    EXPECT_EQ(fx.server.run(input, output), 0);

    const auto responses = responseLines(output.str());
    ASSERT_EQ(responses.size(), 1U);
    EXPECT_EQ(responses[0].at("jsonrpc"), "2.0");
    EXPECT_EQ(responses[0].at("id"), 1);
    EXPECT_TRUE(responses[0].at("result").contains("protocolVersion"));
}

TEST(McpServerStdioTest, MalformedJsonGetsParseErrorWithNullId) {
    ServerFixture fx{/*writeEnabled=*/false};
    std::istringstream input("this is not json\n");
    std::ostringstream output;

    fx.server.run(input, output);

    const auto responses = responseLines(output.str());
    ASSERT_EQ(responses.size(), 1U);
    EXPECT_TRUE(responses[0].at("id").is_null());
    EXPECT_EQ(responses[0].at("error").at("code"),
              app::mcp::kJsonRpcParseError);
}

TEST(McpServerStdioTest, NotificationWithoutIdProducesNoResponse) {
    ServerFixture fx{/*writeEnabled=*/false};
    // A JSON-RPC notification has no id; the spec forbids a response.
    std::istringstream input(
        R"({"jsonrpc":"2.0","method":"initialize","params":{}})"
        "\n");
    std::ostringstream output;

    fx.server.run(input, output);
    EXPECT_TRUE(output.str().empty());
}

TEST(McpServerStdioTest, UnknownMethodGetsMethodNotFound) {
    ServerFixture fx{/*writeEnabled=*/false};
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":7,"method":"no_such_method"})"
        "\n");
    std::ostringstream output;

    fx.server.run(input, output);

    const auto responses = responseLines(output.str());
    ASSERT_EQ(responses.size(), 1U);
    EXPECT_EQ(responses[0].at("error").at("code"),
              app::mcp::kJsonRpcMethodNotFound);
}

TEST(McpServerStdioTest, EachRequestGetsExactlyOneResponseLine) {
    ServerFixture fx{/*writeEnabled=*/false};
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
        "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
        "\n");
    std::ostringstream output;

    fx.server.run(input, output);

    const auto responses = responseLines(output.str());
    ASSERT_EQ(responses.size(), 2U);
    EXPECT_EQ(responses[0].at("id"), 1);
    EXPECT_EQ(responses[1].at("id"), 2);
    // The read-only deployment lists exactly the two read tools.
    EXPECT_EQ(responses[1].at("result").at("tools").size(), 2U);
}

TEST(McpServerStdioTest, ToolsListSurfacesWriteToolWhenEnabled) {
    ServerFixture fx{/*writeEnabled=*/true};
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":9,"method":"tools/list"})"
        "\n");
    std::ostringstream output;

    fx.server.run(input, output);

    const auto responses = responseLines(output.str());
    ASSERT_EQ(responses.size(), 1U);
    const auto& tools = responses[0].at("result").at("tools");
    ASSERT_EQ(tools.size(), 3U);
    bool found = false;
    for (const auto& tool : tools) {
        found = found || tool.at("name") == kEquipmentCommandTool;
    }
    EXPECT_TRUE(found);
}

TEST(McpServerStdioTest, ToolsCallStartOverStdioDrivesPresenter) {
    ServerFixture fx{/*writeEnabled=*/true};
    EXPECT_CALL(fx.model, startProduction()).Times(1);
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call",)"
        R"("params":{"name":"equipment_command","arguments":{"command":"start"}}})"
        "\n");
    std::ostringstream output;

    fx.server.run(input, output);

    const auto responses = responseLines(output.str());
    ASSERT_EQ(responses.size(), 1U);
    EXPECT_TRUE(responses[0].contains("result"));
}
