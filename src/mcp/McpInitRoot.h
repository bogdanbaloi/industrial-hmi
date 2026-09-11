#pragma once

#include <iosfwd>
#include <memory>
#include <thread>

namespace app::core {
class Bootstrap;
}
namespace app::presenter {
class AlertCenter;
}
namespace app {
class DashboardPresenter;
}
namespace app::historian {
class SqliteHistoryStore;
class HistorianBridge;
}
namespace app::auth {
class Session;
class SqliteAuditLogger;
}

namespace app::mcp {

/// Composition root for the MCP server binary, a structural sibling of
/// `InitConsole`/`QtInitRoot`: it builds the same `SimulatedModel`,
/// `AlertCenter`, `DashboardPresenter` and historian the human frontends use,
/// drives the simulation from a background tick so alarms and history evolve,
/// then serves MCP over stdio until end of input. There is no View toolkit.
class McpInitRoot {
public:
    explicit McpInitRoot(core::Bootstrap& bootstrap);
    ~McpInitRoot();

    McpInitRoot(const McpInitRoot&)            = delete;
    McpInitRoot& operator=(const McpInitRoot&) = delete;
    McpInitRoot(McpInitRoot&&)                 = delete;
    McpInitRoot& operator=(McpInitRoot&&)      = delete;

    /// Build the core, start the tick, and serve MCP over std::cin and the
    /// given output stream (the caller binds it to the real stdout, keeping
    /// std::cout free for logging). Returns the process exit code.
    int run(std::ostream& output);

private:
    core::Bootstrap& bootstrap_;

    std::unique_ptr<presenter::AlertCenter>        alertCenter_;
    std::unique_ptr<DashboardPresenter>            dashboardPresenter_;
    std::unique_ptr<historian::SqliteHistoryStore> historyStore_;
    std::unique_ptr<historian::HistorianBridge>    historianBridge_;

    // Synthetic agent identity + audit sink for the state-changing write tool
    // (ADR-0024). Built only when mcp.write_enabled is true; left null for a
    // read-only deployment.
    std::unique_ptr<auth::Session>            agentSession_;
    std::unique_ptr<auth::SqliteAuditLogger>  auditLogger_;

    std::jthread                                   ticker_;
};

}  // namespace app::mcp
