#include "src/core/Bootstrap.h"
#include "src/core/StartupDialog.h"
#include "src/core/StartupErrors.h"
#include "src/config/ConfigManager.h"
#include "src/integration/IntegrationManager.h"
#include "src/integration/MqttClient.h"
#include "src/integration/SensorIngestBridge.h"
#include "src/integration/ProductionTelemetryBridge.h"
#include "src/integration/TcpBackend.h"
#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
#  include "src/integration/opcua/FactoryCommandSink.h"
#  include "src/integration/opcua/FactoryNodeMap.h"
#  include "src/integration/opcua/OpcUaBackend.h"
#  include "src/integration/opcua/OpcUaConfig.h"
#  include "src/integration/opcua/OpcUaIngestBridge.h"
#  include "src/integration/opcua/Open62541Client.h"
#  include "src/integration/opcua/Open62541Server.h"
#endif
#ifdef INDUSTRIAL_HMI_HAS_MODBUS_BACKEND
#  include "src/integration/modbus/ModbusBackend.h"
#  include "src/integration/modbus/ModbusClient.h"
#  include "src/integration/modbus/ModbusIngestBridge.h"
#  include "src/integration/modbus/ModbusPollLoop.h"
#  include "src/integration/modbus/ModbusRegisterMap.h"
#endif
#include "src/auth/Argon2PasswordHasher.h"
#include "src/auth/AuthService.h"
#include "src/auth/Session.h"
#include "src/auth/SqliteAuditLogger.h"
#include "src/auth/SqliteUserRepository.h"
#include "src/historian/HistorianBridge.h"
#include "src/historian/HistorianMaintenance.h"
#include "src/historian/SqliteHistoryStore.h"
#include "src/model/DatabaseManager.h"
#include "src/model/SimulatedModel.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>   // SetConsoleOutputCP
#  include <fcntl.h>
#  include <io.h>
#  include <clocale>
#endif

#ifdef CONSOLE_MODE
#  include "src/console/InitConsole.h"
#else
#  include "src/core/Application.h"
#endif

