// [utest->req~dashboard-001~1]
// [utest->req~quality-001~1]
// Covers REQ-DASHBOARD-001 (equipment cards reflect live state),
//        REQ-QUALITY-001 (pass-rate classification).
//
// INTEGRATION test: a real DashboardPresenter observing a real
// ProductionModel (MirrorModel), with a real ViewObserver capturing the
// emitted view models. No MockProductionModel.
//
// DashboardPresenterTest drives the presenter by SaveArg-capturing the
// callbacks a MockProductionModel records, then invoking them by hand.
// This test instead lets a real model fire its real observer plumbing:
// a setter on the model must travel through the model's dispatch, the
// presenter's view-model builder, and land on the observer -- the whole
// chain the production dashboard relies on.
//
// MirrorModel (not the SimulatedModel singleton) keeps each test
// isolated and lets the presenter's subscription die with the fixture.

#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ViewObserver.h"
#include "src/model/MirrorModel.h"

#include <memory>
#include <optional>

#include <gtest/gtest.h>

namespace {

using app::DashboardPresenter;
using app::model::MirrorModel;
using app::presenter::QualityCheckpointStatus;

/// Records the latest view model emitted on the hooks this test checks.
class CapturingObserver : public app::ViewObserver {
public:
    std::optional<app::presenter::EquipmentCardViewModel>     equipment;
    std::optional<app::presenter::QualityCheckpointViewModel> quality;

    void onEquipmentCardChanged(
        const app::presenter::EquipmentCardViewModel& vm) override {
        equipment = vm;
    }
    void onQualityCheckpointChanged(
        const app::presenter::QualityCheckpointViewModel& vm) override {
        quality = vm;
    }
};

class DashboardPresenterIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        presenter_ = std::make_unique<DashboardPresenter>(model_);
        presenter_->addObserver(&observer_);
        presenter_->initialize();  // subscribes to model_'s real signals
    }

    void TearDown() override {
        presenter_->removeObserver(&observer_);
    }

    // Declared so presenter_ (last) is destroyed before model_.
    MirrorModel                         model_;
    CapturingObserver                   observer_;
    std::unique_ptr<DashboardPresenter> presenter_;
};

TEST_F(DashboardPresenterIntegrationTest,
       LowPassRateFromModelClassifiesCriticalOnObserver) {
    // 80% is below the 90% warning floor -> Critical (REQ-QUALITY-001).
    model_.setQualityPassRate(/*checkpointId=*/2, /*rate=*/80.0F);

    ASSERT_TRUE(observer_.quality.has_value())
        << "presenter never emitted a quality view model";
    EXPECT_EQ(observer_.quality->checkpointId, 2U);
    EXPECT_FLOAT_EQ(observer_.quality->passRate, 80.0F);
    EXPECT_EQ(observer_.quality->status, QualityCheckpointStatus::Critical);
}

TEST_F(DashboardPresenterIntegrationTest,
       HighPassRateFromModelClassifiesPassing) {
    model_.setQualityPassRate(/*checkpointId=*/0, /*rate=*/99.0F);

    ASSERT_TRUE(observer_.quality.has_value());
    EXPECT_EQ(observer_.quality->status, QualityCheckpointStatus::Passing);
}

TEST_F(DashboardPresenterIntegrationTest,
       EquipmentChangeFromModelReachesObserver) {
    // A supply-level change on the model must drive an equipment card
    // view model out to the observer (the presenter renders the supply
    // into the human-readable `consumables` string).
    model_.setEquipmentSupplyLevel(/*equipmentId=*/1, /*level=*/72);

    ASSERT_TRUE(observer_.equipment.has_value())
        << "presenter never emitted an equipment view model";
    EXPECT_EQ(observer_.equipment->equipmentId, 1U);
    EXPECT_FALSE(observer_.equipment->consumables.empty());
}

}  // namespace
