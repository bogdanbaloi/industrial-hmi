// Model header first so its enumerators are parsed before any other header
// pulls in a conflicting macro (same discipline as the other init roots).
#include "src/model/SimulatedModel.h"
#include "src/presenter/DashboardPresenter.h"

#include "src/mcp/McpInitRoot.h"

#include "src/mcp/McpServer.h"
#include "src/config/ConfigManager.h"
#include "src/core/Bootstrap.h"
#include "src/core/LoggerBase.h"
#include "src/historian/HistorianBridge.h"
#include "src/historian/SqliteHistoryStore.h"
#include "src/model/ModelContext.h"
#include "src/presenter/AlertCenter.h"

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
    dashboardPresenter_.reset();
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

    // `output` is bound to the real stdout by main(); std::cout has been
    // redirected to stderr there, so no log line can reach the JSON-RPC stream.
    McpServer server(*alertCenter_, *historyStore_);
    return server.run(std::cin, output);
}

}  // namespace app::mcp
