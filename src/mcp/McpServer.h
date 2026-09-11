#pragma once

#include <iosfwd>

namespace app::presenter {
class AlertCenter;
}
namespace app::historian {
class HistoryReader;
}
namespace app {
class DashboardPresenter;
}
namespace app::auth {
class Session;
}

namespace app::mcp {

/// Reads newline-delimited JSON-RPC requests from an input stream, dispatches
/// each through the MCP protocol handlers, and writes a JSON-RPC response line
/// per request. Single-threaded and synchronous: one request is fully handled
/// before the next is read. The stream parameters make the loop testable with
/// string streams instead of real stdio.
///
/// The presenter + agent session + `writeEnabled` flag carry the state-changing
/// `equipment_command` tool (ADR-0024). When `writeEnabled` is false the write
/// tool is neither listed nor callable, so the presenter/session are inert.
class McpServer {
public:
    McpServer(const presenter::AlertCenter& alerts,
              historian::HistoryReader& reader,
              DashboardPresenter& presenter,
              const auth::Session& session,
              bool writeEnabled);

    /// Pump requests until end of input. Returns 0 on a clean EOF.
    int run(std::istream& input, std::ostream& output);

    /// Convenience overload over std::cin / std::cout for the real binary.
    int run();

private:
    const presenter::AlertCenter& alerts_;
    historian::HistoryReader&     reader_;
    DashboardPresenter&           presenter_;
    const auth::Session&          session_;
    bool                          writeEnabled_;
};

}  // namespace app::mcp