namespace {

// Compile-time tag: true for the console binary, false for the GTK one.
// Drives the fatal-reporter between stderr and MessageBoxW.
constexpr bool kConsoleMode =
#ifdef CONSOLE_MODE
    true;
#else
    false;
#endif

// Process exit codes -- documented so CI / shell scripts can branch on them.
// Marked [[maybe_unused]] because the set used by a given build depends
// on which branch of the CONSOLE_MODE #ifdef is active (the GTK path
// propagates the GTK main-loop's own return code via `app.run(...)`).
[[maybe_unused]] constexpr int kExitOk              = 0;
[[maybe_unused]] constexpr int kExitUnexpectedFatal = 1;
[[maybe_unused]] constexpr int kExitStartupFatal    = 2;
[[maybe_unused]] constexpr int kExitUnknownFatal    = 3;

/// Build + register the MQTT backend with the IntegrationManager,
/// plus the outbound ProductionTelemetryBridge and (optionally) the
/// inbound SensorIngestBridge. Extracted from main() to keep the
/// latter under the readability-function-size threshold.
///
/// The bridges are owned via out-params because they need to outlive
/// this helper -- the caller's stack frame keeps them alive until the
/// front-end exits.
void registerMqttBackend(
    app::integration::IntegrationManager& integration,
    app::config::ConfigManager& config,
    std::unique_ptr<app::integration::ProductionTelemetryBridge>&
        productionBridgeOut,
    std::unique_ptr<app::integration::SensorIngestBridge>&
        sensorIngestBridgeOut) {
    app::integration::MqttClient::Config mqttConfig;
    mqttConfig.brokerHost = config.getMqttBrokerHost();
    mqttConfig.brokerPort =
        static_cast<std::uint16_t>(config.getMqttBrokerPort());
    mqttConfig.clientId = config.getMqttClientId();
    auto client =
        std::make_unique<app::integration::MqttClient>(std::move(mqttConfig));

    // The outbound bridge subscribes to the production model and pushes
    // through the client's TelemetryPublisher interface -- it doesn't
    // know or care that the underlying transport is MQTT.
    app::integration::ProductionTelemetryBridge::Config bridgeConfig;
    bridgeConfig.topicPrefix   = config.getMqttTopicPrefix();
    bridgeConfig.emitPlainText = config.isMqttEmitPlainText();
    bridgeConfig.emitJson      = config.isMqttEmitJson();
    productionBridgeOut =
        std::make_unique<app::integration::ProductionTelemetryBridge>(
            *client,
            app::model::SimulatedModel::instance(),
            std::move(bridgeConfig));
    productionBridgeOut->wire();

    // Inbound counterpart: same MqttClient also drives SensorIngestBridge.
    // One socket, two roles, two bridges. Wired before transferring
    // ownership of the client into the manager so the bridge constructor
    // gets a live reference.
    if (config.isMqttSubscriberEnabled()) {
        app::integration::SensorIngestBridge::Config sensorCfg;
        sensorCfg.topicPrefix = config.getMqttSensorTopicPrefix();
        sensorIngestBridgeOut =
            std::make_unique<app::integration::SensorIngestBridge>(
                *client,
                app::model::SimulatedModel::instance(),
                std::move(sensorCfg));
        sensorIngestBridgeOut->wire();
    }

    integration.registerBackend(std::move(client));
}

#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
/// Build + register the OPC-UA backend with the IntegrationManager.
/// Extracted from main() to keep the latter under the
/// readability-function-size threshold; the wiring is mechanical
/// enough that pulling it into a helper hurts nothing.
void registerOpcUaBackend(
        app::integration::IntegrationManager& integration,
        app::config::ConfigManager& config,
        app::core::Logger& logger,
        std::unique_ptr<app::integration::opcua::FactoryCommandSink>&
            commandSink) {
    app::integration::opcua::OpcUaConfig opcuaConfig;
    opcuaConfig.port =
        static_cast<std::uint16_t>(config.getOpcUaServerPort());
    opcuaConfig.applicationUri = config.getOpcUaApplicationUri();
    opcuaConfig.applicationName = config.getOpcUaApplicationName();

    auto opcuaServer =
        std::make_unique<app::integration::opcua::Open62541Server>(
            std::move(opcuaConfig), logger);

    // Inbound control surface is opt-in via config. When enabled, the
    // node map registers Factory/Commands + per-line Enabled writes;
    // the sink (owned by main()) routes each invocation to the
    // ProductionModel.
    std::unique_ptr<app::integration::opcua::FactoryNodeMap> opcuaNodeMap;
    if (config.isOpcUaServerCommandsEnabled()) {
        commandSink =
            std::make_unique<app::integration::opcua::FactoryCommandSink>(
                app::model::SimulatedModel::instance(), logger);
        opcuaNodeMap =
            std::make_unique<app::integration::opcua::FactoryNodeMap>(
                app::model::SimulatedModel::instance(), logger,
                *commandSink);
    } else {
        opcuaNodeMap =
            std::make_unique<app::integration::opcua::FactoryNodeMap>(
                app::model::SimulatedModel::instance(), logger);
    }

    integration.registerBackend(
        std::make_unique<app::integration::opcua::OpcUaBackend>(
            std::move(opcuaServer),
            std::move(opcuaNodeMap),
            logger));
}

/// Build + register the OPC-UA *client* backend (inbound role). Same
/// pattern as registerOpcUaBackend above; kept on its own so it stays
/// opt-in independently and the main() body keeps a flat list of
/// composition calls.
///
/// If `network.opcua.client.ingest_bridge.enabled` is set, an
/// `OpcUaIngestBridge` is wired alongside so inbound notifications
/// flow into the `ProductionModel`. The bridge is created BEFORE the
/// backend ownership transfers into the manager so we still hold a
/// live reference for the bridge constructor.
void registerOpcUaClient(
        app::integration::IntegrationManager& integration,
        app::config::ConfigManager& config,
        app::core::Logger& logger,
        std::unique_ptr<app::integration::opcua::OpcUaIngestBridge>&
            ingestBridge) {
    app::integration::opcua::Open62541Client::Config clientConfig;
    clientConfig.endpointUrl     = config.getOpcUaClientEndpoint();
    clientConfig.applicationUri  = config.getOpcUaClientApplicationUri();
    clientConfig.applicationName = config.getOpcUaClientApplicationName();
    auto client =
        std::make_unique<app::integration::opcua::Open62541Client>(
            std::move(clientConfig), logger);

    if (config.isOpcUaIngestBridgeEnabled()) {
        app::integration::opcua::OpcUaIngestBridge::Config bridgeConfig;
        bridgeConfig.topicPrefix =
            config.getOpcUaIngestBridgeTopicPrefix();
        ingestBridge =
            std::make_unique<app::integration::opcua::OpcUaIngestBridge>(
                *client,
                app::model::SimulatedModel::instance(),
                std::move(bridgeConfig));
        ingestBridge->wire();
    }

    integration.registerBackend(std::move(client));
}
#endif

#ifdef INDUSTRIAL_HMI_HAS_MODBUS_BACKEND
/// Build + register the Modbus master backend. Composes the four
/// pieces (client + register map + ingest bridge + poll loop) and
/// hands ownership to the IntegrationManager. Same shape as
/// registerOpcUaBackend / registerMqttBackend; lives in a helper so
/// main() stays under the readability-function-size threshold.
///
/// The register map is built from a single block of contiguous
/// holding-register addresses: equipment[i] maps to register
/// `baseAddress + i` on `slaveId`. This is the simplest mapping that
/// demonstrates the abstraction; a future schema would let the JSON
/// list individual (address, field, entity) triples. Keeps the MVP
/// JSON tiny.
void registerModbusBackend(
        app::integration::IntegrationManager& integration,
        app::config::ConfigManager& config,
        app::core::Logger& logger) {
    namespace modbus = app::integration::modbus;

    modbus::ModbusClient::Config clientConfig;
    clientConfig.host = config.getModbusHost();
    clientConfig.port =
        static_cast<std::uint16_t>(config.getModbusPort());
    clientConfig.connectTimeout =
        std::chrono::milliseconds{config.getModbusConnectTimeoutMs()};
    clientConfig.requestTimeout =
        std::chrono::milliseconds{config.getModbusRequestTimeoutMs()};
    auto client = std::make_unique<modbus::ModbusClient>(
        std::move(clientConfig));

    // Build the register map. The default layout exposes three
    // contiguous blocks on the same slave:
    //
    //   Block A (boolean):   addresses base+[0..N-1]   EquipmentEnabled
    //   Block B (supply):    addresses supplyBase+[0..N-1]
    //                                                  EquipmentSupplyLevel
    //   Block C (quality):   addresses qualityBase+[0..M-1]
    //                                                  QualityPassRate
    //
    // Block A is the original boolean per-equipment toggle. Blocks B
    // and C ride the new analog setters added in the A3 model-surface
    // refactor; bridge `scale` converts fixed-point PLC encodings
    // (raw 850 -> 85.0%) into the model's percent domain.
    //
    // Operators retarget any block via app-config.json without code
    // changes; future PRs let the JSON enumerate individual (address,
    // field, entity, scale) tuples for non-contiguous PLC layouts.
    auto map = std::make_unique<modbus::ModbusRegisterMap>();
    const auto slaveId =
        static_cast<std::uint8_t>(config.getModbusSlaveId());
    const auto enabledBase =
        static_cast<std::uint16_t>(config.getModbusEquipmentBaseAddress());
    const auto equipmentCount = config.getModbusEquipmentCount();

    // Block A -- boolean equipment-enabled bits.
    for (int i = 0; i < equipmentCount; ++i) {
        modbus::RegisterMapping mapping;
        mapping.slaveId  = slaveId;
        mapping.type     = modbus::RegisterType::HoldingRegister;
        mapping.address  =
            static_cast<std::uint16_t>(enabledBase + i);
        mapping.field    = modbus::FieldKind::EquipmentEnabled;
        mapping.entityId = static_cast<std::uint32_t>(i);
        map->add(mapping);
    }

    // Block B -- analog supply levels (one register per equipment).
    const auto supplyBase =
        static_cast<std::uint16_t>(config.getModbusSupplyBaseAddress());
    const auto supplyScale = config.getModbusSupplyScale();
    for (int i = 0; i < equipmentCount; ++i) {
        modbus::RegisterMapping mapping;
        mapping.slaveId  = slaveId;
        mapping.type     = modbus::RegisterType::HoldingRegister;
        mapping.address  =
            static_cast<std::uint16_t>(supplyBase + i);
        mapping.field    = modbus::FieldKind::EquipmentSupplyLevel;
        mapping.entityId = static_cast<std::uint32_t>(i);
        mapping.scale    = supplyScale;
        map->add(mapping);
    }

    // Block C -- analog quality pass rates (one register per checkpoint).
    const auto qualityBase =
        static_cast<std::uint16_t>(config.getModbusQualityBaseAddress());
    const auto qualityScale = config.getModbusQualityScale();
    const auto qualityCount = config.getModbusQualityCount();
    for (int i = 0; i < qualityCount; ++i) {
        modbus::RegisterMapping mapping;
        mapping.slaveId  = slaveId;
        mapping.type     = modbus::RegisterType::HoldingRegister;
        mapping.address  =
            static_cast<std::uint16_t>(qualityBase + i);
        mapping.field    = modbus::FieldKind::QualityPassRate;
        mapping.entityId = static_cast<std::uint32_t>(i);
        mapping.scale    = qualityScale;
        map->add(mapping);
    }

    auto bridge = std::make_unique<modbus::ModbusIngestBridge>(
        app::model::SimulatedModel::instance());

    modbus::ModbusPollLoop::Config pollConfig;
    pollConfig.pollInterval =
        std::chrono::milliseconds{config.getModbusPollIntervalMs()};
    // The poll loop holds references; create it AFTER its
    // collaborators and pass them in. ModbusBackend then takes
    // ownership of all four via unique_ptr.
    auto pollLoop = std::make_unique<modbus::ModbusPollLoop>(
        *client, *map, *bridge, pollConfig);

    integration.registerBackend(
        std::make_unique<modbus::ModbusBackend>(
            std::move(client),
            std::move(map),
            std::move(bridge),
            std::move(pollLoop),
            logger));
}
#endif

/// Build the Historian (SQLite store + bridge) when enabled in config.
/// Extracted from main() for the same reason as registerMqttBackend
/// etc -- keeps main() under the readability-function-size threshold,
/// and a degraded-config audit reads as a flat list of helpers.
///
/// If `initialize()` fails (bad path, read-only fs, permission), the
/// store is dropped and the bridge stays unconstructed -- the rest of
/// the binary keeps running with a missing History tab, matching the
/// project-wide "degraded > crash" policy.
void registerHistorian(
        app::config::ConfigManager& config,
        app::core::Logger& logger,
        std::unique_ptr<app::historian::SqliteHistoryStore>& storeOut,
        std::unique_ptr<app::historian::HistorianBridge>& bridgeOut,
        std::unique_ptr<app::historian::HistorianMaintenance>&
            maintenanceOut) {
    app::historian::SqliteHistoryStore::Config storeCfg;
    storeCfg.dbPath = config.getHistorianDbPath();
    storeOut = std::make_unique<app::historian::SqliteHistoryStore>(
        std::move(storeCfg));
    storeOut->setLogger(logger);

    if (!storeOut->initialize()) {
        logger.warn("Historian disabled: SqliteHistoryStore failed to "
                    "open '{}'", config.getHistorianDbPath());
        storeOut.reset();
        return;
    }

    app::historian::HistorianBridge::Config bridgeCfg;
    bridgeCfg.maxBatchSize = static_cast<std::size_t>(
        config.getHistorianBatchSize());
    bridgeCfg.maxBatchAge  = std::chrono::milliseconds{
        config.getHistorianBatchAgeMs()};
    bridgeOut = std::make_unique<app::historian::HistorianBridge>(
        *storeOut,
        app::model::SimulatedModel::instance(),
        bridgeCfg);
    bridgeOut->setLogger(logger);
    bridgeOut->wire();

    // Tiered-retention worker -- one jthread that demotes
    // raw -> 1m -> 1h on a cadence. RAII via unique_ptr; the
    // destructor cancels the stop_token, wakes the cv, joins.
    app::historian::HistorianMaintenance::Config mainCfg;
    mainCfg.sweepInterval   = std::chrono::milliseconds{
        config.getHistorianSweepIntervalMs()};
    mainCfg.rawRetention    = std::chrono::milliseconds{
        config.getHistorianRawRetentionMs()};
    mainCfg.minuteRetention = std::chrono::milliseconds{
        config.getHistorianMinuteRetentionMs()};
    maintenanceOut = std::make_unique<
        app::historian::HistorianMaintenance>(*storeOut, mainCfg);
    maintenanceOut->setLogger(logger);
    maintenanceOut->start();
}

/// Build the auth stack (repository + hasher + service) when enabled
/// in config. Same shape as the other register helpers: stack-owned
/// pieces handed back to main() so the destructors fire in reverse
/// declaration order at exit.
///
/// If the SQLite user store fails to initialise (read-only fs, bad
/// path) the stack stays unconstructed and the caller continues
/// without auth -- matches the project-wide degraded-over-crash
/// policy. Seeded default users (operator / maintenance / admin) are
/// only inserted on first run; a populated table is left alone.
void registerAuth(
        app::config::ConfigManager& config,
        app::core::Logger& logger,
        std::unique_ptr<app::auth::SqliteUserRepository>& repoOut,
        std::unique_ptr<app::auth::Argon2PasswordHasher>& hasherOut,
        std::unique_ptr<app::auth::SqliteAuditLogger>& auditOut,
        std::unique_ptr<app::auth::AuthService>& serviceOut,
        app::auth::Session& sessionRef) {
    app::auth::SqliteUserRepository::Config repoCfg;
    repoCfg.dbPath = config.getAuthDbPath();
    repoOut = std::make_unique<app::auth::SqliteUserRepository>(
        std::move(repoCfg));
    repoOut->setLogger(logger);

    if (!repoOut->initialize()) {
        logger.warn("Auth disabled: user store failed to open '{}'",
                    config.getAuthDbPath());
        repoOut.reset();
        return;
    }

    // Audit log shares the same SQLite file. Two tables (users +
    // audit_log) in one DB keeps backup + permissioning simple on
    // the operator terminal. A failed open downgrades to "auth
    // without audit" rather than crashing the whole feature.
    app::auth::SqliteAuditLogger::Config auditCfg;
    auditCfg.dbPath = config.getAuthDbPath();
    auditOut = std::make_unique<app::auth::SqliteAuditLogger>(
        std::move(auditCfg));
    auditOut->setLogger(logger);
    if (!auditOut->initialize()) {
        logger.warn("Audit log disabled: failed to open '{}'",
                    config.getAuthDbPath());
        auditOut.reset();
    }

    hasherOut  = std::make_unique<app::auth::Argon2PasswordHasher>();
    serviceOut = std::make_unique<app::auth::AuthService>(
        *repoOut, *hasherOut, sessionRef);
    serviceOut->setLogger(logger);
    if (auditOut) {
        serviceOut->setAuditLogger(*auditOut);
    }

    // Seed the three demo accounts on first run. The seeder is
    // idempotent so a populated DB is left alone.
    serviceOut->seedDefaultUsersIfEmpty();
}

#ifdef _WIN32
/// Windows-only platform init: Cairo renderer override + UTF-8 setup.
///
/// Three independent layers must agree on UTF-8 for the console
/// front-end to render translated strings correctly:
///   1. Win32 console codepage (cmd.exe stdout).
///   2. CRT locale (.UTF-8 supported from Windows 10 v1803; older
///      systems silently fall back).
///   3. Binary mode on stdout -- stops the CRT from LF->CRLF +
///      codepage conversion when stdout is a pipe (Git Bash /
///      mintty). Without this, UTF-8 bytes become Latin-1 mojibake.
///
/// Extracted from main() so the readability-function-size lint stays
/// under the 150-line threshold.
void initWindowsConsole() {
    _putenv_s("GSK_RENDERER", "cairo");
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    initWindowsConsole();
#endif

    // Top-level exception guard. Exit codes:
    //   2 = startup fatal (config/DB), 1 = std::exception escaped,
    //   3 = non-std exception. See kExit* constants above.
    try {
        // Staged startup: logger -> config -> configured logger -> i18n.
        app::core::Bootstrap bootstrap;
        bootstrap.run();

        // Integration backends -- opt-in per deployment via JSON.
        // Stack-owned through main() so RAII shuts them down on exit.
        auto& config = app::config::ConfigManager::instance();
        app::integration::IntegrationManager integration;
        std::unique_ptr<app::integration::ProductionTelemetryBridge>
            productionBridge;
        std::unique_ptr<app::integration::SensorIngestBridge>
            sensorIngestBridge;

        // Auth + Historian stacks. Declared in construction order so
        // destruction reverses naturally. See registerAuth() /
        // registerHistorian() for the wiring + degraded-open paths.
        app::auth::Session                                authSession;
        std::unique_ptr<app::auth::SqliteUserRepository>  authRepo;
        std::unique_ptr<app::auth::Argon2PasswordHasher>  authHasher;
        std::unique_ptr<app::auth::SqliteAuditLogger>     auditLogger;
        std::unique_ptr<app::auth::AuthService>           authService;
        std::unique_ptr<app::historian::SqliteHistoryStore>   historyStore;
        std::unique_ptr<app::historian::HistorianBridge>      historianBridge;
        std::unique_ptr<app::historian::HistorianMaintenance> historianMaintenance;
#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
        std::unique_ptr<app::integration::opcua::OpcUaIngestBridge>
            opcuaIngestBridge;
        std::unique_ptr<app::integration::opcua::FactoryCommandSink>
            opcuaCommandSink;
#endif

        if (config.isTcpBackendEnabled()) {
            integration.registerBackend(
                std::make_unique<app::integration::TcpBackend>(
                    static_cast<std::uint16_t>(config.getTcpBackendPort()),
                    app::model::SimulatedModel::instance(),
                    app::model::DatabaseManager::instance()));
        }

        if (config.isAuthEnabled()) {
            registerAuth(config, bootstrap.logger(),
                         authRepo, authHasher, auditLogger,
                         authService, authSession);
        }

        if (config.isHistorianEnabled()) {
            registerHistorian(config, bootstrap.logger(),
                              historyStore, historianBridge,
                              historianMaintenance);
        }

        if (config.isMqttBackendEnabled()) {
            registerMqttBackend(integration, config,
                                productionBridge, sensorIngestBridge);
        }

#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
        // OPC-UA server + client backends -- both opt-in via config,
        // compiled out via BUILD_OPCUA_BACKEND=OFF. See register*().
        if (config.isOpcUaBackendEnabled()) {
            registerOpcUaBackend(integration, config, bootstrap.logger(),
                                 opcuaCommandSink);
        }
        if (config.isOpcUaClientEnabled()) {
            registerOpcUaClient(integration, config, bootstrap.logger(),
                                opcuaIngestBridge);
        }
#endif

#ifdef INDUSTRIAL_HMI_HAS_MODBUS_BACKEND
        // Modbus master -- opt-in via config; compiled out via
        // BUILD_MODBUS_BACKEND=OFF. See registerModbusBackend().
        if (config.isModbusBackendEnabled()) {
            registerModbusBackend(integration, config, bootstrap.logger());
        }
#endif

        integration.startAll();

#ifdef CONSOLE_MODE
        (void)argc; (void)argv;
        app::console::InitConsole console(bootstrap);
        console.run();
        integration.stopAll();
        return kExitOk;
#else
        auto& app = app::core::Application::instance();
        // Inject the manager so MainWindow can mount the backend-
        // health bar in the sidebar. Pointer stays valid through
        // app.run() because `integration` lives on the same stack
        // frame just above us.
        app.setIntegrationManager(&integration);
        // Historian read side is optional -- mounted only when the
        // store opened successfully above. Pointer (or null) flows
        // to MainWindow which decides whether to register the page.
        app.setHistoryReader(historyStore.get());
        // Auth: when registerAuth() succeeded both pointers are non-
        // null and Application::run() will show the LoginDialog first.
        // When auth is disabled (or registration failed) the pointers
        // stay null and run() goes straight to MainWindow as before.
        app.setAuth(authService.get(), &authSession);
        app.setAuditLogger(auditLogger.get());
        app.initialize(bootstrap, argc, argv);   // throws DatabaseInitError on DB failure

        // Inject the app-wide logger into the SimulatedModel singleton so
        // its state transitions and tick traces show up in the normal log
        // stream. Done here (not in Application::initDatabase) because
        // including SimulatedModel.h there would pull in
        // ProductionTypes::ERROR after gtkmm has already defined the
        // wingdi.h ERROR=0 macro.
        app::model::SimulatedModel::instance().setLogger(app.logger());

        const int result = app.run(argc, argv);
        integration.stopAll();
        app.shutdown();
        return result;
#endif
    } catch (const app::core::CriticalStartupError& e) {
        app::core::reportFatalStartup(e, kConsoleMode);
        return kExitStartupFatal;
    } catch (const std::exception& e) {
        app::core::reportUnexpectedFatal(e.what(), kConsoleMode);
        return kExitUnexpectedFatal;
    } catch (...) {
        app::core::reportUnexpectedFatal(
            "Unknown (non-std::exception) fatal error reached main.",
            kConsoleMode);
        return kExitUnknownFatal;
    }
}
