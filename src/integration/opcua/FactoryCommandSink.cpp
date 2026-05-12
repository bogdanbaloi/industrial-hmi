#include "src/integration/opcua/FactoryCommandSink.h"

#include "src/core/LoggerBase.h"
#include "src/model/ProductionModel.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace app::integration::opcua {

namespace {

/// Expected prefix + suffix bracketing the equipment id in the
/// writable-variable browse path. `dispatchEquipmentEnabled` parses
/// `<prefix><id><suffix>` and extracts the integer in between.
///
/// The prefix matches what `FactoryNodeMap::wireCommands` registers
/// on the server. Hardcoded here on purpose -- the sink owns the
/// domain mapping; if the node map grows, both sides update
/// together so a typo turns into a unit-test failure.
constexpr std::string_view kEquipmentEnabledPrefix =
    "Factory/EquipmentLines/Line";
constexpr std::string_view kEquipmentEnabledSuffix = "/Enabled";

}  // namespace

FactoryCommandSink::FactoryCommandSink(model::ProductionModel& model,
                                       core::Logger& logger)
    : model_(model), logger_(logger) {}

void FactoryCommandSink::onCommand(std::string_view commandName) noexcept {
    try {
        if (commandName == "StartProduction") {
            model_.startProduction();
        } else if (commandName == "StopProduction") {
            model_.stopProduction();
        } else if (commandName == "ResetSystem") {
            model_.resetSystem();
        } else if (commandName == "StartCalibration") {
            model_.startCalibration();
        } else {
            logger_.warn(
                "FactoryCommandSink: unknown OPC-UA command '{}' "
                "(dropping)", commandName);
            return;
        }
        logger_.info("FactoryCommandSink: dispatched '{}'", commandName);
    } catch (const std::exception& exc) {
        // Model setters generally don't throw, but we honour the
        // noexcept contract regardless. A throw here would otherwise
        // propagate up into open62541's C callback and call
        // std::terminate.
        logger_.error(
            "FactoryCommandSink: '{}' threw -- dropping: {}",
            commandName, exc.what());
    } catch (...) {
        logger_.error(
            "FactoryCommandSink: '{}' threw unknown -- dropping",
            commandName);
    }
}

void FactoryCommandSink::onBoolWrite(std::string_view nodePath,
                                     bool value) noexcept {
    try {
        if (dispatchEquipmentEnabled(nodePath, value)) {
            logger_.info(
                "FactoryCommandSink: bool write {} <- {}",
                nodePath, value);
            return;
        }
        logger_.warn(
            "FactoryCommandSink: unknown OPC-UA bool path '{}' "
            "(dropping)", nodePath);
    } catch (const std::exception& exc) {
        logger_.error(
            "FactoryCommandSink: bool write '{}' threw: {}",
            nodePath, exc.what());
    } catch (...) {
        logger_.error(
            "FactoryCommandSink: bool write '{}' threw unknown",
            nodePath);
    }
}

bool FactoryCommandSink::dispatchEquipmentEnabled(std::string_view nodePath,
                                                   bool value) {
    if (!nodePath.starts_with(kEquipmentEnabledPrefix) ||
        !nodePath.ends_with(kEquipmentEnabledSuffix)) {
        return false;
    }
    const auto idBegin = kEquipmentEnabledPrefix.size();
    const auto idEnd   = nodePath.size() - kEquipmentEnabledSuffix.size();
    if (idEnd <= idBegin) return false;

    const auto idView = nodePath.substr(idBegin, idEnd - idBegin);
    std::uint32_t id  = 0;
    const auto result = std::from_chars(idView.data(),
                                         idView.data() + idView.size(), id);
    if (result.ec != std::errc{} ||
        result.ptr != idView.data() + idView.size()) {
        return false;
    }
    model_.setEquipmentEnabled(id, value);
    return true;
}

}  // namespace app::integration::opcua
