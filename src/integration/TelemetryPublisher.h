#pragma once

#include <string>

namespace app::integration {

/// Generic topic-keyed key/value telemetry publisher.
///
/// This is the abstraction every messaging-style backend implements
/// (MQTT today; AMQP, Kafka, NATS in the future). Domain code never
/// references a concrete publisher -- it talks to this interface,
/// which makes the integration layer reusable across vertical
/// deployments:
///
///   * Manufacturing: equipment + quality + work-unit telemetry
///   * Pharma: lab instrument readings, batch state
///   * Smart-building: HVAC, sensors, occupancy
///   * Energy: SCADA telemetry, breaker states
///
/// Each vertical writes its own thin "bridge" that subscribes to its
/// domain model and calls publish() on this interface. The
/// publisher concrete (MQTT broker, Kafka cluster, in-process logger)
/// is selected at deployment time via configuration.
///
/// SOLID:
///   * S -- one job: ship a {topic, payload} pair somewhere external.
///     No domain awareness. No connection lifecycle (that's
///     IntegrationBackend's job; concretes typically inherit both).
///   * O -- adding a new wire protocol = new subclass; existing
///     bridges (ProductionTelemetryBridge etc.) stay untouched.
///   * L -- bridges depend on TelemetryPublisher&; any concrete is
///     substitutable as long as it honours canPublish() / publish().
///   * I -- intentionally narrow: 2 methods. RPC-style (request/
///     response) backends have their own separate interface.
///   * D -- bridges depend on this abstraction, never on the
///     concrete MqttPublisher / KafkaPublisher / etc.
class TelemetryPublisher {
public:
    virtual ~TelemetryPublisher() = default;

    TelemetryPublisher(const TelemetryPublisher&) = delete;
    TelemetryPublisher& operator=(const TelemetryPublisher&) = delete;
    TelemetryPublisher(TelemetryPublisher&&) = delete;
    TelemetryPublisher& operator=(TelemetryPublisher&&) = delete;

    /// Publish a topic/payload pair. Must be safe to call from any
    /// thread; concrete publishers serialise to their own I/O thread.
    /// Failures are silent at this layer -- backends decide whether
    /// to log / mark themselves down / drop the message.
    virtual void publish(const std::string& topic,
                         const std::string& payload) = 0;

    /// True iff a publish() call right now would actually reach the
    /// transport (connection up, no fatal error). Bridges check this
    /// to skip work when the backend is offline so the heavy lifting
    /// (formatting, serialisation) stays cheap on the failure path.
    [[nodiscard]] virtual bool canPublish() const = 0;

protected:
    TelemetryPublisher() = default;
};

}  // namespace app::integration
