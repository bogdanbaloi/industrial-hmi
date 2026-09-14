// [utest->req~integration-008~1]
// Covers REQ-INTEGRATION-008 (HTTP/REST production KPIs): the shared
// production-metrics JSON builder that both the MCP `production_metrics` tool
// and the HTTP `GET /production` route delegate to. Extracted so the two
// consumers cannot drift.
//
// Pure logic -- no I/O, no GTK, no sockets. A gmock ProductionModel drives
// getWorkUnit() / oeeSnapshot() and the produced JSON is asserted field by
// field, including the omit-at-non-positive-throughput behaviour.

#include "src/integration/ProductionMetricsJson.h"

#include "src/model/ProductionModel.h"
#include "src/model/ProductionTypes.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using ::testing::Return;

using app::integration::buildProductionMetricsJson;
using app::model::ProductionModel;

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

    MOCK_METHOD(app::model::SystemState, getState, (), (const, override));
    MOCK_METHOD(app::model::QualityCheckpoint, getQualityCheckpoint,
                (uint32_t), (const, override));
    MOCK_METHOD((std::vector<app::model::QualityCheckpoint>),
                getQualityCheckpoints, (), (const, override));
    MOCK_METHOD(app::model::WorkUnit, getWorkUnit, (), (const, override));
    MOCK_METHOD(std::string, lastFaultReason, (), (const, override));
    MOCK_METHOD(app::model::OeeMetrics, oeeSnapshot, (), (const, override));
};

app::model::WorkUnit workUnitWith(double throughputUph) {
    app::model::WorkUnit unit;
    unit.throughputUnitsPerHour = throughputUph;
    return unit;
}

app::model::OeeMetrics oeeWith(float oeePct) {
    app::model::OeeMetrics metrics;
    metrics.oeePct = oeePct;
    return metrics;
}

TEST(ProductionMetricsJsonTest, ReportsThroughputAndOee) {
    MockProductionModel model;
    EXPECT_CALL(model, getWorkUnit())
        .WillRepeatedly(Return(workUnitWith(120.0)));
    EXPECT_CALL(model, oeeSnapshot()).WillRepeatedly(Return(oeeWith(85.0F)));

    const nlohmann::json j = buildProductionMetricsJson(model);

    EXPECT_DOUBLE_EQ(j.at("throughputUph").get<double>(), 120.0);
    EXPECT_FLOAT_EQ(j.at("oeePct").get<float>(), 85.0F);
}

TEST(ProductionMetricsJsonTest, DerivesMinutesPerUnitFromThroughput) {
    MockProductionModel model;
    // 120 units/hour -> one unit every 0.5 minutes.
    EXPECT_CALL(model, getWorkUnit())
        .WillRepeatedly(Return(workUnitWith(120.0)));
    EXPECT_CALL(model, oeeSnapshot()).WillRepeatedly(Return(oeeWith(85.0F)));

    const nlohmann::json j = buildProductionMetricsJson(model);

    ASSERT_TRUE(j.contains("minutesPerUnit"));
    EXPECT_DOUBLE_EQ(j.at("minutesPerUnit").get<double>(), 0.5);
}

TEST(ProductionMetricsJsonTest, OmitsMinutesPerUnitWhenThroughputIsZero) {
    MockProductionModel model;
    EXPECT_CALL(model, getWorkUnit()).WillRepeatedly(Return(workUnitWith(0.0)));
    EXPECT_CALL(model, oeeSnapshot()).WillRepeatedly(Return(oeeWith(40.0F)));

    const nlohmann::json j = buildProductionMetricsJson(model);

    EXPECT_DOUBLE_EQ(j.at("throughputUph").get<double>(), 0.0);
    EXPECT_FALSE(j.contains("minutesPerUnit"));
}

TEST(ProductionMetricsJsonTest, OmitsMinutesPerUnitWhenThroughputIsNegative) {
    MockProductionModel model;
    EXPECT_CALL(model, getWorkUnit())
        .WillRepeatedly(Return(workUnitWith(-5.0)));
    EXPECT_CALL(model, oeeSnapshot()).WillRepeatedly(Return(oeeWith(40.0F)));

    const nlohmann::json j = buildProductionMetricsJson(model);

    EXPECT_FALSE(j.contains("minutesPerUnit"));
}

TEST(ProductionMetricsJsonTest, ObjectHasThroughputAndOeeKeys) {
    MockProductionModel model;
    EXPECT_CALL(model, getWorkUnit()).WillRepeatedly(Return(workUnitWith(0.0)));
    EXPECT_CALL(model, oeeSnapshot()).WillRepeatedly(Return(oeeWith(0.0F)));

    const nlohmann::json j = buildProductionMetricsJson(model);

    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.contains("throughputUph"));
    EXPECT_TRUE(j.contains("oeePct"));
}

}  // namespace
