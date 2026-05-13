#pragma once

#include "src/integration/TelemetryPayloadFormatter.h"

namespace app::integration {

/// Consolidated-per-entity JSON formatter.
///
/// Where PlainTextTelemetryFormatter splits an event across one topic
/// per scalar field, this formatter ships the full entity snapshot in a
/// single JSON document on a single topic. Ideal for SCADA / DCS /
/// downstream consumers that want one logical message per equipment
/// or checkpoint, with named keys, and that already parse JSON.
///
/// Topic vocabulary (suffix `/json` keeps the two formatters
/// non-colliding when both are enabled side by side):
///   <prefix>/state/json
///       -> {"state":"running"}
///   <prefix>/equipment/<id>/json
///       -> {"id":7,"state":"processing","supply":42}
///   <prefix>/quality/<id>/json
///       -> {"id":5,"rate":98.7,"status":"passing"}
///
/// The same named-status vocabulary as the plain formatter is used --
/// keeping the two wire shapes in lockstep so a renamed enum on the
/// C++ side surfaces identically on both topics.
///
/// JSON is hand-rolled (no nlohmann dep) -- the payloads are flat and
/// finite, and the project deliberately avoids pulling a JSON library
/// into the integration layer just for serialisation.
class JsonTelemetryFormatter final : public TelemetryPayloadFormatter {
public:
    JsonTelemetryFormatter() = default;
    ~JsonTelemetryFormatter() override = default;

    JsonTelemetryFormatter(const JsonTelemetryFormatter&) = delete;
    JsonTelemetryFormatter& operator=(const JsonTelemetryFormatter&) = delete;
    JsonTelemetryFormatter(JsonTelemetryFormatter&&) = delete;
    JsonTelemetryFormatter& operator=(JsonTelemetryFormatter&&) = delete;

    void publishSystemState(TelemetryPublisher& publisher,
                            std::string_view topicPrefix,
                            model::SystemState state) override;

    void publishEquipment(TelemetryPublisher& publisher,
                          std::string_view topicPrefix,
                          const model::EquipmentStatus& es) override;

    void publishQuality(TelemetryPublisher& publisher,
                        std::string_view topicPrefix,
                        const model::QualityCheckpoint& qc) override;
};

}  // namespace app::integration
