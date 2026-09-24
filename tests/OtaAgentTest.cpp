// [utest->req~integration-016~1]
// Covers REQ-INTEGRATION-016: the OTA agent, the piece that owns one
// OtaSession and a real link -- bytes out through an injected send function,
// decoded frames in through the sink handed to the transport, and the
// session's clock ticked on the agent's own thread.
//
// No serial port here either. The "board" is a fake that plays the SendFn:
// it decodes what the host sent with the same parser SerialBackend uses,
// then answers through the agent's own frame sink. That is exactly the
// shape of the real wiring, with the driver taken out.
//
// Every wait is a bounded poll with a generous budget, never a fixed sleep:
// the agent runs on its own thread, so the test asks "has it got there yet"
// rather than guessing how long it takes.

#include "src/integration/FlashFrame.h"
#include "src/integration/FlashFrameParser.h"
#include "src/integration/OtaAgent.h"
#include "src/integration/OtaSession.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using app::integration::FlashFrame;
using app::integration::FlashFrameParser;
using app::integration::OtaAgent;
using app::integration::OtaFailure;
using app::integration::OtaProgress;
using app::integration::OtaSession;
using app::integration::OtaStage;

/// Message and NAK codes, from section 4 of the protocol.
constexpr std::uint8_t kInfoReq = 0x01;
constexpr std::uint8_t kBegin   = 0x02;
constexpr std::uint8_t kData    = 0x03;
constexpr std::uint8_t kCommit  = 0x04;
constexpr std::uint8_t kConfirm = 0x05;
constexpr std::uint8_t kInfo    = 0x81;
constexpr std::uint8_t kAck     = 0x82;
constexpr std::uint8_t kNak     = 0x83;

constexpr std::uint8_t kNakFlashError = 0x05;

constexpr std::uint8_t kStateConfirmed = 0;
constexpr std::uint8_t kBank1          = 1;

constexpr std::uint32_t kOldVersion = 1;
constexpr std::uint32_t kNewVersion = 2;

/// Bits in a byte, and the mask for one, for the little-endian helpers. The
/// operands are widened to `unsigned` first: a `uint8_t` shifted directly
/// promotes to `int` and trips `hicpp-signed-bitwise`.
constexpr unsigned      kBitsPerByte = 8U;
constexpr unsigned      kU32Bits     = 32U;
constexpr std::uint32_t kByteMask    = 0xFFU;

/// Bytes of offset in front of a `DATA` payload's image bytes.
constexpr std::size_t kDataOffsetBytes = 4;

/// A `BEGIN` payload is size u32, CRC-32 u32, version u32, per section 4.
constexpr std::size_t kBeginPayloadBytes = 3 * sizeof(std::uint32_t);

/// An image whose size is neither a multiple of the 256-byte chunk nor of
/// the 8-byte flash word, so the last frame is short AND padded. Three
/// `DATA` frames, which is enough for a progress bar to move twice.
constexpr std::size_t kImageBytes = 601;

/// How long a bounded poll waits before it gives up. Generous on purpose:
/// it is a ceiling that a healthy run never approaches, not a delay anyone
/// pays. A loaded CI runner under a sanitizer is still far inside it.
constexpr std::chrono::milliseconds kPollBudget{10000};
/// How long the poll sleeps between two looks.
constexpr std::chrono::milliseconds kPollInterval{1};

/// How long the deliberately slow fake link holds one send open. A fixed
/// figure and not a poll, because here the wait IS the thing under test: it
/// is the fake link being slow on purpose, so that a second `stop()` has a
/// send in flight to come back from, not a guess about how fast anything is.
constexpr std::chrono::milliseconds kSlowSendHold{100};

/// Timings for a test that wants the update to run: far longer than the
/// agent's tick so nothing times out by accident, far shorter than the
/// spec's own numbers so the whole conversation is over in milliseconds.
OtaSession::Timing briskTiming() {
    OtaSession::Timing timing;
    timing.answer      = std::chrono::milliseconds{2000};
    timing.beginAnswer = std::chrono::milliseconds{2000};
    timing.reboot      = std::chrono::milliseconds{20};
    return timing;
}

/// Timings for a test that wants the update to STOP somewhere and stay
/// there, so the stage it is parked in can be observed without racing a
/// timeout. Nothing in these tests waits for one of these to elapse.
OtaSession::Timing patientTiming() {
    OtaSession::Timing timing;
    timing.answer      = std::chrono::minutes{10};
    timing.beginAnswer = std::chrono::minutes{10};
    timing.reboot      = std::chrono::minutes{10};
    return timing;
}

