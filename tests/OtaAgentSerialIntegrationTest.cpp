// [utest->req~integration-016~1]
// The OTA chain end to end over a real serial link, with nothing injected
// between the pieces: a real SerialBackend opened on a pseudo-terminal, a
// real OtaAgent whose SendFn IS SerialBackend::send and whose frameSink()
// IS what the backend was constructed with, a real OtaSession inside it and
// a real FlashFrameParser on both ends.
//
// OtaAgentTest already drives a whole update, but through an injected
// lambda: the agent and the transport had never appeared in a test
// together, so the one seam that matters in production was the one seam
// nothing exercised. Here the bytes go out through a file descriptor and
// come back through another one.
//
// POSIX-only (posix_openpt / ptsname), the same gate SerialBackendTest
// uses. There is no pty on Windows MSYS2, and the cross-platform half of
// this logic is covered by OtaAgentTest, which runs everywhere.

#include "src/integration/FlashFrame.h"
#include "src/integration/FlashFrameParser.h"
#include "src/integration/OtaAgent.h"
#include "src/integration/OtaSession.h"
#include "src/integration/SerialBackend.h"

#include <gtest/gtest.h>

#include <fcntl.h>   // posix_openpt, O_RDWR, O_NOCTTY
#include <poll.h>    // poll
#include <stdlib.h>  // grantpt, unlockpt, ptsname
#include <unistd.h>  // read, write, close

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

using app::integration::encodeFlashFrame;
using app::integration::FlashFrame;
using app::integration::FlashFrameParser;
using app::integration::OtaAgent;
using app::integration::OtaFailure;
using app::integration::OtaSession;
using app::integration::OtaStage;
using app::integration::SerialBackend;
using app::integration::SerialReading;

constexpr unsigned kBaudRate = 115200;

/// Message and NAK codes, from section 4 of the protocol.
constexpr std::uint8_t kInfoReq = 0x01;
constexpr std::uint8_t kBegin   = 0x02;
constexpr std::uint8_t kData    = 0x03;
constexpr std::uint8_t kCommit  = 0x04;
constexpr std::uint8_t kConfirm = 0x05;
constexpr std::uint8_t kInfo    = 0x81;
constexpr std::uint8_t kAck     = 0x82;

constexpr std::uint8_t kStateConfirmed = 0;
constexpr std::uint8_t kBank1          = 1;

constexpr std::uint32_t kOldVersion = 1;
constexpr std::uint32_t kNewVersion = 2;

/// Bits in a byte, and the mask for one, for the little-endian helpers. The
/// operands widen to `unsigned` first: a `uint8_t` shifted directly
/// promotes to `int` and trips `hicpp-signed-bitwise`.
constexpr unsigned      kBitsPerByte = 8U;
constexpr unsigned      kU32Bits     = 32U;
constexpr std::uint32_t kByteMask    = 0xFFU;

/// Bytes of offset in front of a `DATA` payload's image bytes.
constexpr std::size_t kDataOffsetBytes = 4;
/// A `BEGIN` payload is size u32, CRC-32 u32, version u32.
constexpr std::size_t kBeginPayloadBytes = 3 * sizeof(std::uint32_t);

/// An image whose size is neither a multiple of the 256-byte chunk nor of
/// the 8-byte flash word, so the last frame is short AND padded. Small on
/// purpose: every byte of it crosses a real pty here.
constexpr std::size_t kImageBytes = 601;

/// How long a bounded poll waits before it gives up. A ceiling a healthy
/// run never approaches, not a delay anyone pays.
constexpr std::chrono::milliseconds kPollBudget{15000};
/// How long the poll sleeps between two looks.
constexpr std::chrono::milliseconds kPollInterval{2};
/// How long the board's reader blocks in poll() before it re-checks its
/// stop token. Short enough that the test never waits on it at teardown.
constexpr int kBoardPollMs = 20;
/// One read off the pty master.
constexpr std::size_t kBoardChunkBytes = 256;

/// An `ACK` is eight bytes. Seven is what a board that resets to switch
/// banks right after answering leaves on the wire, with the last CRC byte
/// still in the shift register. Firmware measured exactly this.
constexpr std::size_t kTruncatedAckBytes = 7;

