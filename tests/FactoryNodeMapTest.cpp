// Tests for app::integration::opcua::FactoryNodeMap.
//
// The map is the manufacturing-domain strategy that translates
// ProductionModel signals into OPC-UA address-space writes. We verify
// two things:
//
//   1. registerNodes builds the static skeleton (Factory root, the
//      three collection folders, the WorkUnit subtree, the State
//      variable). Per-equipment / per-checkpoint subtrees are
//      LAZY -- they appear only when the corresponding signal fires.
//
//   2. Each model signal lands as the right writeXxx call on the right
//      browse-path. A regression that flips Float vs Int32 or maps a
//      checkpoint id onto an equipment subtree would silently corrupt
//      the address space (clients still see numbers, just wrong ones).
//
// Mocks: MockOpcUaServer (records every add* + write*) and
// MockProductionModel (capture-callback pattern via SaveArg).

#include "src/integration/opcua/FactoryNodeMap.h"

#include "src/core/LoggerImpl.h"
#include "src/model/ProductionModel.h"
#include "src/model/ProductionTypes.h"
#include "tests/MockOpcUaServer.h"
#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

using app::integration::opcua::FactoryNodeMap;
using app::integration::opcua::testing::MockOpcUaServer;
using app::model::EquipmentStatus;
using app::model::QualityCheckpoint;
using app::model::SystemState;
using app::model::WorkUnit;
using app::test::MockProductionModel;

using ::testing::_;
using ::testing::Eq;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace {

/// Captures the four signal callbacks the map registers in `wire()`.
/// Tests then fire the captured callback to drive the map.
struct CapturedCallbacks {
    app::model::ProductionModel::EquipmentCallback equipment;
    app::model::ProductionModel::QualityCheckpointCallback quality;
    app::model::ProductionModel::WorkUnitCallback workUnit;
    app::model::ProductionModel::StateCallback systemState;
};

/// Wire EXPECT_CALL on the four subscriptions so the test can drive
/// callbacks back into the map. Uses SaveArg to stash a copy of each
/// std::function. ProductionModel's contract documents that
/// subscriptions are append-only and never invoked from the
/// subscription call itself, so a single SaveArg is sufficient.
void primeSubscriptions(MockProductionModel& model,
                        CapturedCallbacks& cbs) {
    EXPECT_CALL(model, onEquipmentStatusChanged(_))
        .WillOnce(SaveArg<0>(&cbs.equipment));
    EXPECT_CALL(model, onQualityCheckpointChanged(_))
        .WillOnce(SaveArg<0>(&cbs.quality));
    EXPECT_CALL(model, onWorkUnitChanged(_))
        .WillOnce(SaveArg<0>(&cbs.workUnit));
    EXPECT_CALL(model, onSystemStateChanged(_))
        .WillOnce(SaveArg<0>(&cbs.systemState));
}

class FactoryNodeMapTest : public ::testing::Test {
protected:
    MockOpcUaServer        server_;
    MockProductionModel    model_;
    app::core::Logger logger_{std::make_unique<app::core::ConsoleLogger>()};
};

TEST_F(FactoryNodeMapTest, RegisterNodesBuildsStaticSkeleton) {
    // Static skeleton: Factory root, top-level State, the three
    // collection folders, and the WorkUnit subtree. Per-id subtrees
    // are NOT created here -- those come on first signal.
    EXPECT_CALL(server_, addObject(Eq(std::string_view{"Factory"})));
    EXPECT_CALL(server_,
                addInt32Variable(Eq(std::string_view{"Factory/State"}), 0));
    EXPECT_CALL(server_,
                addObject(Eq(std::string_view{"Factory/EquipmentLines"})));
    EXPECT_CALL(server_,
                addObject(Eq(std::string_view{"Factory/QualityCheckpoints"})));
    EXPECT_CALL(server_,
                addObject(Eq(std::string_view{"Factory/WorkUnit"})));
    EXPECT_CALL(server_, addStringVariable(
                Eq(std::string_view{"Factory/WorkUnit/Id"}), _));
    EXPECT_CALL(server_, addStringVariable(
                Eq(std::string_view{"Factory/WorkUnit/ProductId"}), _));
    EXPECT_CALL(server_, addInt32Variable(
                Eq(std::string_view{"Factory/WorkUnit/CompletedOperations"}),
                0));
    EXPECT_CALL(server_, addInt32Variable(
                Eq(std::string_view{"Factory/WorkUnit/TotalOperations"}), 0));

    FactoryNodeMap map(model_, logger_);
    map.registerNodes(server_);
}

TEST_F(FactoryNodeMapTest, EquipmentSignalCreatesSubtreeAndWritesValues) {
    CapturedCallbacks cbs;
    primeSubscriptions(model_, cbs);

    FactoryNodeMap map(model_, logger_);
    map.wire(server_);
    ASSERT_TRUE(static_cast<bool>(cbs.equipment));

    // First-time signal: subtree comes into existence (5 add* calls
    // because addObject + 3 variables = 4, wait actually 4 nodes:
    // Line0 + Status + SupplyLevel + Message).
    EXPECT_CALL(server_,
                addObject(Eq(std::string_view{"Factory/EquipmentLines/Line0"})));
    EXPECT_CALL(server_, addInt32Variable(
        Eq(std::string_view{"Factory/EquipmentLines/Line0/Status"}), 0));
    EXPECT_CALL(server_, addInt32Variable(
        Eq(std::string_view{"Factory/EquipmentLines/Line0/SupplyLevel"}), 0));
    EXPECT_CALL(server_, addStringVariable(
        Eq(std::string_view{"Factory/EquipmentLines/Line0/Message"}), _));

    // Then the actual write* on the same paths with the model values.
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/EquipmentLines/Line0/Status"}), 2));
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/EquipmentLines/Line0/SupplyLevel"}),
        87));
    EXPECT_CALL(server_, writeString(
        Eq(std::string_view{"Factory/EquipmentLines/Line0/Message"}),
        Eq(std::string_view{"running normally"})));

    EquipmentStatus status;
    status.equipmentId = 0;
    status.status = 2;
    status.supplyLevel = 87;
    status.message = "running normally";
    cbs.equipment(status);
}

