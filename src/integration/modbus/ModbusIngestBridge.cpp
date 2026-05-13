#include "src/integration/modbus/ModbusIngestBridge.h"

#include "src/model/ProductionModel.h"

namespace app::integration::modbus {

ModbusIngestBridge::ModbusIngestBridge(model::ProductionModel& model)
    : model_(model) {}

void ModbusIngestBridge::onRegisterChanged(const RegisterMapping& mapping,
                                           std::uint16_t rawValue) {
    using enum FieldKind;
    switch (mapping.field) {
    case EquipmentEnabled: {
        // Out-of-range entity ids are silently ignored. A misconfigured
        // register map mustn't crash the HMI -- the operator notices
        // the missing dashboard update and corrects the JSON.
        if (mapping.entityId >= kMaxTrackedEquipment) {
            return;
        }

        const bool enabled = (rawValue != 0);
        auto& cached = lastEnabled_[mapping.entityId];
        if (cached.has_value() && *cached == enabled) {
            return;  // no flip -- drop to avoid re-emit chatter
        }
        cached = enabled;
        model_.setEquipmentEnabled(mapping.entityId, enabled);
        return;
    }

    case EquipmentSupplyLevel: {
        // Dedup by integer-percent equality (the model stores
        // supplyLevel as int). Suppresses re-emits when a noisy
        // sensor reports the same percent across consecutive polls;
        // the model side never sees redundant setter calls.
        const int scaled = static_cast<int>(
            static_cast<float>(rawValue) * mapping.scale);
        if (mapping.entityId < kMaxTrackedEquipment) {
            auto& cached = lastSupplyLevel_[mapping.entityId];
            if (cached.has_value() && *cached == scaled) {
                return;
            }
            cached = scaled;
        }
        model_.setEquipmentSupplyLevel(mapping.entityId, scaled);
        return;
    }

    case QualityPassRate: {
        // Pass rate stays float on the model side. Dedup uses exact
        // float compare -- safe because the same raw register + same
        // scale produces the same float bit-for-bit, and a moving
        // sensor reading naturally drifts away.
        const float scaled =
            static_cast<float>(rawValue) * mapping.scale;
        if (mapping.entityId < kMaxTrackedQuality) {
            auto& cached = lastPassRate_[mapping.entityId];
            if (cached.has_value() && *cached == scaled) {
                return;
            }
            cached = scaled;
        }
        model_.setQualityPassRate(mapping.entityId, scaled);
        return;
    }
    }
}

}  // namespace app::integration::modbus
