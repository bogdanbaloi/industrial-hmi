#pragma once

#include "src/integration/TelemetryPayloadFormatter.h"
#include "src/integration/TelemetryPublisher.h"
#include "src/model/ProductionModel.h"

#include <memory>
#include <string>
#include <vector>

namespace app::integration {

/// Domain-specific telemetry bridge for the manufacturing reference
/// implementation.
///
/// Subscribes to ProductionModel signals and pushes them through one
/// or more TelemetryPayloadFormatters into a generic
/// TelemetryPublisher (MQTT today; AMQP / Kafka / NATS / anything that
/// implements the publisher interface tomorrow).
///
/// The bridge does no formatting itself -- *what* shape goes on the
/// wire is the formatter's job. The bridge's job is *when* to publish
/// (on every model signal it cares about) and routing the event to
/// every registered formatter. That makes the bridge open/closed: a
/// new wire format = a new formatter subclass; bridge code does not
/// change.
///
/// Config flags pick which built-in formatters get wired up by
/// default. Both flags may be true simultaneously -- the bridge then
/// publishes the same event in both shapes, on disjoint topics. Pass
/// custom formatters via the second constructor when you need
/// Protobuf / Avro / domain-specific shapes that aren't built in.
///
/// Why the bridge lives in this layer (and not in the GTK / Console
/// front-ends): both front-ends share the same Production model, and
/// both want telemetry to flow regardless of UI. Putting the bridge
/// here keeps it on the GTK-free side of the architecture so the
/// console binary can publish telemetry without dragging GTK in.
///
/// SOLID:
///   * S -- one job: route ProductionModel signals to formatters.
///     No I/O, no string building.
///   * O -- new wire format = new TelemetryPayloadFormatter subclass.
///     New domain (pharma, smart-building) = its own bridge against
///     the same TelemetryPublisher abstraction. Either extension
///     leaves this class untouched.
///   * L -- accepts any TelemetryPublisher and any
///     TelemetryPayloadFormatter subclass.
///   * D -- depends on both abstractions; never on concrete MqttClient
///     or concrete formatter.
///
/// Threading: wire() is called once on the construction thread. Model
/// callbacks then fire on whichever thread the model dispatches from;
/// the bridge forwards to each formatter, which forwards to the
/// publisher (publisher marshals to its own I/O thread internally).
class ProductionTelemetryBridge {
public:
    struct Config {
        std::string topicPrefix{"industrial-hmi"};

        /// Wire the built-in per-field plain-text formatter (default
        /// on -- preserves backward compat with subscribers from the
        /// previous wire shape).
        bool emitPlainText{true};

        /// Wire the built-in consolidated-per-entity JSON formatter
        /// (default off -- opt in when a SCADA / DCS subscribes).
        bool emitJson{false};
    };

    /// Built-in formatters chosen by Config flags.
    /// @param publisher    Generic publisher. Must outlive this bridge.
    /// @param production   Domain model. Must outlive this bridge.
    /// @param config       Topic prefix + which built-ins to enable.
    ProductionTelemetryBridge(TelemetryPublisher& publisher,
                              model::ProductionModel& production,
                              Config config);

    /// Custom formatter list -- composition root injects whichever
    /// strategies it wants (built-ins, mocks, future Protobuf / Avro).
    /// The bridge takes ownership of every formatter in the vector.
    /// Config flags are ignored in this overload; the caller is
    /// already explicit about the wire mix.
    ProductionTelemetryBridge(
        TelemetryPublisher& publisher,
        model::ProductionModel& production,
        Config config,
        std::vector<std::unique_ptr<TelemetryPayloadFormatter>> formatters);

    /// Subscribe to all ProductionModel signals we care about. Called
    /// once at startup; the registrations stay live for the lifetime
    /// of this bridge.
    void wire();

private:
    TelemetryPublisher& publisher_;
    model::ProductionModel& production_;
    Config config_;
    std::vector<std::unique_ptr<TelemetryPayloadFormatter>> formatters_;
};

}  // namespace app::integration
