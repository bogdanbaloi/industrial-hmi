// Tests for app::integration::modbus::ModbusIngestBridge and
// ModbusRegisterMap.
//
// The bridge takes (mapping, raw uint16 value) from the poll loop
// and dispatches to ProductionModel setters. We verify:
//   * the register map preserves insertion order + size + emptiness
//   * EquipmentEnabled maps 0 -> off, non-zero -> on
//   * repeated identical readings dedup (only the first fires)
//   * out-of-range entity IDs are silently dropped
//   * multiple entities are tracked independently
//
// No socket, no thread, no poll loop -- pure dispatch logic against
// the same MockProductionModel the other ingest-bridge tests use.

#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusRegisterMap.h"

#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using app::integration::modbus::FieldKind;
using app::integration::modbus::ModbusIngestBridge;
using app::integration::modbus::ModbusRegisterMap;
using app::integration::modbus::RegisterMapping;
using app::integration::modbus::RegisterType;
using app::test::MockProductionModel;

namespace {

RegisterMapping equipmentEnabledMapping(std::uint32_t entityId,
                                        std::uint16_t address = 0x0001) {
    RegisterMapping m;
    m.slaveId  = 1;
    m.type     = RegisterType::HoldingRegister;
    m.address  = address;
    m.field    = FieldKind::EquipmentEnabled;
    m.entityId = entityId;
    return m;
}

}  // namespace

// ===== ModbusRegisterMap =========================================

TEST(ModbusRegisterMapTest, StartsEmpty) {
    ModbusRegisterMap map;
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0U);
    EXPECT_TRUE(map.entries().empty());
}

TEST(ModbusRegisterMapTest, AddAppendsAndPreservesInsertionOrder) {
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 0x0100));
    map.add(equipmentEnabledMapping(1, 0x0200));
    map.add(equipmentEnabledMapping(2, 0x0300));

    ASSERT_EQ(map.size(), 3U);
    const auto entries = map.entries();
    EXPECT_EQ(entries[0].entityId, 0U);
    EXPECT_EQ(entries[0].address,  0x0100U);
    EXPECT_EQ(entries[1].entityId, 1U);
    EXPECT_EQ(entries[2].address,  0x0300U);
}

TEST(ModbusRegisterMapTest, AllowsDuplicateAddressEntries) {
    // Two entries on the same address are legal: mirror a register
    // into two domain entities. The bridge dedupes per (entity,
    // field), not per address, so this is supported by design.
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0, 0x0100));
    map.add(equipmentEnabledMapping(1, 0x0100));
    EXPECT_EQ(map.size(), 2U);
}

TEST(ModbusRegisterMapTest, ClearWipesEntries) {
    ModbusRegisterMap map;
    map.add(equipmentEnabledMapping(0));
    map.add(equipmentEnabledMapping(1));
    map.clear();
    EXPECT_TRUE(map.empty());
}

// ===== ModbusIngestBridge: EquipmentEnabled =======================

TEST(ModbusIngestBridgeTest, ZeroValueDisablesEquipment) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentEnabled(0U, false)).Times(1);
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 0);
}

TEST(ModbusIngestBridgeTest, NonZeroValueEnablesEquipment) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentEnabled(2U, true)).Times(1);
    bridge.onRegisterChanged(equipmentEnabledMapping(2), 0x0001);
}

TEST(ModbusIngestBridgeTest, AnyNonZeroValueEnables) {
    // The convention is "0 -> off, anything else -> on", so a coil
    // arriving as 0xFF00 (Modbus's traditional ON sentinel) or any
    // other non-zero word must drive the same enabled = true.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);

    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 0xFF00);

    EXPECT_CALL(model, setEquipmentEnabled(1U, true)).Times(1);
    bridge.onRegisterChanged(equipmentEnabledMapping(1), 0xFFFF);

    EXPECT_CALL(model, setEquipmentEnabled(2U, true)).Times(1);
    bridge.onRegisterChanged(equipmentEnabledMapping(2), 42);
}

