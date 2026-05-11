#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace app::integration {

/// Generic topic-keyed telemetry subscriber.
///
/// Mirror of `TelemetryPublisher`, for the inbound direction: a
/// concrete messaging backend (MQTT today; AMQP / Kafka / NATS in the
/// future) exposes this interface so that ingestion bridges can
/// register topic filters + callbacks without knowing the wire
/// protocol underneath.
///
/// Typical wiring (composition root selects the concrete):
///
///   MqttClient mqtt(cfg);             // implements both interfaces
///   SensorIngestBridge bridge(model); // depends on TelemetrySubscriber&
///   bridge.attach(mqtt);              // subscribes via the interface
///
/// SOLID:
///   * S -- one job: hand inbound (topic, payload) pairs to callers.
///     No domain awareness, no connection lifecycle (that's
///     `IntegrationBackend`'s job; concretes typically inherit both).
///   * O -- adding a new wire protocol = new subclass; existing
///     ingestion bridges stay untouched.
///   * L -- bridges depend on TelemetrySubscriber&; any concrete is
///     substitutable.
///   * I -- intentionally narrow: one method. RPC-style backends
///     have their own separate interface.
///   * D -- bridges depend on this abstraction, never on the concrete
///     MqttClient / KafkaSubscriber / etc.
class TelemetrySubscriber {
public:
    /// Topic + payload are passed as `string_view` so the callback can
    /// peek without forcing a copy; if the consumer needs to outlive
    /// the call (e.g. dispatch to another thread), it must copy.
    using MessageCallback =
        std::function<void(std::string_view topic,
                           std::string_view payload)>;

    virtual ~TelemetrySubscriber() = default;

    TelemetrySubscriber(const TelemetrySubscriber&)            = delete;
    TelemetrySubscriber& operator=(const TelemetrySubscriber&) = delete;
    TelemetrySubscriber(TelemetrySubscriber&&)                 = delete;
    TelemetrySubscriber& operator=(TelemetrySubscriber&&)      = delete;

    /// Register a callback for a topic filter. The concrete subscriber
    /// arranges with its broker to receive matching publishes, and
    /// invokes `callback` once per inbound frame.
    ///
    /// Must be safe to call from any thread; concretes marshal the
    /// SUBSCRIBE write to their own I/O thread.
    ///
    /// Multiple subscriptions to the same filter are allowed; each
    /// callback is invoked.
    ///
    /// Local matching is exact-string by default -- pass concrete
    /// topics, not wildcards, unless the concrete documents wildcard
    /// resolution.
    virtual void subscribe(const std::string& topicFilter,
                           MessageCallback callback) = 0;

protected:
    TelemetrySubscriber() = default;
};

}  // namespace app::integration