std::vector<std::byte> makeImage(std::size_t size) {
    std::vector<std::byte> image;
    image.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        image.push_back(static_cast<std::byte>(index % 251U));
    }
    return image;
}

[[nodiscard]] std::uint32_t readLe32(const std::vector<std::byte>& payload,
                                     std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < sizeof(std::uint32_t); ++index) {
        value |= std::to_integer<std::uint32_t>(payload[at + index])
                 << (index * kBitsPerByte);
    }
    return value;
}

/// Poll until `predicate` holds, or the budget runs out. Returns what the
/// predicate last said, so a caller can assert on it.
template <typename Predicate>
[[nodiscard]] bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kPollBudget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return predicate();
}

/// A board on the other end of the link. It IS the agent's `SendFn`: the
/// agent hands it bytes, it decodes them the way `SerialBackend` would, and
/// it answers through the agent's own frame sink.
///
/// Everything it records is read from the test's thread while the agent's
/// thread is writing it, so the whole of its state sits behind one mutex.
class FakeBoard {
public:
    explicit FakeBoard(std::uint32_t version) : version_(version) {}

    /// The sink the agent handed out. Set before the agent starts.
    void attach(std::function<void(const FlashFrame&)> sink) {
        const std::lock_guard<std::mutex> lock(mutex_);
        sink_ = std::move(sink);
    }

    /// Answer nothing to this message type, the way a board that has gone
    /// quiet behaves. The host then sits in the stage that sent it.
    void goSilentOn(std::uint8_t type) {
        const std::lock_guard<std::mutex> lock(mutex_);
        silentOn_ = type;
    }

    /// Refuse this message type with a `NAK` carrying `code`.
    void refuse(std::uint8_t type, std::uint8_t code) {
        const std::lock_guard<std::mutex> lock(mutex_);
        refusedType_ = type;
        refusalCode_ = code;
    }

    /// The link itself is gone: every send fails, nothing is decoded.
    void cutTheLink() { linkUp_.store(false, std::memory_order_release); }

    /// The `OtaAgent::SendFn`. Called on the agent's thread.
    bool onHostBytes(std::span<const std::byte> bytes) {
        if (!linkUp_.load(std::memory_order_acquire)) {
            return false;
        }
        std::vector<FlashFrame>                replies;
        std::function<void(const FlashFrame&)> sink;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            sink = sink_;
            for (const FlashFrame& frame : parser_.consume(bytes).frames) {
                seen_.push_back(frame.type);
                if (std::optional<FlashFrame> reply = answer(frame)) {
                    replies.push_back(std::move(*reply));
                }
            }
        }
        // Outside the lock: the sink posts onto the agent's io_context, and
        // nothing it does needs the board.
        if (sink) {
            for (const FlashFrame& reply : replies) {
                sink(reply);
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<std::uint8_t> seen() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return seen_;
    }

    [[nodiscard]] bool sawType(std::uint8_t type) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return std::find(seen_.begin(), seen_.end(), type) != seen_.end();
    }

    /// What reached flash, cut back to the announced size: what the board
    /// checksums at `COMMIT`.
    [[nodiscard]] std::vector<std::byte> image() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        // Clamped: an update that stopped between BEGIN and the last DATA
        // leaves fewer bytes written than BEGIN announced, and the cut would
        // then run past the end. A caller comparing against the source image
        // still gets a mismatch, which is the failure it wanted to see.
        const std::size_t cut = std::min<std::size_t>(announcedSize_,
                                                      written_.size());
        return {written_.begin(),
                written_.begin() + static_cast<std::ptrdiff_t>(cut)};
    }

    [[nodiscard]] std::uint32_t announcedCrc32() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return announcedCrc32_;
    }