/// Timings for a run that should finish: long enough that a real pty never
/// times out by accident, short enough that the reboot wait does not
/// dominate the test.
[[nodiscard]] OtaSession::Timing briskTiming() {
    OtaSession::Timing timing;
    timing.answer      = std::chrono::milliseconds{2000};
    timing.beginAnswer = std::chrono::milliseconds{2000};
    timing.reboot      = std::chrono::milliseconds{20};
    return timing;
}

/// Timings for a run that should give up, or resend, quickly. Real wall
/// clock is spent waiting these out, so they are the smallest values that
/// still sit far above a pty round trip.
[[nodiscard]] OtaSession::Timing impatientTiming(int resends) {
    OtaSession::Timing timing;
    timing.answer      = std::chrono::milliseconds{250};
    timing.beginAnswer = std::chrono::milliseconds{250};
    timing.reboot      = std::chrono::milliseconds{20};
    timing.resends     = resends;
    return timing;
}

[[nodiscard]] std::vector<std::byte> makeImage(std::size_t size) {
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

/// Poll until `predicate` holds, or the budget runs out.
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

/// Owns one pty pair: the master fd the board reads and writes, plus the
/// slave device path the backend opens as its serial port. Trimmed from
/// the one in SerialBackendTest, which keeps its own for the same reason
/// this keeps this one: one self-contained helper per test file.
class PtyPair {
public:
    PtyPair() : master_(posix_openpt(O_RDWR | O_NOCTTY)) {
        if (master_ >= 0 && grantpt(master_) == 0 && unlockpt(master_) == 0) {
            if (const char* path = ptsname(master_)) {
                slavePath_ = path;
            }
        }
    }
    ~PtyPair() {
        if (master_ >= 0) {
            ::close(master_);
        }
    }
    PtyPair(const PtyPair&)            = delete;
    PtyPair& operator=(const PtyPair&) = delete;
    PtyPair(PtyPair&&)                 = delete;
    PtyPair& operator=(PtyPair&&)      = delete;

    [[nodiscard]] bool               ok() const { return !slavePath_.empty(); }
    [[nodiscard]] const std::string& slavePath() const { return slavePath_; }
    [[nodiscard]] int                master() const { return master_; }

private:
    int         master_;
    std::string slavePath_;
};

/// The board on the far end of the link. Unlike the one in OtaAgentTest it
/// is handed no sink: it reads the host's bytes off the pty master with a
/// real parser and writes its answers back as real bytes, on its own
/// thread, which is the only way anything reaches the master.
class FakeBoard {
public:
    FakeBoard(int master, std::uint32_t version)
        : master_(master), version_(version) {}

    /// Start answering. Stopped by the destructor.
    void start() {
        reader_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }

    /// Answer nothing at all, the way a board that has gone quiet behaves.
    void goSilent() {
        const std::lock_guard<std::mutex> lock(mutex_);
        silent_ = true;
    }

    /// Cut the first answer to `type` short by one byte, then answer every
    /// later one in full. The board resetting right after it writes.
    void truncateFirstAnswerTo(std::uint8_t type) {
        const std::lock_guard<std::mutex> lock(mutex_);
        truncateType_ = type;
    }

    [[nodiscard]] bool sawType(std::uint8_t type) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return std::find(seen_.begin(), seen_.end(), type) != seen_.end();
    }

    [[nodiscard]] std::size_t countOf(std::uint8_t type) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<std::size_t>(
            std::count(seen_.begin(), seen_.end(), type));
    }

    /// What reached flash, cut back to the size `BEGIN` announced, which is
    /// what the board checksums at `COMMIT`. Clamped, so a run that stopped
    /// half way gives a mismatch rather than reading past the end.
    [[nodiscard]] std::vector<std::byte> image() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t cut =
            std::min<std::size_t>(announcedSize_, written_.size());
        return {written_.begin(),
                written_.begin() + static_cast<std::ptrdiff_t>(cut)};
    }

