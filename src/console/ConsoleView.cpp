#include "src/console/ConsoleView.h"

#include "src/core/i18n.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace app::console {

namespace {

/// Trim ASCII whitespace from both ends.
constexpr std::string_view trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

/// Equipment status enum -> short upper-case tag for log output.
/// Kept in sync with EquipmentCardViewModel.h so the console and the
/// GTK view agree on the same vocabulary.
constexpr std::string_view equipmentStatusTag(presenter::EquipmentCardStatus s) {
    using S = presenter::EquipmentCardStatus;
    switch (s) {
    case S::Offline:     return "OFFLINE";
    case S::StartingUp:  return "STARTING_UP";
    case S::CheckOutput: return "CHECK_OUTPUT";
    case S::Online:      return "ONLINE";
    case S::Processing:  return "PROCESSING";
    case S::WarmingUp:   return "WARMING_UP";
    case S::Ready:       return "READY";
    case S::Reboot:      return "REBOOT";
    case S::Error:       return "ERROR";
    case S::Disabled:    return "DISABLED";
    case S::Unknown:     return "UNKNOWN";
    }
    return "UNKNOWN";
}

constexpr std::string_view qualityStatusTag(presenter::QualityCheckpointStatus s) {
    using S = presenter::QualityCheckpointStatus;
    switch (s) {
    case S::Passing:  return "PASSING";
    case S::Warning:  return "WARNING";
    case S::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

constexpr std::string_view actuatorStatusTag(presenter::ActuatorCardStatus s) {
    using S = presenter::ActuatorCardStatus;
    switch (s) {
    case S::Offline:     return "OFFLINE";
    case S::Idle:        return "IDLE";
    case S::Working:     return "WORKING";
    case S::Error:       return "ERROR";
    case S::Homing:      return "HOMING";
    case S::Calibrating: return "CALIBRATING";
    case S::Unknown:     return "UNKNOWN";
    }
    return "UNKNOWN";
}

constexpr std::string_view severityTag(presenter::StatusZoneViewModel::Severity s) {
    using S = presenter::StatusZoneViewModel::Severity;
    switch (s) {
    case S::NONE:    return "OK";
    case S::INFO:    return "INFO";
    case S::WARNING: return "WARNING";
    case S::ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace

// --------------------------------------------------------------------------
// Lifecycle
// --------------------------------------------------------------------------

ConsoleView::ConsoleView(std::ostream& out, std::istream& in)
    : out_{out}, in_{in} {}

ConsoleView::~ConsoleView() {
    // jthread destructor will request_stop() + join(). If the reader is
    // blocked in std::getline, join() would hang forever. By the time we
    // destruct, the process is shutting down anyway — either the reader
    // already returned (quit / EOF) or SIGINT killed us; both cases make
    // join() return promptly. Interactive callers must drain via
    // waitForExit() before destruction.
}

void ConsoleView::start() {
    printBanner();
    reader_ = std::jthread([this](std::stop_token stop) { readerLoop(stop); });
}

void ConsoleView::waitForExit() {
    std::unique_lock lock{exitMutex_};
    exitCv_.wait(lock, [this] { return exit_.load(std::memory_order_acquire); });
}

void ConsoleView::signalExit() {
    {
        const std::scoped_lock lock{exitMutex_};
        exit_.store(true, std::memory_order_release);
    }
    exitCv_.notify_all();
}

// --------------------------------------------------------------------------
// Reader loop and command dispatch
// --------------------------------------------------------------------------

void ConsoleView::readerLoop(std::stop_token stop) {
    std::string line;
    while (!stop.stop_requested() && std::getline(in_, line)) {
        if (stop.stop_requested()) break;
        dispatchCommand(line);
        if (exit_.load(std::memory_order_acquire)) break;
    }
    // EOF, stop, or quit — signal exit so waitForExit() returns.
    signalExit();
}

void ConsoleView::dispatchCommand(std::string_view raw) {
    const std::string_view line = trim(raw);
    if (line.empty()) return;

    // Case-insensitive first token.
    const auto spaceAt = line.find(' ');
    std::string verb{line.substr(0, spaceAt)};
    std::transform(verb.begin(), verb.end(), verb.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (verb == "help" || verb == "h" || verb == "?") {
        printHelp();
    } else if (verb == "status") {
        printStatus();
    } else if (verb == "quit" || verb == "exit" || verb == "q") {
        writeLine("[BYE      ] Shutting down.");
        if (cbShutdown_) cbShutdown_();
        signalExit();
    } else {
        writeLine(std::format("[ERROR    ] Unknown command: '{}'. Type 'help'.",
                              verb));
    }
}

// --------------------------------------------------------------------------
// Output helpers
// --------------------------------------------------------------------------

void ConsoleView::writeLine(std::string_view s) {
    const std::scoped_lock lock{outMutex_};
    out_ << s << '\n';
    out_.flush();
}

void ConsoleView::printBanner() {
    const std::scoped_lock lock{outMutex_};
    out_ << "========================================\n"
         << " Industrial HMI - Console front-end\n"
         << " Type 'help' for commands, 'quit' to exit.\n"
         << "========================================\n";
    out_.flush();
}

void ConsoleView::printHelp() {
    const std::scoped_lock lock{outMutex_};
    out_ << "Commands:\n"
         << "  help, h, ?   Show this list\n"
         << "  status       Dump current system snapshot\n"
         << "  quit, q      Exit the application\n";
    out_.flush();
}

void ConsoleView::printStatus() {
    const std::scoped_lock sl{stateMutex_};
    const std::scoped_lock ol{outMutex_};

    out_ << "--- STATUS ---\n";
    if (lastWorkUnit_) {
        out_ << std::format("  work_unit  id={} product={} progress={}/{}\n",
                            lastWorkUnit_->workUnitId,
                            lastWorkUnit_->productId,
                            lastWorkUnit_->completedOperations,
                            lastWorkUnit_->totalOperations);
    }
    if (lastStatusZone_) {
        out_ << std::format("  system     severity={} message={}\n",
                            severityTag(lastStatusZone_->severity),
                            lastStatusZone_->message);
    }
    for (const auto& e : lastEquipment_) {
        out_ << std::format("  equipment  id={} status={} enabled={} supply={}\n",
                            e.equipmentId,
                            equipmentStatusTag(e.status),
                            e.enabled,
                            e.consumables);
    }
    for (const auto& q : lastQuality_) {
        out_ << std::format("  quality    id={} status={} pass={:.1f}% inspected={} defects={}\n",
                            q.checkpointId,
                            qualityStatusTag(q.status),
                            q.passRate,
                            q.unitsInspected,
                            q.defectsFound);
    }
    if (lastControl_) {
        out_ << std::format("  control    start={} stop={} reset={} calib={}\n",
                            lastControl_->startEnabled,
                            lastControl_->stopEnabled,
                            lastControl_->resetRestartEnabled,
                            lastControl_->calibrationEnabled);
    }
    out_ << "--- END ---\n";
    out_.flush();
}

// --------------------------------------------------------------------------
// ViewObserver — cache the latest VM (for `status`) and emit one
// structured line per event. Format is stable so scenario CI tests
// can diff against expected files.
// --------------------------------------------------------------------------

void ConsoleView::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    {
        const std::scoped_lock lock{stateMutex_};
        lastWorkUnit_ = vm;
    }
    writeLine(std::format(
        "[WORK_UNIT] id={} product={} progress={}/{}",
        vm.workUnitId, vm.productId,
        vm.completedOperations, vm.totalOperations));
}

void ConsoleView::onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) {
    {
        const std::scoped_lock lock{stateMutex_};
        const auto it = std::find_if(
            lastEquipment_.begin(), lastEquipment_.end(),
            [&](const auto& e) { return e.equipmentId == vm.equipmentId; });
        if (it == lastEquipment_.end()) lastEquipment_.push_back(vm);
        else                            *it = vm;
    }
    writeLine(std::format(
        "[EQUIPMENT] id={} status={} enabled={}",
        vm.equipmentId, equipmentStatusTag(vm.status), vm.enabled));
}

void ConsoleView::onActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) {
    writeLine(std::format(
        "[ACTUATOR ] id={} status={} auto={} home={}",
        vm.actuatorId, actuatorStatusTag(vm.status),
        vm.autoMode, vm.atHomePosition));
}

void ConsoleView::onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) {
    {
        const std::scoped_lock lock{stateMutex_};
        const auto it = std::find_if(
            lastQuality_.begin(), lastQuality_.end(),
            [&](const auto& q) { return q.checkpointId == vm.checkpointId; });
        if (it == lastQuality_.end()) lastQuality_.push_back(vm);
        else                          *it = vm;
    }
    writeLine(std::format(
        "[QUALITY  ] id={} status={} pass={:.1f}% inspected={} defects={}",
        vm.checkpointId, qualityStatusTag(vm.status), vm.passRate,
        vm.unitsInspected, vm.defectsFound));
}

void ConsoleView::onControlPanelChanged(const presenter::ControlPanelViewModel& vm) {
    {
        const std::scoped_lock lock{stateMutex_};
        lastControl_ = vm;
    }
    writeLine(std::format(
        "[CONTROL  ] start={} stop={} reset={} calib={}",
        vm.startEnabled, vm.stopEnabled,
        vm.resetRestartEnabled, vm.calibrationEnabled));
}

void ConsoleView::onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) {
    {
        const std::scoped_lock lock{stateMutex_};
        lastStatusZone_ = vm;
    }
    writeLine(std::format(
        "[SYSTEM   ] severity={} message={}",
        severityTag(vm.severity), vm.message));
}

void ConsoleView::onError(const std::string& errorMessage) {
    writeLine(std::format("[ERROR    ] {}", errorMessage));
}

}  // namespace app::console