private:
    /// Build the board's answer to one host frame. Called under the lock.
    [[nodiscard]] std::optional<FlashFrame> answer(const FlashFrame& frame) {
        if (frame.type == silentOn_) {
            return std::nullopt;
        }
        if (frame.type == refusedType_) {
            return FlashFrame{kNak, frame.seq,
                              {static_cast<std::byte>(refusalCode_)}};
        }
        switch (frame.type) {
        case kInfoReq:
            return infoFrame(frame.seq);
        case kBegin:
            // Checked before it is indexed: a host that regressed into a
            // short frame would otherwise read past the end of the payload,
            // which is undefined behaviour in the fake rather than a named
            // failure in the test.
            if (frame.payload.size() < kBeginPayloadBytes) {
                ADD_FAILURE() << "BEGIN carried " << frame.payload.size()
                              << " bytes, the protocol says "
                              << kBeginPayloadBytes;
                return refusal(frame.seq);
            }
            announcedSize_  = readLe32(frame.payload, 0);
            announcedCrc32_ = readLe32(frame.payload, sizeof(std::uint32_t));
            // From here the board reports the version it is being given, so
            // the recheck after the reset finds the new image.
            version_ = readLe32(frame.payload, 2 * sizeof(std::uint32_t));
            written_.clear();
            return FlashFrame{kAck, frame.seq, {}};
        case kData:
            if (frame.payload.size() < kDataOffsetBytes) {
                ADD_FAILURE() << "DATA carried " << frame.payload.size()
                              << " bytes, too short for its offset field";
                return refusal(frame.seq);
            }
            written_.insert(
                written_.end(),
                frame.payload.begin() +
                    static_cast<std::ptrdiff_t>(kDataOffsetBytes),
                frame.payload.end());
            return FlashFrame{kAck, frame.seq, {}};
        case kCommit:
        case kConfirm:
            return FlashFrame{kAck, frame.seq, {}};
        default:
            ADD_FAILURE() << "the host sent an unexpected message type";
            return refusal(frame.seq);
        }
    }

    /// The board's way of saying no, used wherever it cannot answer.
    [[nodiscard]] static FlashFrame refusal(std::uint16_t seq) {
        return FlashFrame{kNak, seq, {static_cast<std::byte>(kNakFlashError)}};
    }

    /// `INFO` payload: version u32, active bank u8, state u8.
    [[nodiscard]] FlashFrame infoFrame(std::uint16_t seq) const {
        std::vector<std::byte> payload;
        for (unsigned shift = 0; shift < kU32Bits; shift += kBitsPerByte) {
            payload.push_back(
                static_cast<std::byte>((version_ >> shift) & kByteMask));
        }
        payload.push_back(static_cast<std::byte>(kBank1));
        payload.push_back(static_cast<std::byte>(kStateConfirmed));
        return FlashFrame{kInfo, seq, payload};
    }

    mutable std::mutex                     mutex_;
    std::function<void(const FlashFrame&)> sink_;
    std::atomic<bool>         linkUp_{true};
    FlashFrameParser          parser_;
    std::uint32_t             version_;
    std::uint32_t             announcedSize_  = 0;
    std::uint32_t             announcedCrc32_ = 0;
    std::uint8_t              silentOn_       = 0;
    std::uint8_t              refusedType_    = 0;
    std::uint8_t              refusalCode_    = 0;
    std::vector<std::byte>    written_;
    std::vector<std::uint8_t> seen_;
};

/// Counts the `onDone` callbacks and keeps the progress the last one
/// carried. Written on the agent's thread, read on the test's.
class DoneRecord {
public:
    void record(const OtaProgress& progress) {
        const std::lock_guard<std::mutex> lock(mutex_);
        ++count_;
        last_ = progress;
    }

    [[nodiscard]] int count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    [[nodiscard]] OtaProgress last() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return last_;
    }

private:
    mutable std::mutex mutex_;
    int                count_ = 0;
    OtaProgress        last_;
};

TEST(OtaAgentTest, StartSendsTheFirstFrame) {
    FakeBoard board(kOldVersion);
    board.goSilentOn(kInfoReq);

    OtaAgent agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                   },
                   makeImage(kImageBytes), kNewVersion, patientTiming(),
                   OtaAgent::DoneFn{});
    board.attach(agent.frameSink());

    // Nothing is on the wire until start(): the agent owns the moment the
    // update begins, not the constructor.
    EXPECT_EQ(agent.progress().stage, OtaStage::Idle);
    EXPECT_EQ(agent.progress().imageSize, kImageBytes);

    agent.start();

    // Wait on the published stage, not on the board having seen the frame:
    // beginUpdate() sends first and publishes second, so a wait on the board
    // can return with the snapshot still reading Idle. Probing is the later
    // of the two, so reaching it means the frame is already on the wire.
    EXPECT_TRUE(waitFor(
        [&agent] { return agent.progress().stage == OtaStage::Probing; }))
        << "the agent never put the first frame on the wire";
    const std::vector<std::uint8_t> seen = board.seen();
    ASSERT_FALSE(seen.empty());
    EXPECT_EQ(seen.front(), kInfoReq) << "the update did not open with INFO_REQ";
    EXPECT_FALSE(agent.progress().transportFailed);
}