private:
    void run(const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            pollfd waiting{master_, POLLIN, 0};
            if (::poll(&waiting, 1, kBoardPollMs) <= 0) {
                continue;
            }
            std::array<std::byte, kBoardChunkBytes> chunk{};
            const ssize_t got = ::read(master_, chunk.data(), chunk.size());
            if (got <= 0) {
                continue;
            }
            answerAll(std::span<const std::byte>(chunk.data(),
                                                 static_cast<std::size_t>(got)));
        }
    }

    void answerAll(std::span<const std::byte> hostBytes) {
        std::vector<std::vector<std::byte>> wire;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const FlashFrame& frame : parser_.consume(hostBytes).frames) {
                seen_.push_back(frame.type);
                if (silent_) {
                    continue;
                }
                if (std::optional<FlashFrame> reply = answer(frame)) {
                    std::vector<std::byte> bytes = encodeFlashFrame(*reply);
                    if (frame.type == truncateType_) {
                        // The reset comes before the last byte is out.
                        bytes.resize(kTruncatedAckBytes);
                        truncateType_ = 0;
                    }
                    wire.push_back(std::move(bytes));
                }
            }
        }
        // Outside the lock: writing to the master can block, and nothing it
        // does needs the board's state.
        for (const std::vector<std::byte>& bytes : wire) {
            const ssize_t put = ::write(master_, bytes.data(), bytes.size());
            EXPECT_EQ(put, static_cast<ssize_t>(bytes.size()))
                << "the board could not put its answer on the wire";
        }
    }

    /// Build the answer to one host frame. Called under the lock.
    [[nodiscard]] std::optional<FlashFrame> answer(const FlashFrame& frame) {
        switch (frame.type) {
        case kInfoReq:
            return infoFrame(frame.seq);
        case kBegin:
            if (frame.payload.size() < kBeginPayloadBytes) {
                ADD_FAILURE() << "BEGIN carried " << frame.payload.size()
                              << " bytes, the protocol says "
                              << kBeginPayloadBytes;
                return std::nullopt;
            }
            announcedSize_ = readLe32(frame.payload, 0);
            // From here the board reports the version it is being given, so
            // the recheck after the reset finds the new image.
            version_ = readLe32(frame.payload, 2 * sizeof(std::uint32_t));
            written_.clear();
            return FlashFrame{kAck, frame.seq, {}};
        case kData:
            if (frame.payload.size() < kDataOffsetBytes) {
                ADD_FAILURE() << "DATA carried " << frame.payload.size()
                              << " bytes, too short for its offset field";
                return std::nullopt;
            }
            written_.insert(written_.end(),
                            frame.payload.begin() +
                                static_cast<std::ptrdiff_t>(kDataOffsetBytes),
                            frame.payload.end());
            return FlashFrame{kAck, frame.seq, {}};
        case kCommit:
        case kConfirm:
            return FlashFrame{kAck, frame.seq, {}};
        default:
            ADD_FAILURE() << "the host sent an unexpected message type";
            return std::nullopt;
        }
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

    int                       master_;
    mutable std::mutex        mutex_;
    FlashFrameParser          parser_;
    std::uint32_t             version_;
    std::uint32_t             announcedSize_ = 0;
    bool                      silent_        = false;
    std::uint8_t              truncateType_  = 0;
    std::vector<std::byte>    written_;
    std::vector<std::uint8_t> seen_;
    std::jthread              reader_;
};

/// The whole chain, assembled the way production assembles it. Declared in
/// one place because the order matters twice: the agent has to exist before
/// the backend, since the backend is constructed with the agent's sink, and
/// the agent has to be destroyed before the backend, since its send
/// function holds on to it.
class Chain {
public:
    Chain(const PtyPair& pty, std::vector<std::byte> image,
          OtaSession::Timing timing)
        : agent_([this](std::span<const std::byte> bytes) {
                     return backend_ != nullptr && backend_->send(bytes);
                 },
                 std::move(image), kNewVersion, timing, OtaAgent::DoneFn{}) {
        backend_ = std::make_unique<SerialBackend>(
            pty.slavePath(), kBaudRate, [](const SerialReading&) {},
            agent_.frameSink());
    }

    ~Chain() {
        // The agent first: its send function reaches into the backend.
        agent_.stop();
        if (backend_ != nullptr) {
            backend_->stop();
        }
    }

    Chain(const Chain&)            = delete;
    Chain& operator=(const Chain&) = delete;
    Chain(Chain&&)                 = delete;
    Chain& operator=(Chain&&)      = delete;

