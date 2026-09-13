// [utest->req~integration-007~1]
// Covers REQ-INTEGRATION-007 (HTTP/REST backend): the shared status-JSON
// builder that both the TCP `status` command and the HTTP `/status` route
// serve. Extracted so the two frontends cannot drift.
//
// Pure logic -- no I/O, no GTK, no sockets. A gmock ProductionModel drives
// getState() and the produced JSON is asserted field by field.

#include "src/integration/StatusJson.h"

#include "src/model/ProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using ::testing::Return;

using app::integration::buildStatusJson;
using app::model::ProductionModel;
using app::model::SystemState;

namespace {

class MockProductionModel : public ProductionModel {
public:
    MOCK_METHOD(void, onEquipmentStatusChanged, (EquipmentCallback), (override));
    MOCK_METHOD(void, onActuatorStatusChanged, (ActuatorCallback), (override));
    MOCK_METHOD(void, onQualityCheckpointChanged, (QualityCheckpointCallback),
                (override));
    MOCK_METHOD(void, onWorkUnitChanged, (WorkUnitCallback), (override));
    MOCK_METHOD(void, onSystemStateChanged, (StateCallback), (override));

    MOCK_METHOD(void, startProduction, (), (override));
    MOCK_METHOD(void, stopProduction, (), (override));
    MOCK_METHOD(void, resetSystem, (), (override));
    MOCK_METHOD(void, startCalibration, (), (override));
    MOCK_METHOD(void, setEquipmentEnabled, (uint32_t, bool), (override));
    MOCK_METHOD(void, loadProduct,
                (const app::model::Product&, const app::model::Recipe&),
                (override));
    MOCK_METHOD(void, setEquipmentSupplyLevel, (uint32_t, int), (override));
    MOCK_METHOD(void, setQualityPassRate, (uint32_t, float), (override));

    MOCK_METHOD(SystemState, getState, (), (const, override));
    MOCK_METHOD(app::model::QualityCheckpoint, getQualityCheckpoint,
                (uint32_t), (const, override));
    MOCK_METHOD((std::vector<app::model::QualityCheckpoint>),
                getQualityCheckpoints, (), (const, override));
    MOCK_METHOD(app::model::WorkUnit, getWorkUnit, (), (const, override));
    MOCK_METHOD(std::string, lastFaultReason, (), (const, override));
    MOCK_METHOD(app::model::OeeMetrics, oeeSnapshot, (), (const, override));
};

TEST(StatusJsonTest, RunningStateReportsRunningTrue) {
    MockProductionModel model;
    EXPECT_CALL(model, getState()).WillRepeatedly(Return(SystemState::RUNNING));

    const nlohmann::json j = buildStatusJson(model);

    EXPECT_EQ(j.at("state").get<std::string>(), "running");
    EXPECT_TRUE(j.at("running").get<bool>());
}

TEST(StatusJsonTest, IdleStateReportsRunningFalse) {
    MockProductionModel model;
    EXPECT_CALL(model, getState()).WillRepeatedly(Return(SystemState::IDLE));

    const nlohmann::json j = buildStatusJson(model);

    EXPECT_EQ(j.at("state").get<std::string>(), "idle");
    EXPECT_FALSE(j.at("running").get<bool>());
}

TEST(StatusJsonTest, ErrorAndCalibrationStatesMapToTheirNames) {
    MockProductionModel model;

    EXPECT_CALL(model, getState()).WillRepeatedly(Return(SystemState::ERROR));
    EXPECT_EQ(buildStatusJson(model).at("state").get<std::string>(), "error");

    EXPECT_CALL(model, getState())
        .WillRepeatedly(Return(SystemState::CALIBRATION));
    EXPECT_EQ(buildStatusJson(model).at("state").get<std::string>(),
              "calibration");
    // Only RUNNING is "running".
    EXPECT_FALSE(buildStatusJson(model).at("running").get<bool>());
}

TEST(StatusJsonTest, ObjectHasExactlyStateAndRunningKeys) {
    MockProductionModel model;
    EXPECT_CALL(model, getState()).WillRepeatedly(Return(SystemState::IDLE));

    const nlohmann::json j = buildStatusJson(model);

    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j.size(), 2U);
    EXPECT_TRUE(j.contains("state"));
    EXPECT_TRUE(j.contains("running"));
}

}  // namespace