TEST(OtaAgentTest, FeedingTheBoardsAnswerAdvancesTheStage) {
    // The board answers the first question and then goes quiet, so the
    // session parks in Beginning: the step only a decoded frame fed back
    // through the sink can reach.
    FakeBoard board(kOldVersion);
    board.goSilentOn(kBegin);

    OtaAgent agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                   },
                   makeImage(kImageBytes), kNewVersion, patientTiming(),
                   OtaAgent::DoneFn{});
    board.attach(agent.frameSink());
    agent.start();

    EXPECT_TRUE(waitFor([&agent] {
        return agent.progress().stage == OtaStage::Beginning;
    })) << "the board's INFO never reached the session";

    const std::vector<std::uint8_t> seen = board.seen();
    ASSERT_EQ(seen.size(), 2U);
    EXPECT_EQ(seen[0], kInfoReq);
    EXPECT_EQ(seen[1], kBegin) << "BEGIN follows INFO, per section 5";
    EXPECT_EQ(agent.progress().failure, OtaFailure::None);
}

TEST(OtaAgentTest, AWholeUpdateReachesDoneAndFiresOnDoneOnce) {
    const std::vector<std::byte> image = makeImage(kImageBytes);
    FakeBoard  board(kOldVersion);
    DoneRecord done;

    OtaAgent agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                   },
                   image, kNewVersion, briskTiming(),
                   [&done](const OtaProgress& progress) {
                       done.record(progress);
                   });
    board.attach(agent.frameSink());
    agent.start();

    // ASSERT, not EXPECT: everything below reads what the board flashed, and
    // an update that stopped half way leaves it short of the size BEGIN
    // announced. A failure here has to stop the test, not walk into it.
    ASSERT_TRUE(waitFor(
        [&agent] { return agent.progress().stage == OtaStage::Done; }))
        << "the update did not finish";
    agent.stop();

    const OtaProgress progress = agent.progress();
    EXPECT_EQ(progress.stage, OtaStage::Done);
    EXPECT_EQ(progress.failure, OtaFailure::None);
    EXPECT_FALSE(progress.transportFailed);
    EXPECT_FALSE(progress.alreadyUpToDate);
    EXPECT_EQ(progress.bytesAcknowledged, kImageBytes);
    EXPECT_EQ(progress.imageSize, kImageBytes);

    EXPECT_EQ(board.image(), image) << "the flashed image differs";
    EXPECT_EQ(board.announcedCrc32(), app::integration::crc32IsoHdlc(image));
    // The whole conversation, in the order section 5 lays it out.
    EXPECT_TRUE(board.sawType(kData));
    EXPECT_TRUE(board.sawType(kCommit));
    EXPECT_TRUE(board.sawType(kConfirm));

    EXPECT_EQ(done.count(), 1) << "onDone must fire exactly once";
    EXPECT_EQ(done.last().stage, OtaStage::Done);
}

TEST(OtaAgentTest, BoardRefusesConfirmWithFlashError) {
    // The hand-reflashed-board case firmware asked about: the image goes
    // over fine and the board resets into it, then refuses to keep it with
    // FLASH_ERROR. The update must stop named, not hang.
    FakeBoard  board(kOldVersion);
    DoneRecord done;
    board.refuse(kConfirm, kNakFlashError);

    OtaAgent agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                   },
                   makeImage(kImageBytes), kNewVersion, briskTiming(),
                   [&done](const OtaProgress& progress) {
                       done.record(progress);
                   });
    board.attach(agent.frameSink());
    agent.start();

    EXPECT_TRUE(waitFor(
        [&agent] { return agent.progress().stage == OtaStage::Failed; }))
        << "the refusal never stopped the update";
    agent.stop();

    const OtaProgress progress = agent.progress();
    EXPECT_EQ(progress.stage, OtaStage::Failed);
    EXPECT_EQ(progress.failure, OtaFailure::BoardRefused);
    EXPECT_EQ(progress.nakCode, kNakFlashError);
    EXPECT_FALSE(progress.failureText.empty())
        << "an operator needs a sentence, not just a code";
    EXPECT_TRUE(board.sawType(kConfirm)) << "it failed before CONFIRM";
    EXPECT_EQ(done.count(), 1);
}