TEST(ModbusIngestBridgeTest, RepeatedSameValueDedupsAfterFirst) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);

    // Only ONE call expected even though we push 5 identical reads.
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    for (int i = 0; i < 5; ++i) {
        bridge.onRegisterChanged(equipmentEnabledMapping(0), 1);
    }
}

TEST(ModbusIngestBridgeTest, FlipFiresAgain) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);

    {
        testing::InSequence seq;
        EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
        EXPECT_CALL(model, setEquipmentEnabled(0U, false)).Times(1);
        EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    }

    bridge.onRegisterChanged(equipmentEnabledMapping(0), 1);
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 1);  // dedup
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 0);
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 0);  // dedup
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 7);  // back ON
}

TEST(ModbusIngestBridgeTest, IndependentTrackingPerEntity) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);

    // Two entities flipping independently must each get their own
    // setter call. No cross-talk between cache slots.
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(1U, false)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(0U, false)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(1U, true)).Times(1);

    bridge.onRegisterChanged(equipmentEnabledMapping(0), 1);   // 0 ON
    bridge.onRegisterChanged(equipmentEnabledMapping(1), 0);   // 1 OFF
    bridge.onRegisterChanged(equipmentEnabledMapping(0), 0);   // 0 OFF
    bridge.onRegisterChanged(equipmentEnabledMapping(1), 99);  // 1 ON
}

TEST(ModbusIngestBridgeTest, OutOfRangeEntityIdIsSilentlyDropped) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);

    // entityId 16 is beyond the kMaxTrackedEquipment cap. Bridge
    // mustn't crash; it just refuses to fire the setter so a
    // misconfigured JSON map can't take the HMI down.
    EXPECT_CALL(model, setEquipmentEnabled(testing::_, testing::_)).Times(0);
    bridge.onRegisterChanged(equipmentEnabledMapping(16), 1);
    bridge.onRegisterChanged(equipmentEnabledMapping(99), 0);
    bridge.onRegisterChanged(equipmentEnabledMapping(
        std::numeric_limits<std::uint32_t>::max()), 1);
}

TEST(ModbusIngestBridgeTest, InputRegisterTypeRoutesIdentically) {
    // Bridge cares about FieldKind, not RegisterType (the poll loop
    // already chose FC03/FC04 before dispatching). An input-register
    // mapping flips the same enabled bit.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);

    auto m = equipmentEnabledMapping(0);
    m.type = RegisterType::InputRegister;
    bridge.onRegisterChanged(m, 1);
}

// ===== ModbusIngestBridge: EquipmentSupplyLevel ==================

namespace {

RegisterMapping supplyLevelMapping(std::uint32_t entityId,
                                   float scale = 1.0f) {
    RegisterMapping m;
    m.slaveId  = 1;
    m.type     = RegisterType::HoldingRegister;
    m.address  = 0x0010;
    m.field    = FieldKind::EquipmentSupplyLevel;
    m.entityId = entityId;
    m.scale    = scale;
    return m;
}

RegisterMapping passRateMapping(std::uint32_t entityId,
                                float scale = 1.0f) {
    RegisterMapping m;
    m.slaveId  = 1;
    m.type     = RegisterType::HoldingRegister;
    m.address  = 0x0020;
    m.field    = FieldKind::QualityPassRate;
    m.entityId = entityId;
    m.scale    = scale;
    return m;
}

}  // namespace

TEST(ModbusIngestBridgeTest, SupplyLevelPassesScaleOneThrough) {
    // Direct-percent PLC: raw register IS the percent already.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 73)).Times(1);
    bridge.onRegisterChanged(supplyLevelMapping(0, 1.0f), 73);
}

TEST(ModbusIngestBridgeTest, SupplyLevelAppliesScaleZeroPointOne) {
    // Fixed-point PLC: raw 850 means 85.0%, so scale 0.1 -> int 85.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentSupplyLevel(1U, 85)).Times(1);
    bridge.onRegisterChanged(supplyLevelMapping(1, 0.1f), 850);
}

