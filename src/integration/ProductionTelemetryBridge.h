#pragma once

#include "src/integration/TelemetryPublisher.h"
#include "src/model/ProductionModel.h"

#include <chrono>
#include <string>

namespace app::integration {

/// Domain-specific telemetry bridge for the manufacturing reference
/// implementation.
///
/// Subscribes to ProductionModel signals and pushes them to a generic
/// TelemetryPublisher (MQTT today; AMQP / Kafka / NATS / anything that
/// implements the interface tomorrow). The bridge owns the topic
/// vocabulary; the publisher knows nothing about it.
///
/// Topic schema (prefix configurable):
///   <prefix>/state                  -- system state on every change
///   <prefix>/equipment/<id>/state   -- equipment "ok" / "fault"
///   <prefix>/quality/<id>/rate      -- quality checkpoint pass rate
///   <prefix>/heartbeat              -- not emitted here; the
///                                      publisher owns heartbeats
///                                      via PINGREQ / its own timer.
///
/// Why the bridge lives in this layer (and not in the GTK / Console
/// front-ends): both front-ends share the same Production model, and
/// both want telemetry to flow regardless of UI. Putting the bridge
/// here keeps it on the GTK-free side of the architecture so the
/// console binary can publish telemetry without dragging GTK in.
///
/// SOLID:
///   * S -- one job: map ProductionModel signals to TelemetryPublisher
///     calls. No I/O, no formatting beyond float-to-string.
///   * O -- a new domain (pharma, smart-building) writes its OWN
///     bridge against the same TelemetryPublisher. This class stays
///     untouched.
///   * L -- accepts any TelemetryPublisher subclass.
///   * D -- depends on TelemetryPublisher abstraction; never on
///     MqttPublisher concrete.
///
/// Threading:
///   * wire() is called once on the construction thread. Model
///     callbacks then fire on whichever thread the model dispatches
///     from; we forward to publisher.publish() which marshals
///     internally to its own I/O thread.
class ProductionTelemetryBridge {
public:
    /// Topic-prefix-only for now; future configuration (e.g. include
    /// equipment subset, severity threshold) goes here.
    struct Config {
        std::string topicPrefix{"industrial-hmi"};
    };

    /// @param publisher    Generic publisher to deliver telemetry.
    ///                     Must outlive this bridge.
    /// @param production   Domain model whose signals drive
    ///                     publication. Must outlive this bridge.
    /// @param config       Topic prefix + future knobs.
    ProductionTelemetryBridge(TelemetryPublisher& publisher,
                              model::ProductionModel& production,
                              Config config);

    /// Subscribe to all ProductionModel signals we care about. Called
    /// once at startup; the registrations stay live for the lifetime
    /// of this bridge.
    void wire();

private:
    TelemetryPublisher& publisher_;
    model::ProductionModel& production_;
    Config config_;
};

}  // namespace app::integration
