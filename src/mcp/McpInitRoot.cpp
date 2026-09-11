// Model header first so its enumerators are parsed before any other header
// pulls in a conflicting macro (same discipline as the other init roots).
#include "src/model/SimulatedModel.h"
#include "src/presenter/DashboardPresenter.h"

#include "src/mcp/McpInitRoot.h"

#include "src/mcp/McpServer.h"
#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/auth/SqliteAuditLogger.h"
#include "src/auth/User.h"
#include "src/config/ConfigManager.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/historian/HistorianBridge.h"
#include "src/historian/SqliteHistoryStore.h"
#include "src/model/ModelContext.h"
#include "src/presenter/AlertCenter.h"

#include <string>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <ostream>
#include <stop_token>
#include <utility>

namespace app::mcp {

namespace {
// Background simulation cadence, matching the tick the console/Qt frontends use.
constexpr auto kTickPeriod = std::chrono::milliseconds{1000};

// Username stamped on the synthetic agent identity + every audit row it writes
// (ADR-0024). Not a real account: no password, no repository row, no login.
constexpr const char* kAgentUsername = "mcp-agent";
}  // namespace

McpInitRoot::McpInitRoot(core::Bootstrap& bootstrap) : bootstrap_{bootstrap} {}

McpInitRoot::~McpInitRoot() {
    // Stop the tick before anything it touches is torn down.
    ticker_.request_stop();
    if (ticker_.joinable()) {
        ticker_.join();
    }
    // Dependency order: the bridge flushes pending rows on reset, then the store
    // closes, then the presenter (which borrows the model + AlertCenter).
    historianBridge_.reset();
    historyStore_.reset();
    // The presenter borrows the audit logger + agent session via setAudit(), so
    // it must be torn down before them.
    dashboardPresenter_.reset();
    auditLogger_.reset();
    agentSession_.reset();
    alertCenter_.reset();
    app::model::SimulatedModel::instance().clearCallbacks();
    app::model::ModelContext::instance().stop();
}

int McpInitRoot::run(std::ostream& output) {
    auto& logger = bootstrap_.logger();
    logger.info("Application starting (MCP server)");

    auto& model = app::model::SimulatedModel::instance();
    model.setLogger(logger);

    alertCenter_        = std::make_unique<presenter::AlertCenter>();
    dashboardPresenter_ = std::make_unique<DashboardPresenter>(model);
    dashboardPresenter_->setAlertCenter(*alertCenter_);

    // Historian: the same SQLite archive the other frontends persist to, so the
    // agent queries real accumulated history. The store is always constructed --
    // a failed open degrades to inert (empty) reads, still a valid reader.
    auto& config = app::config::ConfigManager::instance();
    historian::SqliteHistoryStore::Config storeCfg;
    storeCfg.dbPath = config.getHistorianDbPath();
    historyStore_ =
        std::make_unique<historian::SqliteHistoryStore>(std::move(storeCfg));
    historyStore_->setLogger(logger);
    if (historyStore_->initialize()) {
        historian::HistorianBridge::Config bridgeCfg;
        bridgeCfg.maxBatchSize =
            static_cast<std::size_t>(config.getHistorianBatchSize());
        bridgeCfg.maxBatchAge =
            std::chrono::milliseconds{config.getHistorianBatchAgeMs()};
        historianBridge_ = std::make_unique<historian::HistorianBridge>(
            *historyStore_, model, bridgeCfg);
        historianBridge_->setLogger(logger);
        historianBridge_->wire();
    } else {
        logger.warn("Historian store failed to open '{}'; historian_query "
                    "will return no rows",
                    config.getHistorianDbPath());
    }

    // One-time bootstrap of the presenter + demo data, as the console frontend
    // does, so the first snapshot is already populated.
    dashboardPresenter_->initialize();
    model.initializeDemoData();

    // Drive the simulation from a background tick so alarms and history evolve
    // while the main thread blocks serving requests. AlertCenter and the
    // historian are both documented thread-safe for this reader/writer split.
    ticker_ = std::jthread([this](const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            app::model::SimulatedModel::instance().tickSimulation();
            alertCenter_->tick();
            std::this_thread::sleep_for(kTickPeriod);
        }
    });

    // Agent identity for the state-changing write tool (ADR-0024). Always build
    // a session so the server has a valid reference; only when writes are opted
    // in do we seed the synthetic agent user and wire the audit sink into the
    // presenter, so every agent write is role-checked + audited like a human
    // click. A read-only deployment leaves the session empty and the tool
    // unreachable.
    const bool writeEnabled = config.isMcpWriteEnabled();
    agentSession_ = std::make_unique<auth::Session>();
    if (writeEnabled) {
        const auth::Role agentRole = auth::parseRole(config.getMcpAgentRole());
        // Field-by-field (the auth-layer idiom) rather than a designated
        // initializer: the agent is not a real account, so passwordHash stays
        // empty by default -- no login path, no repository row (ADR-0024).
        auth::User agent;
        agent.username = kAgentUsername;
        agent.role     = agentRole;
        agentSession_->setUser(agent);

        // Audit sink shares the auth SQLite file, exactly as the human
        // frontends do. A failed open downgrades to "writes without a
        // persisted audit row" rather than killing the feature.
        auth::SqliteAuditLogger::Config auditCfg;
        auditCfg.dbPath = config.getAuthDbPath();
        auditLogger_ =
            std::make_unique<auth::SqliteAuditLogger>(std::move(auditCfg));
        auditLogger_->setLogger(logger);
        if (auditLogger_->initialize()) {
            dashboardPresenter_->setAudit(*auditLogger_, *agentSession_);
        } else {
            logger.warn("MCP audit log failed to open '{}'; equipment_command "
                        "writes will not be persisted to the audit trail",
                        config.getAuthDbPath());
            auditLogger_.reset();
        }
        logger.info("MCP write tool enabled -- agent role {}",
                    auth::roleName(agentRole));
    }

    // `output` is bound to the real stdout by main(); std::cout has been
    // redirected to stderr there, so no log line can reach the JSON-RPC stream.
    McpServer server(*alertCenter_, *historyStore_, *dashboardPresenter_,
                     *agentSession_, writeEnabled);
    return server.run(std::cin, output);
}

}  // namespace app::mcp
