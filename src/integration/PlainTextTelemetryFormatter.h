#pragma once

#include "src/integration/TelemetryPayloadFormatter.h"

namespace app::integration {

/// Per-field plain-text formatter -- the original wire shape.
///
/// Splits every event into one publish per scalar field, each with a
/// human-readable string payload. Ideal for Grafana / Telegraf / any
/// historian that wants one time series per topic and dislikes parsing
/// JSON on the broker side.
///
/// Topic vocabulary (prefix is the bridge's `topicPrefix`):
///   <prefix>/state                       -- system state name
///   <prefix>/equipment/<id>/state        -- named status
///                                           (offline/online/processing/error)
///   <prefix>/equipment/<id>/supply       -- integer supply level
///   <prefix>/quality/<id>/rate           -- pass rate, "{:.1f}"
///   <prefix>/quality/<id>/status         -- named severity
///                                           (passing/warning/critical)
///
/// Status integers map to stable strings so a SCADA consumer never
/// learns the integer codes -- a rename of the enum on the C++ side
/// does NOT silently change the wire vocabulary.
class PlainTextTelemetryFormatter final : public TelemetryPayloadFormatter {
public:
    PlainTextTelemetryFormatter() = default;
    ~PlainTextTelemetryFormatter() override = default;

    PlainTextTelemetryFormatter(const PlainTextTelemetryFormatter&) = delete;
    PlainTextTelemetryFormatter&
        operator=(const PlainTextTelemetryFormatter&) = delete;
    PlainTextTelemetryFormatter(PlainTextTelemetryFormatter&&) = delete;
    PlainTextTelemetryFormatter&
        operator=(PlainTextTelemetryFormatter&&) = delete;

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
