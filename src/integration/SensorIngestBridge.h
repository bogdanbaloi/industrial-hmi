#pragma once

#include "src/integration/TelemetrySubscriber.h"

#include <cstdint>
#include <string>

namespace app::model {
class ProductionModel;
}

namespace app::integration {

/// Inbound counterpart to ProductionTelemetryBridge: subscribes to
/// sensor-side MQTT topics through a `TelemetrySubscriber` and mutates
/// a `ProductionModel` accordingly. Lets external field devices /
/// supervisors push state changes into the HMI without speaking the
/// TCP control protocol.
///
/// Topic vocabulary (default prefix `industrial-hmi-sensors`):
///
///   <prefix>/equipment/<id>/state    payload = on|off|1|0|true|false
///
/// Payload parsing is case-insensitive, whitespace-trimmed. Unknown
/// payloads are dropped silently (the bridge is a fan-in, not a
/// validator -- a misbehaving sensor must not crash the HMI).
///
/// SOLID:
///   * S -- one job: translate inbound MQTT into Model calls.
///     Doesn't open sockets, doesn't render anything, doesn't know
///     MQTT framing.
///   * O -- adding a new sensor topic = add a `subscribe()` call.
///     The dispatch table is data, not control flow.
///   * L -- depends on `TelemetrySubscriber&` so any concrete (MQTT
///     today; Kafka subscriber tomorrow) plugs in unchanged.
///   * D -- model mutation goes through `ProductionModel&`
///     abstraction, not `SimulatedModel`.
///
/// Threading: callbacks fire on the subscriber's I/O thread. The
/// bridge calls `ProductionModel::setEquipmentEnabled(...)` which is
/// the same entry point used by TCP commands -- ProductionModel
/// guarantees thread-safety for its setters (see its docstring).
class SensorIngestBridge {
public:
    struct Config {
        /// Common prefix prepended to every subscribed topic. Tests
        /// can shorten it; deployments mirror the publisher prefix
        /// so a single broker namespace stays consistent.
        std::string topicPrefix{"industrial-hmi-sensors"};
        /// How many equipment slots to subscribe to. Matches
        /// SimulatedModel's three lines (A/B/C); deployments with
        /// more lines bump this and add expected payloads.
        std::uint32_t equipmentCount{3};
    };

    // Two overloads avoid a default `Config{}` argument inside the
    // class body -- gcc requires Config's default member initialisers
    // to be complete before they can be used in a default function
    // arg, and the constructor declaration is parsed before they are.
    SensorIngestBridge(TelemetrySubscriber& subscriber,
                       model::ProductionModel& model);
    SensorIngestBridge(TelemetrySubscriber& subscriber,
                       model::ProductionModel& model,
                       Config config);

    SensorIngestBridge(const SensorIngestBridge&)            = delete;
    SensorIngestBridge& operator=(const SensorIngestBridge&) = delete;
    SensorIngestBridge(SensorIngestBridge&&)                 = delete;
    SensorIngestBridge& operator=(SensorIngestBridge&&)      = delete;
    ~SensorIngestBridge()                                    = default;

    /// Register subscriptions on the wrapped TelemetrySubscriber.
    /// Idempotent on the bridge side; the subscriber's own contract
    /// decides whether the duplicate SUBSCRIBE hits the broker.
    void wire();

private:
    /// Convert an MQTT payload (e.g. "on", "OFF", "1") into a boolean.
    /// Returns std::nullopt for anything unrecognised so the caller
    /// can ignore the frame without raising.
    [[nodiscard]] static bool parseOnOffPayload(std::string_view raw,
                                                bool& out);

    TelemetrySubscriber& subscriber_;
    model::ProductionModel& model_;
    Config config_;
};

}  // namespace app::integration
