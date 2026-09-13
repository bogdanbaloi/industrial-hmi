#pragma once

#include <iosfwd>

namespace app::presenter {
class AlertCenter;
}
namespace app::historian {
class HistoryReader;
}
namespace app::model {
class ProductionModel;
}

namespace app::mcp {

/// Reads newline-delimited JSON-RPC requests from an input stream, dispatches
/// each through the MCP protocol handlers, and writes a JSON-RPC response line
/// per request. Single-threaded and synchronous: one request is fully handled
/// before the next is read. The stream parameters make the loop testable with
/// string streams instead of real stdio.
class McpServer {
public:
    McpServer(const presenter::AlertCenter& alerts,
              historian::HistoryReader& reader,
              const model::ProductionModel& production);

    /// Pump requests until end of input. Returns 0 on a clean EOF.
    int run(std::istream& input, std::ostream& output);

    /// Convenience overload over std::cin / std::cout for the real binary.
    int run();

private:
    const presenter::AlertCenter& alerts_;
    historian::HistoryReader&      reader_;
    const model::ProductionModel&  production_;
};

}  // namespace app::mcp