    void start() {
        backend_->start();
        agent_.start();
    }

    [[nodiscard]] OtaAgent&      agent() { return agent_; }
    [[nodiscard]] SerialBackend& backend() { return *backend_; }

private:
    // Declared first so it is destroyed last: see the note above.
    std::unique_ptr<SerialBackend> backend_;
    OtaAgent                       agent_;
};

TEST(OtaAgentSerialIntegrationTest, WholeUpdateReachesDoneOverTheRealSerialLink) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a pty pair";

    FakeBoard board(pty.master(), kOldVersion);
    board.start();

    const std::vector<std::byte> image = makeImage(kImageBytes);
    Chain chain(pty, image, briskTiming());
    chain.start();

    ASSERT_TRUE(waitFor([&chain] {
        return chain.agent().progress().stage == OtaStage::Done;
    })) << "the update did not finish over the real link";

    const auto progress = chain.agent().progress();
    EXPECT_EQ(progress.failure, OtaFailure::None);
    EXPECT_FALSE(progress.transportFailed);
    EXPECT_FALSE(progress.alreadyUpToDate);
    EXPECT_EQ(progress.bytesAcknowledged, kImageBytes);
    EXPECT_EQ(progress.imageSize, kImageBytes);

    // Every byte of the image crossed a file descriptor to get here.
    EXPECT_EQ(board.image(), image) << "the flashed image differs";
    EXPECT_TRUE(board.sawType(kBegin));
    EXPECT_TRUE(board.sawType(kData));
    EXPECT_TRUE(board.sawType(kCommit));
    EXPECT_TRUE(board.sawType(kConfirm));
    EXPECT_EQ(chain.backend().falseStarts(), 0U)
        << "a clean link must produce no false starts";
}

TEST(OtaAgentSerialIntegrationTest, SilentBoardIsReportedFailedNotHung) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a pty pair";

    FakeBoard board(pty.master(), kOldVersion);
    board.goSilent();
    board.start();

    Chain chain(pty, makeImage(kImageBytes), impatientTiming(1));
    chain.start();

    ASSERT_TRUE(waitFor([&chain] {
        return chain.agent().progress().stage == OtaStage::Failed;
    })) << "a silent board left the update hanging";

    const auto progress = chain.agent().progress();
    EXPECT_EQ(progress.failure, OtaFailure::NoAnswer);
    // The distinction that matters to an operator: the cable is fine, the
    // board is not answering. A dead link would say the opposite.
    EXPECT_FALSE(progress.transportFailed)
        << "a silent board must not be reported as a broken link";
    EXPECT_TRUE(board.sawType(kInfoReq)) << "the host never asked";
}

TEST(OtaAgentSerialIntegrationTest, TruncatedCommitAckIsAbsorbedByTheResend) {
    // The failure firmware measured, replayed over a real link rather than
    // fed to the parser by hand. Their first bank-switch build answered
    // COMMIT and reset itself immediately, so the last CRC byte never left
    // the shift register and the host saw seven bytes of an eight-byte ACK.
    // FlashFrameParserTest proves the parser calls that a bad frame. This
    // proves the chain above it survives one: the answer times out, COMMIT
    // is resent, and the update still finishes.
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a pty pair";

    FakeBoard board(pty.master(), kOldVersion);
    board.truncateFirstAnswerTo(kCommit);
    board.start();

    const std::vector<std::byte> image = makeImage(kImageBytes);
    Chain chain(pty, image, impatientTiming(3));
    chain.start();

    ASSERT_TRUE(waitFor([&chain] {
        return chain.agent().progress().stage == OtaStage::Done;
    })) << "one truncated ACK wedged the whole update";

    EXPECT_EQ(chain.agent().progress().failure, OtaFailure::None);
    EXPECT_EQ(board.image(), image);
    EXPECT_GE(board.countOf(kCommit), 2U)
        << "COMMIT was never resent, so nothing was absorbed";
    // The seven held bytes plus the next frame's start byte make one
    // full-length candidate with the wrong CRC. Exactly one, and the good
    // frame behind it still decoded, which is what reaching Done proves.
    EXPECT_EQ(chain.backend().falseStarts(), 1U);
}

}  // namespace