TEST(ModbusIngestBridgeTest, SupplyLevelDedupsRepeatedSameValue) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    // First call only -- subsequent identical reads collapse.
    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 42)).Times(1);
    for (int i = 0; i < 5; ++i) {
        bridge.onRegisterChanged(supplyLevelMapping(0), 42);
    }
}

TEST(ModbusIngestBridgeTest, SupplyLevelFiresAgainAfterChange) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    {
        testing::InSequence seq;
        EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 50)).Times(1);
        EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 60)).Times(1);
        EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 50)).Times(1);
    }
    bridge.onRegisterChanged(supplyLevelMapping(0), 50);
    bridge.onRegisterChanged(supplyLevelMapping(0), 50);  // dedup
    bridge.onRegisterChanged(supplyLevelMapping(0), 60);
    bridge.onRegisterChanged(supplyLevelMapping(0), 50);
}

TEST(ModbusIngestBridgeTest, SupplyLevelTracksEntitiesIndependently) {
    // Cross-entity dedup must NOT pollute -- entity 0's reading
    // doesn't suppress entity 1's identical reading.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 75)).Times(1);
    EXPECT_CALL(model, setEquipmentSupplyLevel(1U, 75)).Times(1);
    bridge.onRegisterChanged(supplyLevelMapping(0), 75);
    bridge.onRegisterChanged(supplyLevelMapping(1), 75);
}

TEST(ModbusIngestBridgeTest, SupplyLevelOutOfRangeStillForwards) {
    // Out-of-range ids skip the dedup cache (no array slot) but
    // still forward to the model so it can enforce its own bounds.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setEquipmentSupplyLevel(99U, 50)).Times(1);
    bridge.onRegisterChanged(supplyLevelMapping(99), 50);
}

// ===== ModbusIngestBridge: QualityPassRate ========================

TEST(ModbusIngestBridgeTest, PassRatePassesScaleOneThrough) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setQualityPassRate(0U, 95.0f)).Times(1);
    bridge.onRegisterChanged(passRateMapping(0, 1.0f), 95);
}

TEST(ModbusIngestBridgeTest, PassRateAppliesScaleZeroPointOne) {
    // Raw 987 with scale 0.1 -> ~98.7%. Float matcher with epsilon
    // because 987.0f * 0.1f doesn't land bit-exact on 98.7f
    // (the literal 98.7f rounds toward a different mantissa than
    // the multiplication result). FloatEq tolerates 4 ULPs.
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model,
                setQualityPassRate(1U, testing::FloatEq(98.7f))).Times(1);
    bridge.onRegisterChanged(passRateMapping(1, 0.1f), 987);
}

TEST(ModbusIngestBridgeTest, PassRateDedupsRepeatedSameValue) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setQualityPassRate(0U, 95.5f)).Times(1);
    for (int i = 0; i < 5; ++i) {
        bridge.onRegisterChanged(passRateMapping(0, 0.1f), 955);
    }
}

TEST(ModbusIngestBridgeTest, PassRateAcrossDifferentScalesEmitsAgain) {
    // Same raw value, different scale -> different output -> two
    // separate setter calls. Edge case for operators who reconfigure
    // the scale at runtime (rare but worth pinning).
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setQualityPassRate(0U, 100.0f)).Times(1);
    EXPECT_CALL(model, setQualityPassRate(0U, 10.0f)).Times(1);
    bridge.onRegisterChanged(passRateMapping(0, 1.0f), 100);
    bridge.onRegisterChanged(passRateMapping(0, 0.1f), 100);
}

TEST(ModbusIngestBridgeTest, PassRateTracksEntitiesIndependently) {
    MockProductionModel model;
    ModbusIngestBridge bridge(model);
    EXPECT_CALL(model, setQualityPassRate(0U, 95.0f)).Times(1);
    EXPECT_CALL(model, setQualityPassRate(1U, 95.0f)).Times(1);
    bridge.onRegisterChanged(passRateMapping(0), 95);
    bridge.onRegisterChanged(passRateMapping(1), 95);
}
