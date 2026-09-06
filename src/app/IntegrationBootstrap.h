#pragma once

#include <memory>

namespace app::config {
class ConfigManager;
}
namespace app::core {
class Logger;
}
namespace app::model {
class MirrorModel;
}

namespace app::integration {

class IntegrationManager;
class ProductionTelemetryBridge;
class SensorIngestBridge;
class PrimaryToSecondaryBridge;

#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
namespace opcua {
class OpcUaIngestBridge;
class FactoryCommandSink;
}  // namespace opcua
#endif

/// Owns the IntegrationManager plus every side object a backend needs kept
/// alive for the process lifetime (the outbound / inbound bridges, the
/// OPC-UA command sink referenced by its node map, the multi-station mirror
/// model and its bridge). Backends that own all their own pieces (TCP,
/// Modbus) contribute no members here.
///
/// The manager is declared first so, on destruction (reverse declaration
/// order), the bridges that hold references into the manager's backends are
/// torn down before the manager -- the same ordering main() relied on when
/// these were separate stack locals.
///
/// Moveable (returned by value from buildIntegrationServices), non-copyable.
/// Special members are defined out-of-line because the members are
/// unique_ptr to types only forward-declared here.
struct IntegrationServices {
    std::unique_ptr<IntegrationManager>        manager;
    std::unique_ptr<ProductionTelemetryBridge> productionBridge;
    std::unique_ptr<SensorIngestBridge>        sensorIngestBridge;
    std::unique_ptr<model::MirrorModel>        secondaryModel;
    std::unique_ptr<PrimaryToSecondaryBridge>  primarySecondaryBridge;
#ifdef INDUSTRIAL_HMI_HAS_OPCUA_BACKEND
    std::unique_ptr<opcua::OpcUaIngestBridge>  opcuaIngestBridge;
    std::unique_ptr<opcua::FactoryCommandSink> opcuaCommandSink;
#endif

    IntegrationServices();
    ~IntegrationServices();
    IntegrationServices(IntegrationServices&&) noexcept;
    IntegrationServices& operator=(IntegrationServices&&) noexcept;
    IntegrationServices(const IntegrationServices&)            = delete;
    IntegrationServices& operator=(const IntegrationServices&) = delete;
};

/// Build -- but do NOT start -- every integration backend the config enables,
/// bundling the manager with the owned side objects. This is the single
/// composition point for the integration layer, shared by the GTK, console
/// and Qt frontends so "what wire protocols does this binary speak" has one
/// answer instead of one per composition root (extends the toolkit-
/// independence of the presenter layer to the integration layer).
///
/// The caller starts the manager (`services.manager->startAll()`) and keeps
/// the returned bundle alive until after `stopAll()`.
[[nodiscard]] IntegrationServices buildIntegrationServices(
    config::ConfigManager& config, core::Logger& logger);

}  // namespace app::integration