TEST_F(FactoryNodeMapTest, QualityCheckpointSignalRoutesToFloatPassRate) {
    CapturedCallbacks cbs;
    primeSubscriptions(model_, cbs);

    FactoryNodeMap map(model_, logger_);
    map.wire(server_);
    ASSERT_TRUE(static_cast<bool>(cbs.quality));

    // Subtree creation calls (one-shot for this id).
    EXPECT_CALL(server_, addObject(_)).Times(1);
    EXPECT_CALL(server_, addStringVariable(
        Eq(std::string_view{"Factory/QualityCheckpoints/Checkpoint5/Name"}),
        _));
    EXPECT_CALL(server_, addInt32Variable(
        Eq(std::string_view{"Factory/QualityCheckpoints/Checkpoint5/Status"}),
        0));
    EXPECT_CALL(server_, addFloatVariable(
        Eq(std::string_view{"Factory/QualityCheckpoints/Checkpoint5/PassRate"}),
        100.0F));
    EXPECT_CALL(server_, addInt32Variable(
        Eq(std::string_view{
            "Factory/QualityCheckpoints/Checkpoint5/UnitsInspected"}),
        0));
    EXPECT_CALL(server_, addInt32Variable(
        Eq(std::string_view{
            "Factory/QualityCheckpoints/Checkpoint5/DefectsFound"}),
        0));

    // The CRITICAL assertion of this test: passRate is Float, not Int32.
    // A regression that demoted to Int32 would lose precision and
    // confuse OPC-UA clients that introspect the data type.
    EXPECT_CALL(server_, writeString(
        Eq(std::string_view{"Factory/QualityCheckpoints/Checkpoint5/Name"}),
        Eq(std::string_view{"Final QC"})));
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/QualityCheckpoints/Checkpoint5/Status"}),
        1));
    EXPECT_CALL(server_, writeFloat(
        Eq(std::string_view{"Factory/QualityCheckpoints/Checkpoint5/PassRate"}),
        ::testing::FloatEq(94.5F)));
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{
            "Factory/QualityCheckpoints/Checkpoint5/UnitsInspected"}),
        200));
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{
            "Factory/QualityCheckpoints/Checkpoint5/DefectsFound"}),
        11));

    QualityCheckpoint qc;
    qc.checkpointId = 5;
    qc.name = "Final QC";
    qc.status = 1;
    qc.passRate = 94.5F;
    qc.unitsInspected = 200;
    qc.defectsFound = 11;
    cbs.quality(qc);
}

TEST_F(FactoryNodeMapTest, SystemStateSignalWritesIntegerEncoding) {
    CapturedCallbacks cbs;
    primeSubscriptions(model_, cbs);

    FactoryNodeMap map(model_, logger_);
    map.wire(server_);
    ASSERT_TRUE(static_cast<bool>(cbs.systemState));

    // SystemState is an enum class -- make sure it lands as Int32 with
    // the underlying value preserved (RUNNING == 1).
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/State"}), 1));

    cbs.systemState(SystemState::RUNNING);
}

TEST_F(FactoryNodeMapTest, WorkUnitSignalWritesAllFields) {
    CapturedCallbacks cbs;
    primeSubscriptions(model_, cbs);

    FactoryNodeMap map(model_, logger_);
    map.wire(server_);
    ASSERT_TRUE(static_cast<bool>(cbs.workUnit));

    EXPECT_CALL(server_, writeString(
        Eq(std::string_view{"Factory/WorkUnit/Id"}),
        Eq(std::string_view{"WU-2024-0042"})));
    EXPECT_CALL(server_, writeString(
        Eq(std::string_view{"Factory/WorkUnit/ProductId"}),
        Eq(std::string_view{"P-1138"})));
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/WorkUnit/CompletedOperations"}), 3));
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/WorkUnit/TotalOperations"}), 5));

    WorkUnit wu;
    wu.workUnitId = "WU-2024-0042";
    wu.productId = "P-1138";
    wu.completedOperations = 3;
    wu.totalOperations = 5;
    cbs.workUnit(wu);
}

TEST_F(FactoryNodeMapTest, UnwireSilencesSubsequentCallbacks) {
    CapturedCallbacks cbs;
    primeSubscriptions(model_, cbs);

    FactoryNodeMap map(model_, logger_);
    map.wire(server_);
    ASSERT_TRUE(static_cast<bool>(cbs.systemState));

    // Pre-unwire callback fires normally.
    EXPECT_CALL(server_, writeInt32(
        Eq(std::string_view{"Factory/State"}), 1));
    cbs.systemState(SystemState::RUNNING);

    // After unwire, no further server calls should happen even if
    // the model fires (callbacks captured before unwire stay alive
    // because ProductionModel doesn't expose removal -- the wired_
    // flag is what guards them).
    map.unwire();
    EXPECT_CALL(server_, writeInt32(_, _)).Times(0);
    cbs.systemState(SystemState::ERROR);
}

}  // namespace
