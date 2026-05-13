#pragma once

#include "src/integration/TelemetryPublisher.h"
#include "src/model/ProductionTypes.h"

#include <string_view>

namespace app::integration {

/// Strategy interface: turns a domain event into one or more
/// {topic, payload} pairs and pushes them through a TelemetryPublisher.
///
/// ProductionTelemetryBridge owns a list of these and fans each event
/// out across every registered formatter. That decouples *what* we
/// publish (the event) from *how* it goes on the wire (plain text,
/// JSON, Protobuf, Avro, ...). Adding a new wire format = one new
/// subclass; the bridge and every existing formatter stay untouched.
///
/// SOLID:
///   * S -- one job: format a single domain event for one wire shape.
///     No subscription logic, no I/O lifecycle, no policy on which
///     events the bridge actually cares about.
///   * O -- new wire format (Protobuf / Avro / CBOR / ...) lands as a
///     new subclass; bridge code does not change.
///   * L -- bridges depend on TelemetryPayloadFormatter&; every
///     concrete is substitutable as long as it accepts the same
///     event types.
///   * I -- intentionally narrow: three methods, one per event family
///     the bridge knows about (system state, equipment, quality).
///   * D -- the bridge holds the abstraction; concrete formatters are
///     plugged in at composition root.
///
/// Threading: methods run on whichever thread the model dispatches
/// from. Implementations must be reentrant; the publisher itself
/// serialises to its own I/O thread.
class TelemetryPayloadFormatter {
public:
    virtual ~TelemetryPayloadFormatter() = default;

    TelemetryPayloadFormatter(const TelemetryPayloadFormatter&) = delete;
    TelemetryPayloadFormatter&
        operator=(const TelemetryPayloadFormatter&) = delete;
    TelemetryPayloadFormatter(TelemetryPayloadFormatter&&) = delete;
    TelemetryPayloadFormatter&
        operator=(TelemetryPayloadFormatter&&) = delete;

    /// One system-state transition -> one or more publishes.
    virtual void publishSystemState(TelemetryPublisher& publisher,
                                    std::string_view topicPrefix,
                                    model::SystemState state) = 0;

    /// One equipment-status update -> one or more publishes.
    virtual void publishEquipment(TelemetryPublisher& publisher,
                                  std::string_view topicPrefix,
                                  const model::EquipmentStatus& es) = 0;

    /// One quality-checkpoint update -> one or more publishes.
    virtual void publishQuality(TelemetryPublisher& publisher,
                                std::string_view topicPrefix,
                                const model::QualityCheckpoint& qc) = 0;

protected:
    TelemetryPayloadFormatter() = default;
};

}  // namespace app::integration
