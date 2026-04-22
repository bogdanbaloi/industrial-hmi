#include "src/console/ConsoleView.h"

#include "src/core/i18n.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace app::console {

namespace {

/// Trim ASCII whitespace from both ends.
constexpr std::string_view trim(std::string_view s) {
    constexpr std::string_view kWs = " \t\r\n";
    const auto first = s.find_first_not_of(kWs);
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(kWs);
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

constexpr std::string_view alertSeverityTag(presenter::AlertSeverity s) {
    using S = presenter::AlertSeverity;
    switch (s) {
    case S::Info:     return "INFO";
    case S::Warning:  return "WARNING";
    case S::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

}  // namespace

// Lifecycle

ConsoleView::ConsoleView(std::ostream& out, std::istream& in)
    : out_{out}, in_{in} {}

// Note on the (defaulted) destructor: jthread will request_stop() +
// join() on member destruction. If the reader is still blocked in
// std::getline, join() would hang forever. In practice the process is
// shutting down by the time we destruct — either the reader already
// returned (quit / EOF) or SIGINT killed us; both cases make join()
// return promptly. Interactive callers must drain via waitForExit()
// before letting the instance go out of scope.
ConsoleView::~ConsoleView() = default;

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

// Reader loop and command dispatch

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

    // Case-insensitive first token. Rest of the line kept raw (may be
    // case-sensitive args like alert keys).
    const auto spaceAt = line.find(' ');
    std::string verb{line.substr(0, spaceAt)};
    std::transform(verb.begin(), verb.end(), verb.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const std::string_view args =
        spaceAt == std::string_view::npos
            ? std::string_view{}
            : trim(line.substr(spaceAt + 1));

    if (verb == "help" || verb == "h" || verb == "?") {
        printHelp();
    } else if (verb == "status") {
        printStatus();
    } else if (verb == "start" || verb == "s") {
        if (cbStart_) cbStart_();
        else          writeLine("[WARN     ] start action not wired");
    } else if (verb == "stop" || verb == "p") {
        if (cbStop_) cbStop_();
        else         writeLine("[WARN     ] stop action not wired");
    } else if (verb == "reset" || verb == "r") {
        if (cbReset_) cbReset_();
        else          writeLine("[WARN     ] reset action not wired");
    } else if (verb == "calibrate" || verb == "c" || verb == "cal") {
        if (cbCalibrate_) cbCalibrate_();
        else              writeLine("[WARN     ] calibrate action not wired");
    } else if (verb == "eq") {
        handleEqCommand(args);
    } else if (verb == "alerts") {
        printAlerts();
    } else if (verb == "dismiss") {
        handleDismissCommand(args);
    } else if (verb == "products") {
        if (cbListProducts_) cbListProducts_();   // triggers onProductsLoaded
        printProducts();
    } else if (verb == "view") {
        handleViewCommand(args);
    } else if (verb == "quit" || verb == "exit" || verb == "q") {
        writeLine("[BYE      ] Shutting down.");
        if (cbShutdown_) cbShutdown_();
        signalExit();
    } else {
        writeLine(std::format("[ERROR    ] Unknown command: '{}'. Type 'help'.",
                              verb));
    }
}

void ConsoleView::handleEqCommand(std::string_view args) {
    // Syntax: eq <id> on|off
    // Parse id (first token) + state (second token).
    if (args.empty()) {
        writeLine("[ERROR    ] Usage: eq <id> on|off");
        return;
    }
    const auto spaceAt = args.find(' ');
    std::string_view idToken =
        (spaceAt == std::string_view::npos) ? args : args.substr(0, spaceAt);
    std::string_view stateToken =
        (spaceAt == std::string_view::npos) ? std::string_view{}
                                            : trim(args.substr(spaceAt + 1));

    std::uint32_t id = 0;
    auto [ptr, ec] = std::from_chars(
        idToken.data(), idToken.data() + idToken.size(), id);
    if (ec != std::errc{} || ptr != idToken.data() + idToken.size()) {
        writeLine(std::format("[ERROR    ] Invalid equipment id: '{}'", idToken));
        return;
    }

    std::string stateLower{stateToken};
    std::transform(stateLower.begin(), stateLower.end(), stateLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    bool enabled = false;
    if      (stateLower == "on"  || stateLower == "true"  || stateLower == "1") enabled = true;
    else if (stateLower == "off" || stateLower == "false" || stateLower == "0") enabled = false;
    else {
        writeLine(std::format("[ERROR    ] Expected on|off after id, got: '{}'",
                              stateToken));
        return;
    }

    if (cbToggleEquipment_) cbToggleEquipment_(id, enabled);
    else                    writeLine("[WARN     ] toggle-equipment action not wired");
}

void ConsoleView::handleDismissCommand(std::string_view args) {
    if (args.empty()) {
        writeLine("[ERROR    ] Usage: dismiss <key>. Use 'alerts' to list keys.");
        return;
    }
    if (cbDismissAlert_) cbDismissAlert_(args);
    else                 writeLine("[WARN     ] dismiss-alert action not wired");
}

void ConsoleView::handleViewCommand(std::string_view args) {
    if (args.empty()) {
        writeLine("[ERROR    ] Usage: view <id>");
        return;
    }
    int id = 0;
    auto [ptr, ec] = std::from_chars(args.data(), args.data() + args.size(), id);
    if (ec != std::errc{} || ptr != args.data() + args.size()) {
        writeLine(std::format("[ERROR    ] Invalid product id: '{}'", args));
        return;
    }
    if (cbViewProduct_) cbViewProduct_(id);
    else                writeLine("[WARN     ] view-product action not wired");
}

// Output helpers

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
         << "  help, h, ?            Show this list\n"
         << "  status                Dump current system snapshot\n"
         << "  start, s              Start production\n"
         << "  stop, p               Stop / pause production\n"
         << "  reset, r              Reset / restart sequence\n"
         << "  calibrate, cal, c     Run calibration routine\n"
         << "  eq <id> on|off        Toggle equipment enable (id: 0..2)\n"
         << "  alerts                List active alerts\n"
         << "  dismiss <key>         Dismiss an alert by key\n"
         << "  products              List products from database\n"
         << "  view <id>             Show product detail\n"
         << "  quit, q, exit         Exit the application\n";
    out_.flush();
}

void ConsoleView::printAlerts() {
    std::vector<presenter::AlertViewModel> active;
    if (cbAlertsSnapshot_) active = cbAlertsSnapshot_();

    const std::scoped_lock lock{outMutex_};
    out_ << "--- ALERTS ---\n";
    if (active.empty()) {
        out_ << "  (none)\n";
    } else {
        for (const auto& a : active) {
            out_ << std::format("  [{}] key={} {} at {}\n",
                                alertSeverityTag(a.severity),
                                a.key,
                                a.title,
                                a.timestamp);
            if (!a.message.empty()) {
                out_ << std::format("        {}\n", a.message);
            }
        }
    }
    out_ << "--- END ---\n";
    out_.flush();
}

void ConsoleView::printProducts() {
    const std::scoped_lock sl{stateMutex_};
    const std::scoped_lock ol{outMutex_};

    out_ << "--- PRODUCTS ---\n";
    if (!lastProducts_ || lastProducts_->products.empty()) {
        out_ << "  (no products loaded)\n";
    } else {
        for (const auto& p : lastProducts_->products) {
            out_ << std::format(
                "  id={} code={} name=\"{}\" status={} stock={} quality={:.1f}%\n",
                p.id, p.productCode, p.name, p.status, p.stock, p.qualityRate);
        }
    }
    out_ << "--- END ---\n";
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

// ViewObserver — cache the latest VM (for `status`) and emit one
// structured line per event. Format is stable so scenario CI tests
// can diff against expected files.

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

void ConsoleView::onProductsLoaded(const presenter::ProductsViewModel& vm) {
    {
        const std::scoped_lock lock{stateMutex_};
        lastProducts_ = vm;
    }
    writeLine(std::format("[PRODUCTS ] loaded count={}", vm.products.size()));
}

void ConsoleView::onViewProductReady(const presenter::ViewProductDialogViewModel& vm) {
    const std::scoped_lock lock{outMutex_};
    out_ << "--- PRODUCT DETAIL ---\n";
    out_ << std::format("  id/code: {}\n",       vm.productId);
    out_ << std::format("  verified: {}\n",      vm.isVerified);
    out_ << std::format("  created:  {}\n",      vm.createdDate.empty() ? "-" : vm.createdDate);
    if (!vm.description.empty()) {
        out_ << "  description:\n";
        // Indent multi-line description for readability.
        std::string_view desc = vm.description;
        std::size_t pos = 0;
        while (pos < desc.size()) {
            const auto nl = desc.find('\n', pos);
            const auto end = (nl == std::string_view::npos) ? desc.size() : nl;
            out_ << "    " << desc.substr(pos, end - pos) << '\n';
            if (nl == std::string_view::npos) break;
            pos = nl + 1;
        }
    }
    out_ << "--- END ---\n";
    out_.flush();
}

void ConsoleView::onError(const std::string& errorMessage) {
    writeLine(std::format("[ERROR    ] {}", errorMessage));
}

}  // namespace app::console