TEST(OtaAgentTest, TransportFailureIsReportedWithoutWaitingForTheTimeoutBudget) {
    // The link is down from the first byte, and the session's own budgets
    // are ten minutes each, so nothing here can be the session timing out.
    // The report has to come from the agent noticing SendFn said no.
    FakeBoard board(kOldVersion);
    board.cutTheLink();

    OtaAgent agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                   },
                   makeImage(kImageBytes), kNewVersion, patientTiming(),
                   OtaAgent::DoneFn{});
    board.attach(agent.frameSink());
    agent.start();

    EXPECT_TRUE(waitFor([&agent] { return agent.progress().transportFailed; }))
        << "a dead link was never reported";

    const OtaProgress progress = agent.progress();
    EXPECT_TRUE(progress.transportFailed);
    // Distinct from, and not dependent on, the session's own verdict: it is
    // still sitting in Probing waiting out a budget it will never reach.
    EXPECT_EQ(progress.stage, OtaStage::Probing);
    EXPECT_EQ(progress.failure, OtaFailure::None);
}

TEST(OtaAgentTest, StopIsIdempotentAndSafeFromTwoThreads) {
    FakeBoard board(kOldVersion);
    board.goSilentOn(kInfoReq);

    OtaAgent agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                   },
                   makeImage(kImageBytes), kNewVersion, patientTiming(),
                   OtaAgent::DoneFn{});
    board.attach(agent.frameSink());

    agent.start();
    agent.start();  // idempotent: the second one must not start a second run
    EXPECT_TRUE(waitFor([&board] { return board.sawType(kInfoReq); }));

    std::thread first([&agent] {
        agent.stop();
        agent.stop();
    });
    std::thread second([&agent] {
        agent.stop();
        agent.stop();
    });
    first.join();
    second.join();
    agent.stop();

    // One INFO_REQ, from the one run that started, and the snapshot is
    // still readable after everything has been stopped.
    EXPECT_EQ(board.seen().size(), 1U);
    EXPECT_EQ(agent.progress().imageSize, kImageBytes);
}

TEST(OtaAgentTest, StopWaitsForTheThreadEvenWhileAnotherThreadIsStopping) {
    // stop() promises the thread is gone when it returns, and a caller acts
    // on that promise: in the real wiring the SerialBackend whose send() the
    // agent captured is torn down next. So the caller that does NOT win the
    // shutdown latch has to wait for the winner's join too. Leaving early on
    // the strength of "someone else is already stopping it" hands that caller
    // a live agent thread sitting inside the very send function it is about
    // to destroy.
    std::atomic<bool> sendInFlight{false};
    std::atomic<bool> stopperEntered{false};
    std::atomic<bool> inFlightWhenStopReturned{true};

    OtaAgent agent(
        [&sendInFlight](std::span<const std::byte>) {
            // A slow link: the agent thread sits in here, which is exactly
            // where stop() must not leave it.
            sendInFlight.store(true, std::memory_order_release);
            std::this_thread::sleep_for(kSlowSendHold);
            sendInFlight.store(false, std::memory_order_release);
            return true;
        },
        makeImage(kImageBytes), kNewVersion, patientTiming(),
        OtaAgent::DoneFn{});
    agent.start();

    ASSERT_TRUE(waitFor([&sendInFlight] {
        return sendInFlight.load(std::memory_order_acquire);
    })) << "the agent never reached the send";

    // The first one takes the latch and blocks in the join, because the
    // agent thread is still inside the send.
    std::thread first([&agent, &stopperEntered] {
        stopperEntered.store(true, std::memory_order_release);
        agent.stop();
    });
    ASSERT_TRUE(waitFor([&stopperEntered] {
        return stopperEntered.load(std::memory_order_acquire);
    }));

    // ...and this is the one that loses it.
    agent.stop();
    inFlightWhenStopReturned.store(
        sendInFlight.load(std::memory_order_acquire), std::memory_order_release);
    first.join();

    EXPECT_FALSE(inFlightWhenStopReturned.load(std::memory_order_acquire))
        << "stop() returned while a send was still running on the agent "
           "thread: the caller would tear the transport down under it";
}

