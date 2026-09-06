#include "src/app/IntegrationBootstrap.h"

#include "src/config/ConfigManager.h"
#include "src/core/LoggerBase.h"
#include "src/integration/IntegrationManager.h"
#include "src/integration/MqttClient.h"
#include "src/integration/PrimaryToSecondaryBridge.h"
#include "src/integration/ProductionTelemetryBridge.h"
#include "src/integration/SensorIngestBridge.h"
#include "src/integration/TcpBackend.h"
#include "src/model/DatabaseManager.h"
#include "src/model/MirrorModel.h"
#include "src/model/SimulatedModel.h"

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

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace app::integration {

namespace {

// Build + register the MQTT backend with the IntegrationManager, plus the
// outbound ProductionTelemetryBridge and (optionally) the inbound
// SensorIngestBridge. The bridges are owned via out-params because they must
// outlive this helper -- the returned bundle keeps them alive.
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
// Build + register the OPC-UA server backend. The command sink is owned via
// an out-param because the node map holds a reference to it.
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
    // the sink (kept in the bundle) routes each invocation to the
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

// Build + register the OPC-UA client backend (inbound role). If the ingest
// bridge is enabled, it is wired before ownership transfers into the manager
// so the bridge constructor gets a live reference.
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
// Build + register the Modbus primary backend. Composes the four pieces
// (client + register map + ingest bridge + poll loop); ModbusBackend takes
// ownership of all of them so nothing leaks into the bundle.
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

    // The register map exposes three contiguous blocks on the same
    // secondary: boolean equipment-enabled bits, analog supply levels and
    // analog quality pass rates. Operators retarget any block via
    // app-config.json without code changes.
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
    // The poll loop holds references; create it AFTER its collaborators and
    // pass them in. ModbusBackend then takes ownership of all four.
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

// Multi-station: build the secondary MirrorModel + the in-process
// PrimaryToSecondaryBridge linking the singleton SimulatedModel (primary)
// into the mirror (secondary), then hand the bridge to the
// IntegrationManager so it surfaces in the backend-health view next to the
// other protocols. See ADR-0011.
void registerMultiStation(
        app::integration::IntegrationManager& integration,
        std::unique_ptr<app::model::MirrorModel>& secondaryModelOut,
        std::unique_ptr<app::integration::PrimaryToSecondaryBridge>& bridgeOut) {
    secondaryModelOut = std::make_unique<app::model::MirrorModel>();
    bridgeOut = std::make_unique<app::integration::PrimaryToSecondaryBridge>(
        app::model::SimulatedModel::instance(),
        *secondaryModelOut);
    integration.registerBackend(std::move(bridgeOut));
}

}  // namespace

IntegrationServices::IntegrationServices()                             = default;
IntegrationServices::~IntegrationServices()                            = default;
IntegrationServices::IntegrationServices(IntegrationServices&&) noexcept = default;
IntegrationServices& IntegrationServices::operator=(
    IntegrationServices&&) noexcept = default;

IntegrationServices buildIntegrationServices(
    app::config::ConfigManager& config,
    [[maybe_unused]] app::core::Logger& logger) {
    IntegrationServices services;
    services.manager = std::make_unique<IntegrationManager>();
    auto& integration = *services.manager;

    if (config.isTcpBackendEnabled()) {
        integration.registerBackend(
            std::make_unique<app::integration::TcpBackend>(
                static_cast<std::uint16_t>(config.getTcpBackendPort()),
                app::model::SimulatedModel::instance(),
                app::model::DatabaseManager::instance()));
    }

    if (config.isMqttBackendEnabled()) {
        registerMqttBackend(integration, config, services.productionBridge,
                            services.sensorIngestBridge);
    }

#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
    if (config.isOpcUaBackendEnabled()) {
        registerOpcUaBackend(integration, config, logger,
                             services.opcuaCommandSink);
    }
    if (config.isOpcUaClientEnabled()) {
        registerOpcUaClient(integration, config, logger,
                            services.opcuaIngestBridge);
    }
#endif

#ifdef INDUSTRIAL_HMI_HAS_MODBUS_BACKEND
    if (config.isModbusBackendEnabled()) {
        registerModbusBackend(integration, config, logger);
    }
#endif

    if (config.isMultiStationEnabled()) {
        registerMultiStation(integration, services.secondaryModel,
                             services.primarySecondaryBridge);
    }

    return services;
}

}  // namespace app::integration
