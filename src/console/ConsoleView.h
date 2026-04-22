#pragma once

#include "src/presenter/ViewObserver.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace app::console {

/// Interactive headless front-end for the Industrial HMI.
///
/// Implements `app::ViewObserver` so `DashboardPresenter` drives it the
/// same way it drives the GTK pages — zero presenter-side branching per
/// front-end. Runs a std::jthread reading stdin, dispatches line-based
/// commands, and prints presenter events to an injected output stream
/// in a deterministic, parse-friendly format (`[CATEGORY] key=value`).
///
/// @design Same substitutability promise as the GTK View (LSP): anything
/// the DashboardPresenter emits through ViewObserver is handled here,
/// and any user command is forwarded to the presenter through explicit
/// action callbacks injected by `InitConsole` at wiring time.
///
/// @thread_safety
/// - `out_` writes are serialised by `outMutex_`.
/// - The latest ViewModel snapshot is guarded by `stateMutex_` so the
///   reader thread's `status` command sees a consistent view even if
///   a presenter callback is mid-update.
/// - Action callbacks are invoked on the reader thread; the presenter
///   dispatches back on whatever thread the Model ticks from. Both
///   sides rely on the presenter's internal `BasePresenter` mutex for
///   safety.
class ConsoleView : public ::app::ViewObserver {
public:
    using Action = std::function<void()>;
    using ToggleEquipmentAction = std::function<void(std::uint32_t, bool)>;

    /// @param out Where events and command output go. Default std::cout.
    ///            Tests inject a std::stringstream for assertion.
    /// @param in  Where commands are read from. Default std::cin.
    explicit ConsoleView(std::ostream& out, std::istream& in);
    ~ConsoleView() override;

    ConsoleView(const ConsoleView&)            = delete;
    ConsoleView& operator=(const ConsoleView&) = delete;
    ConsoleView(ConsoleView&&)                 = delete;
    ConsoleView& operator=(ConsoleView&&)      = delete;

    // Wiring — injected by InitConsole at startup. Safe to leave any of
    // them unbound; the reader just prints "command not wired" in that
    // case so commands added later don't silently no-op.
    void onStart(Action cb)            { cbStart_            = std::move(cb); }
    void onStop(Action cb)             { cbStop_             = std::move(cb); }
    void onReset(Action cb)            { cbReset_            = std::move(cb); }
    void onCalibrate(Action cb)        { cbCalibrate_        = std::move(cb); }
    void onToggleEquipment(ToggleEquipmentAction cb) {
        cbToggleEquipment_ = std::move(cb);
    }
    /// Optional hook for InitConsole to stop the simulation tick thread
    /// before the process returns control to main(). Fires on quit.
    void onShutdown(Action cb)         { cbShutdown_         = std::move(cb); }

    // Lifecycle

    /// Spawn the stdin reader thread. Returns immediately.
    void start();

    /// Block the caller until the reader thread signals exit (quit
    /// command, EOF on stdin, or destructor-driven stop request).
    void waitForExit();

    // ViewObserver — only the subset the console renders is overridden.
    // The defaults in the base interface handle the dialog-specific
    // callbacks (ViewProduct, ResetProcess…) as no-ops, which is the
    // right behaviour for a line-oriented terminal UI.
    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onEquipmentCardChanged(const presenter::EquipmentCardViewModel& vm) override;
    void onActuatorCardChanged(const presenter::ActuatorCardViewModel& vm) override;
    void onQualityCheckpointChanged(const presenter::QualityCheckpointViewModel& vm) override;
    void onControlPanelChanged(const presenter::ControlPanelViewModel& vm) override;
    void onStatusZoneChanged(const presenter::StatusZoneViewModel& vm) override;
    void onError(const std::string& errorMessage) override;

private:
    // Command loop
    void readerLoop(std::stop_token stop);
    void dispatchCommand(std::string_view raw);
    void printHelp();
    void printStatus();
    void printBanner();
    void signalExit();

    // Output helpers (serialise access to out_ across threads)
    void writeLine(std::string_view s);

    // I/O
    std::ostream& out_;
    std::istream& in_;
    std::mutex    outMutex_;

    // Injected actions. std::function so presenter type stays out of
    // this header (Dependency Inversion — ConsoleView depends on the
    // ViewObserver abstraction, not on DashboardPresenter).
    Action                 cbStart_;
    Action                 cbStop_;
    Action                 cbReset_;
    Action                 cbCalibrate_;
    ToggleEquipmentAction  cbToggleEquipment_;
    Action                 cbShutdown_;

    // Latest ViewModel snapshot (for `status` command). Guarded by
    // stateMutex_ because presenter callbacks fire on the tick thread
    // while the reader thread may be building a status dump.
    std::mutex                                                stateMutex_;
    std::optional<presenter::WorkUnitViewModel>               lastWorkUnit_;
    std::vector<presenter::EquipmentCardViewModel>            lastEquipment_;
    std::vector<presenter::QualityCheckpointViewModel>        lastQuality_;
    std::optional<presenter::ControlPanelViewModel>           lastControl_;
    std::optional<presenter::StatusZoneViewModel>             lastStatusZone_;

    // Exit coordination — reader thread sets exit_ and notifies, main
    // thread waits on exitCv_ in waitForExit().
    std::mutex              exitMutex_;
    std::condition_variable exitCv_;
    std::atomic<bool>       exit_{false};

    // Reader thread. jthread so the destructor requests stop + joins.
    std::jthread            reader_;
};

}  // namespace app::console
