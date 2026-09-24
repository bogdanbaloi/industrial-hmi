#pragma once

#include "src/integration/FlashFrame.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace app::integration {

/// How far one update has got. The order matches section 5 of
/// `docs/protocols/uart-flash-v1.md`.
enum class OtaStage {
    Idle,        ///< Nothing sent yet.
    Probing,     ///< `INFO_REQ` sent, waiting for `INFO`.
    Beginning,   ///< `BEGIN` sent, the board is erasing the empty bank.
    Sending,     ///< `DATA` frames, one at a time.
    Committing,  ///< `COMMIT` sent, the board verifies and switches banks.
    Rebooting,   ///< The board resets into the new image.
    Rechecking,  ///< `INFO_REQ` sent again, to read the running version.
    Confirming,  ///< `CONFIRM` sent, waiting for its `ACK`.
    Done,        ///< The board runs the new image and kept it.
    Failed       ///< Stopped. `failure()` says why.
};

/// Why an update stopped. Each value is a different thing to tell an
/// operator, which is why they are not one "error".
enum class OtaFailure {
    None,           ///< Not failed.
    BoardRefused,   ///< The board answered `NAK`. `nakCode()` says which.
    NoAnswer,       ///< Nothing came back, after every resend.
    ProtocolError,  ///< A frame arrived that the protocol does not allow here.
    WrongVersion    ///< After the update the board runs another version.
};

/// The timeouts pinned in the spec's "Timeouts" table
/// (`docs/protocols/uart-flash-v1.md`). Named rather than written into the
/// `Timing` defaults below, because clang-tidy reads a bare 2025 in a
/// member initializer as a magic number and is right to: the figure means
/// nothing without the sentence beside it.
///
/// Namespace scope and not class scope: `Timing`'s default member
/// initializers need them already complete at that point.

/// Every answer except the one to `BEGIN`.
inline constexpr std::chrono::milliseconds kOtaAnswerTimeout{2000};
/// `BEGIN` alone: the same two seconds plus the worst-case bank erase of
/// 24.59 ms, rounded up to the next whole millisecond.
inline constexpr std::chrono::milliseconds kOtaBeginAnswerTimeout{2025};
/// After `COMMIT` the board resets into the new image, so it cannot answer
/// at once. Nothing is sent until this has passed.
inline constexpr std::chrono::milliseconds kOtaRebootWait{500};
/// Resends of the same frame before an update gives up with `NoAnswer`.
inline constexpr int kOtaResendBudget = 3;

/// Drives one update of one board, as pure logic: it performs no I/O, owns
/// no thread and reads no clock. The caller feeds it events and sends the
/// bytes it hands back. REQ-INTEGRATION-015, ADR-0034.
///
/// @rationale The hard part of an update chain is not the happy path, it is
/// what happens when an answer is late, refused or wrong. Keeping the rules
/// in a class with no I/O means every one of those cases is a unit test with
/// no board, no port and no waiting: the test moves the clock by hand.
///
/// @design The rules live in a declarative Boost.SML transition table in the
/// `.cpp`, the same choice `app::model::SystemStateMachine` made: one
/// auditable artefact instead of branching scattered through the code. The
/// library stays behind a pimpl, so no caller pays its compile-time cost.
///
/// @threading Not thread-safe, deliberately timer-free too. One caller
/// drives one session, the way `SerialFrameParser` is driven by one reader.
/// The caller decides which thread that is and when to call `onTick`.
///
/// @logic Stop-and-wait, as the protocol requires: exactly one message is in
/// flight. Every call returns the bytes to send now, or an empty vector when
/// there is nothing to do. `stage()` says where the update is. After a
/// failure `failure()` and `failureText()` say why it stopped.
class OtaSession {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = std::chrono::milliseconds;

    /// How long to wait for each answer, plus how often to resend. The
    /// defaults are the numbers pinned in the spec's "Timeouts" table.
    struct Timing {
        /// Any answer except the one to `BEGIN`.
        Duration answer{kOtaAnswerTimeout};
        /// `BEGIN`: the same 2 s plus the worst-case bank erase, 24.59 ms.
        Duration beginAnswer{kOtaBeginAnswerTimeout};
        /// After `COMMIT` the board resets, so it cannot answer at once.
        Duration reboot{kOtaRebootWait};
        /// Resends of the same frame before giving up.
        int resends{kOtaResendBudget};
    };

    /// @param image    The firmware image, exactly as it must end up in
    ///                 flash. Its size and CRC-32 are announced in `BEGIN`.
    /// @param version  The version number the image reports once it runs.
    ///                 The update is confirmed only if the board reports it.
    /// Two constructors rather than a default argument: `Timing` is a
    /// member type with default member initializers, which are not usable
    /// in a default argument of the same class.
    OtaSession(std::vector<std::byte> image, std::uint32_t version);
    /// @param timing   Overridable for tests, defaults from the spec.
    OtaSession(std::vector<std::byte> image, std::uint32_t version,
               Timing timing);
    ~OtaSession();

    OtaSession(const OtaSession&)            = delete;
    OtaSession& operator=(const OtaSession&) = delete;
    OtaSession(OtaSession&&) noexcept;
    OtaSession& operator=(OtaSession&&) noexcept;

    /// Begin: returns the `INFO_REQ` to send.
    [[nodiscard]] std::vector<std::byte> start(TimePoint now);

    /// Feed one decoded frame from the board. Returns what to send next, or
    /// nothing when the session is waiting, done or failed.
    [[nodiscard]] std::vector<std::byte> onFrame(const FlashFrame& frame,
                                                 TimePoint now);

    /// Feed the current time. Returns a resend when the answer is late, or
    /// the next step when a wait has elapsed. Safe to call as often as the
    /// caller likes: it acts only when a deadline has passed.
    [[nodiscard]] std::vector<std::byte> onTick(TimePoint now);

    [[nodiscard]] OtaStage stage() const;
    [[nodiscard]] OtaFailure failure() const;

    /// One sentence for an operator, naming what failed and why. Empty
    /// while the session has not failed.
    [[nodiscard]] std::string failureText() const;

    /// The `NAK` code the board sent, valid when `failure()` is
    /// `BoardRefused`. Zero otherwise.
    [[nodiscard]] std::uint8_t nakCode() const;

    /// Image bytes the board has acknowledged, for a progress bar.
    [[nodiscard]] std::size_t bytesAcknowledged() const;
    [[nodiscard]] std::size_t imageSize() const;

    /// The version the board reported to the last `INFO_REQ`. Zero until it
    /// answers.
    [[nodiscard]] std::uint32_t boardVersion() const;

    /// True when the board already ran the target version, confirmed, at the
    /// first `INFO`, so the session finished without writing anything.
    [[nodiscard]] bool wasAlreadyUpToDate() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app::integration