TEST(OtaAgentTest, DestructorStopsWithoutJoinHang) {
    FakeBoard board(kOldVersion);
    board.goSilentOn(kInfoReq);

    const auto before = std::chrono::steady_clock::now();
    {
        OtaAgent agent([&board](std::span<const std::byte> bytes) {
                           return board.onHostBytes(bytes);
                       },
                       makeImage(kImageBytes), kNewVersion, patientTiming(),
                       OtaAgent::DoneFn{});
        board.attach(agent.frameSink());
        agent.start();
        // Destroy it mid-update, with the tick timer armed and the session
        // waiting on an answer that is never coming.
        EXPECT_TRUE(waitFor([&board] { return board.sawType(kInfoReq); }));
    }
    const auto elapsed = std::chrono::steady_clock::now() - before;

    // The destructor stops and joins. If it waited on the session's own
    // ten-minute budget instead, this would not be reached at all; the
    // bound is here so a regression fails loudly rather than hanging CI.
    EXPECT_LT(elapsed, kPollBudget * 2)
        << "the destructor did not stop the agent promptly";
}

TEST(OtaAgentTest, FrameArrivingAfterDestructionIsDroppedNotUb) {
    // The transport outlives the agent in the real wiring: SerialBackend
    // holds the sink and its reader thread can be mid-frame when the agent
    // goes away. The sink holds a weak reference precisely for this.
    FakeBoard board(kOldVersion);
    board.goSilentOn(kInfoReq);
    std::function<void(const FlashFrame&)> sink;

    {
        OtaAgent agent([&board](std::span<const std::byte> bytes) {
                           return board.onHostBytes(bytes);
                       },
                       makeImage(kImageBytes), kNewVersion, patientTiming(),
                       OtaAgent::DoneFn{});
        sink = agent.frameSink();
        board.attach(sink);
        agent.start();
        EXPECT_TRUE(waitFor([&board] { return board.sawType(kInfoReq); }));
    }

    // The agent is gone; the sink is not. Under ASan or TSan a shared
    // reference to freed state, or a post onto a destroyed io_context,
    // would surface here.
    ASSERT_TRUE(static_cast<bool>(sink));
    for (int attempt = 0; attempt < 16; ++attempt) {
        sink(FlashFrame{kAck, static_cast<std::uint16_t>(attempt), {}});
    }
    SUCCEED() << "frames after destruction were dropped";
}

TEST(OtaAgentTest, ProgressIsReadableConcurrentlyFromAnotherThread) {
    // Skip under Valgrind memcheck: a reader thread spinning on progress()
    // for the length of a whole update runs far slower there and risks the
    // per-test ctest --memcheck timeout. The case exists to catch a data
    // race, which TSan covers and memcheck does not. CI sets
    // RUNNING_UNDER_VALGRIND=1 for the memcheck job only -- the idiom
    // ConfigManagerTest.ConcurrentReadersDuringReload established.
    if (const char* under = std::getenv("RUNNING_UNDER_VALGRIND");
        under != nullptr && std::string_view(under) == "1") {
        GTEST_SKIP() << "ProgressIsReadableConcurrentlyFromAnotherThread "
                        "skipped under Valgrind memcheck (TSan covers it).";
    }

    FakeBoard board(kOldVersion);
    OtaAgent  agent([&board](std::span<const std::byte> bytes) {
                       return board.onHostBytes(bytes);
                    },
                   makeImage(kImageBytes), kNewVersion, briskTiming(),
                   OtaAgent::DoneFn{});
    board.attach(agent.frameSink());

    std::atomic<bool>        stop{false};
    std::atomic<std::size_t> reads{0};
    std::thread              reader([&agent, &stop, &reads] {
        while (!stop.load(std::memory_order_relaxed)) {
            const OtaProgress progress = agent.progress();
            // Touch the members a UI would: the copy must be a whole one,
            // never a half-published mix of two updates.
            EXPECT_LE(progress.bytesAcknowledged, progress.imageSize);
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    agent.start();
    const bool finished = waitFor(
        [&agent] { return agent.progress().stage == OtaStage::Done; });
    stop.store(true, std::memory_order_relaxed);
    reader.join();
    agent.stop();

    EXPECT_TRUE(finished) << "the update did not finish while being polled";
    EXPECT_GT(reads.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(agent.progress().bytesAcknowledged, kImageBytes);
}

}  // namespace
