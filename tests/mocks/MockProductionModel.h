#pragma once

#include "src/model/ProductionModel.h"

#include <gmock/gmock.h>

namespace app::test {

/// gmock-backed implementation of ProductionModel for DashboardPresenter
/// tests. Lets a test set EXPECT_CALL on commands, capture subscription
/// callbacks (via SaveArg) so it can fire fake signals, and stub query
/// results.
class MockProductionModel : public model::ProductionModel {
public:
    // Subscriptions
    MOCK_METHOD(void, onEquipmentStatusChanged, (EquipmentCallback), (override));
    MOCK_METHOD(void, onActuatorStatusChanged, (ActuatorCallback), (override));
    MOCK_METHOD(void, onQualityCheckpointChanged, (QualityCheckpointCallback), (override));
    MOCK_METHOD(void, onWorkUnitChanged, (WorkUnitCallback), (override));
    MOCK_METHOD(void, onSystemStateChanged, (StateCallback), (override));

    // Commands
    MOCK_METHOD(void, startProduction, (), (override));
    MOCK_METHOD(void, stopProduction, (), (override));
    MOCK_METHOD(void, resetSystem, (), (override));
    MOCK_METHOD(void, startCalibration, (), (override));
    MOCK_METHOD(void, setEquipmentEnabled, (uint32_t equipmentId, bool enabled), (override));

    // Queries
    MOCK_METHOD(model::SystemState, getState, (), (const, override));
    MOCK_METHOD(model::QualityCheckpoint, getQualityCheckpoint, (uint32_t id), (const, override));
    MOCK_METHOD(model::WorkUnit, getWorkUnit, (), (const, override));
};

}  // namespace app::test
