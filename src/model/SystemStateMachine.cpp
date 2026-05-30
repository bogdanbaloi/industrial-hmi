#include "src/model/SystemStateMachine.h"

#include <boost/sml.hpp>

#include <utility>
#include <vector>

namespace app::model {

namespace {
namespace sml = boost::sml;

// Events -- one struct per producer-facing command.
struct StartEvt {};
struct StopEvt {};
struct ResetEvt {};
struct CalibrateEvt {};
struct CalibrationDoneEvt {};

/// The formal Phase 1 transition table.
///
/// States: idle (initial *), running, calibration, error.
/// In Phase 1 every command currently in use is accepted from every
/// state in which the legacy code accepted it -- the goal of this phase
/// is *no behavioural change on valid paths*, only routing the
/// transition through SML so the table is a single auditable artefact.
/// Guards + invalid-transition rejection arrive in Phase 2; Fault /
/// Acknowledge / Recover for safe-state on error arrive in Phase 3.
struct SystemStateTable {
    auto operator()() const {
        using namespace sml;
        return make_transition_table(
            // From idle:
            *"idle"_s + event<StartEvt>            = "running"_s,
             "idle"_s + event<CalibrateEvt>        = "calibration"_s,
             "idle"_s + event<StopEvt>             = "idle"_s,
             "idle"_s + event<ResetEvt>            = "idle"_s,

            // From running:
             "running"_s + event<StopEvt>          = "idle"_s,
             "running"_s + event<ResetEvt>         = "idle"_s,
             "running"_s + event<CalibrateEvt>     = "calibration"_s,
             "running"_s + event<StartEvt>         = "running"_s,

            // From calibration:
             "calibration"_s + event<CalibrationDoneEvt> = "idle"_s,
             "calibration"_s + event<StopEvt>            = "idle"_s,
             "calibration"_s + event<ResetEvt>           = "idle"_s,
             "calibration"_s + event<StartEvt>           = "running"_s,
             "calibration"_s + event<CalibrateEvt>       = "calibration"_s,

            // From error (no entry path defined in Phase 1; transitions
            // kept so Phase 3's safe-state work can drop straight in):
             "error"_s + event<ResetEvt>           = "idle"_s,
             "error"_s + event<StopEvt>            = "idle"_s
        );
    }
};

}  // namespace

// Boost.SML's internal zero-size array (__BOOST_SML_ZERO_SIZE_ARRAY) is
// diagnosed at the point where `sml::sm<...>` is instantiated -- so the
// suppression has to wrap the use, not the include. SYSTEM-include alone
// is insufficient on GCC 13. Scoped tightly to this struct so the rest
// of the file stays under the project's -Wpedantic -Werror gate.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

struct SystemStateMachine::Impl {
    sml::sm<SystemStateTable>  sm;
    std::vector<StateCallback> observers;
    SystemState                lastNotified{SystemState::IDLE};

    [[nodiscard]] SystemState currentState() const {
        using namespace sml;
        if (sm.is("running"_s))     return SystemState::RUNNING;
        if (sm.is("calibration"_s)) return SystemState::CALIBRATION;
        if (sm.is("error"_s))       return SystemState::ERROR;
        return SystemState::IDLE;
    }

    void afterDispatch() {
        const SystemState now = currentState();
        if (now == lastNotified) {
            return;
        }
        lastNotified = now;
        for (auto& cb : observers) {
            cb(now);
        }
    }
};

SystemStateMachine::SystemStateMachine()
    : impl_(std::make_unique<Impl>()) {}

SystemStateMachine::~SystemStateMachine() = default;

void SystemStateMachine::start() {
    impl_->sm.process_event(StartEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::stop() {
    impl_->sm.process_event(StopEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::reset() {
    impl_->sm.process_event(ResetEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::calibrate() {
    impl_->sm.process_event(CalibrateEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::calibrationDone() {
    impl_->sm.process_event(CalibrationDoneEvt{});
    impl_->afterDispatch();
}

SystemState SystemStateMachine::state() const {
    return impl_->currentState();
}

void SystemStateMachine::onStateChanged(StateCallback callback) {
    impl_->observers.push_back(std::move(callback));
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace app::model
