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
    }
}

}  // namespace app::integration::modbus
