#include "src/integration/opcua/FactoryNodeMap.h"

#include "src/core/LoggerBase.h"
#include "src/integration/opcua/OpcUaCommandSink.h"
#include "src/integration/opcua/OpcUaServer.h"
#include "src/model/ProductionModel.h"

#include <format>
#include <string>

namespace app::integration::opcua {

namespace {

constexpr const char* kRoot              = "Factory";
constexpr const char* kEquipmentRoot     = "Factory/EquipmentLines";
constexpr const char* kCheckpointRoot    = "Factory/QualityCheckpoints";
constexpr const char* kWorkUnitRoot      = "Factory/WorkUnit";

std::string equipmentPath(std::uint32_t id, std::string_view leaf) {
    return std::format("{}/Line{}/{}", kEquipmentRoot, id, leaf);
}

std::string checkpointPath(std::uint32_t id, std::string_view leaf) {
    return std::format("{}/Checkpoint{}/{}", kCheckpointRoot, id, leaf);
}

std::string workUnitPath(std::string_view leaf) {
    return std::format("{}/{}", kWorkUnitRoot, leaf);
}

/// Lazily create a per-id Object subtree the first time we see an id.
/// Idempotent on the server side: a duplicate `addObject` is rejected
/// without side effects, so calling this for every event is safe.
void ensureEquipmentSubtree(OpcUaServer& server, std::uint32_t id) {
    const auto base = std::format("{}/Line{}", kEquipmentRoot, id);
    (void)server.addObject(base);
    (void)server.addInt32Variable(base + "/Status", 0);
    (void)server.addInt32Variable(base + "/SupplyLevel", 0);
    (void)server.addStringVariable(base + "/Message", "");
}

void ensureCheckpointSubtree(OpcUaServer& server, std::uint32_t id) {
    const auto base = std::format("{}/Checkpoint{}", kCheckpointRoot, id);
    (void)server.addObject(base);
    (void)server.addStringVariable(base + "/Name", "");
    (void)server.addInt32Variable(base + "/Status", 0);
    (void)server.addFloatVariable(base + "/PassRate", 100.0F);
    (void)server.addInt32Variable(base + "/UnitsInspected", 0);
    (void)server.addInt32Variable(base + "/DefectsFound", 0);
}

}  // namespace

FactoryNodeMap::FactoryNodeMap(model::ProductionModel& production,
                               core::Logger& logger)
    : production_(production), logger_(logger) {}

FactoryNodeMap::FactoryNodeMap(model::ProductionModel& production,
                               core::Logger& logger,
                               OpcUaCommandSink& sink)
    : production_(production), logger_(logger), sink_(&sink) {}

FactoryNodeMap::~FactoryNodeMap() {
    unwire();
}

void FactoryNodeMap::registerNodes(OpcUaServer& server) {
    // Static structure: the three collection folders + the work-unit
    // subtree. Per-equipment / per-checkpoint subtrees are added in
    // `wire` on first event so the address space matches whatever the
    // model exposes (a model with 3 lines lazily creates 3 subtrees;
    // a model with 12 lines creates 12).
    (void)server.addObject(kRoot);
    (void)server.addInt32Variable(std::format("{}/State", kRoot), 0);

    (void)server.addObject(kEquipmentRoot);
    (void)server.addObject(kCheckpointRoot);

    (void)server.addObject(kWorkUnitRoot);
    (void)server.addStringVariable(workUnitPath("Id"), "");
    (void)server.addStringVariable(workUnitPath("ProductId"), "");
    (void)server.addInt32Variable(workUnitPath("CompletedOperations"), 0);
    (void)server.addInt32Variable(workUnitPath("TotalOperations"), 0);

    if (sink_ != nullptr) {
        registerCommandSurface(server);
    }

    logger_.info("OPC-UA: registered Factory address space");
}

void FactoryNodeMap::registerCommandSurface(OpcUaServer& server) {
    // Method nodes under Factory/Commands/. Names match what
    // FactoryCommandSink dispatches on; if these drift apart, the
    // sink's unit test catches it (every routed name is asserted).
    (void)server.addObject("Factory/Commands");
    (void)server.addMethod("Factory/Commands/StartProduction",   *sink_);
    (void)server.addMethod("Factory/Commands/StopProduction",    *sink_);
    (void)server.addMethod("Factory/Commands/ResetSystem",       *sink_);
    (void)server.addMethod("Factory/Commands/StartCalibration",  *sink_);

    // Writable bool per equipment slot. Same path FactoryCommandSink
    // parses, so a SCADA writing `false` flips setEquipmentEnabled
    // through the sink, which forwards to ProductionModel.
    for (std::uint32_t id = 0; id < kEquipmentCount; ++id) {
        // The subtree is created lazily in wire() on first event, but
        // the Enabled variable lives at the same level so we have to
        // ensure the parent exists up front. `addObject` is idempotent
        // on duplicates so wire()'s ensureEquipmentSubtree won't
        // collide.
        const auto base =
            std::format("Factory/EquipmentLines/Line{}", id);
        (void)server.addObject(base);
        (void)server.addBoolVariableWithWriteCallback(
            base + "/Enabled",
            /*initial=*/true,
            *sink_);
    }

    logger_.info("OPC-UA: registered Factory/Commands surface "
                 "(start / stop / reset / calibrate + per-line Enabled)");
}

void FactoryNodeMap::wire(OpcUaServer& server) {
    if (wired_) return;
    wired_ = true;

    // OpcUaServer is captured by reference. The bridge is owned by
    // OpcUaBackend, which guarantees server outlives this map. unwire()
    // does NOT cancel these subscriptions because ProductionModel does
    // not expose a removal API (documented in its header) -- we use a
    // wired_ flag instead so post-unwire callbacks become no-ops.
    production_.onSystemStateChanged(
        [this, &server](model::SystemState state) {
            if (!wired_) return;
            (void)server.writeInt32(std::format("{}/State", kRoot),
                                     static_cast<std::int32_t>(state));
        });

    production_.onEquipmentStatusChanged(
        [this, &server](const model::EquipmentStatus& s) {
            if (!wired_) return;
            ensureEquipmentSubtree(server, s.equipmentId);
            (void)server.writeInt32(equipmentPath(s.equipmentId, "Status"),
                                     s.status);
            (void)server.writeInt32(equipmentPath(s.equipmentId, "SupplyLevel"),
                                     s.supplyLevel);
            (void)server.writeString(equipmentPath(s.equipmentId, "Message"),
                                      s.message);
        });

    production_.onQualityCheckpointChanged(
        [this, &server](const model::QualityCheckpoint& q) {
            if (!wired_) return;
            ensureCheckpointSubtree(server, q.checkpointId);
            (void)server.writeString(checkpointPath(q.checkpointId, "Name"),
                                      q.name);
            (void)server.writeInt32(checkpointPath(q.checkpointId, "Status"),
                                     q.status);
            (void)server.writeFloat(checkpointPath(q.checkpointId, "PassRate"),
                                     q.passRate);
            (void)server.writeInt32(
                checkpointPath(q.checkpointId, "UnitsInspected"),
                q.unitsInspected);
            (void)server.writeInt32(
                checkpointPath(q.checkpointId, "DefectsFound"),
                q.defectsFound);
        });

    production_.onWorkUnitChanged(
        [this, &server](const model::WorkUnit& wu) {
            if (!wired_) return;
            (void)server.writeString(workUnitPath("Id"), wu.workUnitId);
            (void)server.writeString(workUnitPath("ProductId"), wu.productId);
            (void)server.writeInt32(workUnitPath("CompletedOperations"),
                                     wu.completedOperations);
            (void)server.writeInt32(workUnitPath("TotalOperations"),
                                     wu.totalOperations);
        });

    logger_.info("OPC-UA: wired ProductionModel signals to address space");
}

void FactoryNodeMap::unwire() noexcept {
    wired_ = false;
}

}  // namespace app::integration::opcua
