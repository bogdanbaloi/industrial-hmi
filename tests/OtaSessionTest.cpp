// [utest->req~integration-015~1]
// Covers REQ-INTEGRATION-015: the OTA update session, the host-side state
// machine of the UART flash protocol.
//
// Pure-logic test: the session performs no I/O and reads no clock, so every
// case here is driven by hand. The test plays the board, decodes what the
// session sends, answers with a frame it builds itself, then moves the clock
// forward when it wants a timeout. No port, no thread, no waiting.

#include "src/integration/FlashFrame.h"
#include "src/integration/FlashFrameParser.h"
#include "src/integration/OtaSession.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using app::integration::crc32IsoHdlc;
using app::integration::FlashFrame;
using app::integration::FlashFrameParser;
using app::integration::OtaFailure;
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

constexpr std::uint8_t kNakBadCrc      = 0x01;
constexpr std::uint8_t kNakTooLarge    = 0x03;
constexpr std::uint8_t kNakFlashError  = 0x05;

constexpr std::uint8_t kStateConfirmed = 0;
constexpr std::uint8_t kStateTrial     = 1;
constexpr std::uint8_t kBank1          = 1;

constexpr std::uint32_t kOldVersion = 1;
constexpr std::uint32_t kNewVersion = 2;

/// Image bytes per DATA frame, as the protocol caps them.
constexpr std::size_t kChunkBytes = 256;

/// An image whose size is neither a multiple of the chunk nor of the flash
/// word, so the last frame is short AND needs padding.
constexpr std::size_t kImageBytes = 601;

std::vector<std::byte> makeImage(std::size_t size) {
    std::vector<std::byte> image;
    image.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        image.push_back(static_cast<std::byte>(index % 251U));
    }
    return image;
}

/// Decode whatever the session handed back. The session speaks in bytes, so
/// the test reads them with the same parser the backend uses.
std::optional<FlashFrame> decode(std::span<const std::byte> bytes) {
    FlashFrameParser parser;
    auto out = parser.consume(bytes);
    if (out.frames.size() != 1) {
        return std::nullopt;
    }
    return out.frames.front();
}

std::vector<std::byte> ackFor(const FlashFrame& request) {
    return encodeFlashFrame(FlashFrame{kAck, request.seq, {}});
}

std::vector<std::byte> nakFor(const FlashFrame& request, std::uint8_t code) {
    return encodeFlashFrame(
        FlashFrame{kNak, request.seq, {static_cast<std::byte>(code)}});
}

std::vector<std::byte> infoFor(const FlashFrame& request,
                               std::uint32_t version, std::uint8_t state) {
    std::vector<std::byte> payload;
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        payload.push_back(static_cast<std::byte>((version >> shift) & 0xFFU));
    }
    payload.push_back(static_cast<std::byte>(kBank1));
    payload.push_back(static_cast<std::byte>(state));
    return encodeFlashFrame(FlashFrame{kInfo, request.seq, payload});
}

/// A board that answers the way the real one does, so a test can run a whole
/// update and then look at what was written.
class FakeBoard {
public:
    explicit FakeBoard(std::uint32_t version) : version_(version) {}

    /// Answer one frame from the host. Returns the reply bytes.
    [[nodiscard]] std::vector<std::byte> answer(const FlashFrame& frame) {
        seen_.push_back(frame.type);
        switch (frame.type) {
        case kInfoReq:
            return infoFor(frame, version_, kStateConfirmed);
        case kBegin:
            announcedSize_  = readLe32(frame.payload, 0);
            announcedCrc32_ = readLe32(frame.payload, 4);
            version_        = readLe32(frame.payload, 8);
            written_.clear();
            return ackFor(frame);
        case kData: {
            const std::uint32_t offset = readLe32(frame.payload, 0);
            EXPECT_EQ(offset, written_.size()) << "DATA arrived out of order";
            written_.insert(written_.end(), frame.payload.begin() + 4,
                            frame.payload.end());
            return ackFor(frame);
        }
        case kCommit:
        case kConfirm:
            return ackFor(frame);
        default:
            ADD_FAILURE() << "the host sent an unexpected message type";
            return nakFor(frame, kNakFlashError);
        }
    }

    /// What reached flash, cut back to the announced size, which is what the
    /// board checksums at COMMIT.
    [[nodiscard]] std::vector<std::byte> image() const {
        return {written_.begin(),
                written_.begin() + static_cast<std::ptrdiff_t>(announcedSize_)};
    }
    [[nodiscard]] std::size_t writtenBytes() const { return written_.size(); }
    [[nodiscard]] std::uint32_t announcedCrc32() const { return announcedCrc32_; }
    [[nodiscard]] std::uint32_t announcedSize() const { return announcedSize_; }
    [[nodiscard]] const std::vector<std::uint8_t>& seen() const { return seen_; }

private:
    [[nodiscard]] static std::uint32_t readLe32(
        const std::vector<std::byte>& payload, std::size_t at) {
        std::uint32_t value = 0;
        for (unsigned index = 0; index < 4U; ++index) {
            value |= std::to_integer<std::uint32_t>(payload[at + index])
                     << (index * 8U);
        }
        return value;
    }

    std::uint32_t version_;
    std::uint32_t announcedSize_  = 0;
    std::uint32_t announcedCrc32_ = 0;
    std::vector<std::byte> written_;
    std::vector<std::uint8_t> seen_;
};

/// Runs a session against a board until it stops, moving the clock only when
/// the session is waiting on the board's reset.
void runToEnd(OtaSession& session, FakeBoard& board,
              OtaSession::TimePoint start) {
    auto now  = start;
    auto next = session.start(now);
    for (int step = 0; step < 100; ++step) {
        if (next.empty()) {
            if (session.stage() == OtaStage::Done ||
                session.stage() == OtaStage::Failed) {
                return;
            }
            // Only the reboot wait leaves nothing in flight.
            now += std::chrono::seconds(1);
            next = session.onTick(now);
            continue;
        }
        const auto sent = decode(next);
        ASSERT_TRUE(sent.has_value()) << "the session sent something unreadable";
        const auto reply = board.answer(*sent);
        const auto replyFrame = decode(reply);
        ASSERT_TRUE(replyFrame.has_value());
        next = session.onFrame(*replyFrame, now);
    }
    ADD_FAILURE() << "the session did not finish in 100 steps";
}

TEST(OtaSessionTest, Crc32ReproducesTheStandardCheckValue) {
    const std::string text = "123456789";
    std::vector<std::byte> bytes;
    for (const char c : text) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    EXPECT_EQ(crc32IsoHdlc(bytes), 0xCBF43926U);
}

TEST(OtaSessionTest, AWholeUpdatePutsTheExactImageOnTheBoard) {
    const auto image = makeImage(kImageBytes);
    OtaSession session(image, kNewVersion);
    FakeBoard board(kOldVersion);

    runToEnd(session, board, OtaSession::Clock::now());

    EXPECT_EQ(session.stage(), OtaStage::Done);
    EXPECT_EQ(session.failure(), OtaFailure::None);
    EXPECT_EQ(board.image(), image) << "the flashed image differs";
    EXPECT_EQ(board.announcedSize(), kImageBytes);
    EXPECT_EQ(board.announcedCrc32(), crc32IsoHdlc(image));
    EXPECT_EQ(session.bytesAcknowledged(), kImageBytes);
}

TEST(OtaSessionTest, EveryDataFrameIsAMultipleOfEightBytes) {
    // The L4 programs flash 8 bytes at a time, so the last short chunk is
    // padded. The padding is invisible to the checksum, which covers the
    // announced size only.
    const auto image = makeImage(kImageBytes);
    OtaSession session(image, kNewVersion);
    FakeBoard board(kOldVersion);
    auto now = OtaSession::Clock::now();

    auto next = session.start(now);
    while (!next.empty()) {
        const auto sent = decode(next);
        ASSERT_TRUE(sent.has_value());
        if (sent->type == kData) {
            const std::size_t imageBytes = sent->payload.size() - 4;
            EXPECT_EQ(imageBytes % 8U, 0U) << "unaligned DATA payload";
            EXPECT_LE(imageBytes, kChunkBytes);
        }
        const auto replyFrame = decode(board.answer(*sent));
        ASSERT_TRUE(replyFrame.has_value());
        next = session.onFrame(*replyFrame, now);
        if (next.empty() && session.stage() == OtaStage::Rebooting) {
            now += std::chrono::seconds(1);
            next = session.onTick(now);
        }
    }
    EXPECT_EQ(session.stage(), OtaStage::Done);
    EXPECT_GT(board.writtenBytes(), kImageBytes) << "the last chunk was padded";
}

TEST(OtaSessionTest, AnUpToDateBoardIsLeftAlone) {
    OtaSession session(makeImage(kImageBytes), kNewVersion);
    FakeBoard board(kNewVersion);
    auto now = OtaSession::Clock::now();

    const auto first = decode(session.start(now));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->type, kInfoReq);
    const auto reply = decode(board.answer(*first));
    ASSERT_TRUE(reply.has_value());
    const auto next = session.onFrame(*reply, now);

    EXPECT_TRUE(next.empty()) << "nothing more should be sent";
    EXPECT_EQ(session.stage(), OtaStage::Done);
    EXPECT_TRUE(session.wasAlreadyUpToDate());
    EXPECT_EQ(board.writtenBytes(), 0U);
}

TEST(OtaSessionTest, ABoardOnTrialWithTheRightImageIsOnlyConfirmed) {
    // The board reports the target version but has not been confirmed, the
    // state firmware started reporting honestly on 2026-09-22. The image is
    // already in flash, so sending it again would erase a bank for nothing.
    OtaSession session(makeImage(kImageBytes), kNewVersion);
    auto now = OtaSession::Clock::now();

    const auto first = decode(session.start(now));
    ASSERT_TRUE(first.has_value());
    const auto info = decode(infoFor(*first, kNewVersion, kStateTrial));
    ASSERT_TRUE(info.has_value());

    const auto next = decode(session.onFrame(*info, now));
    ASSERT_TRUE(next.has_value()) << "the session sent nothing";
    EXPECT_EQ(next->type, kConfirm);
    EXPECT_EQ(session.stage(), OtaStage::Confirming);
    EXPECT_FALSE(session.wasAlreadyUpToDate()) << "it still needed confirming";

    const auto ack = decode(ackFor(*next));
    ASSERT_TRUE(ack.has_value());
    EXPECT_TRUE(session.onFrame(*ack, now).empty());
    EXPECT_EQ(session.stage(), OtaStage::Done);
}

TEST(OtaSessionTest, ConfirmRefusedWithFlashErrorAfterTrial) {
    // The hand-reflashed board: it already runs the target version on trial,
    // so the session sends CONFIRM and nothing else, and the board refuses to
    // keep it. FLASH_ERROR is not BAD_CRC, so the same bytes would be refused
    // the same way and the session stops instead of resending. The existing
    // FLASH_ERROR case stops at COMMIT; this one is at CONFIRM, the other end
    // of the conversation.
    OtaSession session(makeImage(kImageBytes), kNewVersion);
    auto now = OtaSession::Clock::now();

    const auto first = decode(session.start(now));
    ASSERT_TRUE(first.has_value());
    const auto info = decode(infoFor(*first, kNewVersion, kStateTrial));
    ASSERT_TRUE(info.has_value());

    const auto confirm = decode(session.onFrame(*info, now));
    ASSERT_TRUE(confirm.has_value());
    ASSERT_EQ(confirm->type, kConfirm);

    const auto nak = decode(nakFor(*confirm, kNakFlashError));
    ASSERT_TRUE(nak.has_value());
    EXPECT_TRUE(session.onFrame(*nak, now).empty())
        << "a refusal is not resent";

    EXPECT_EQ(session.stage(), OtaStage::Failed);
    EXPECT_EQ(session.failure(), OtaFailure::BoardRefused);
    EXPECT_EQ(session.nakCode(), kNakFlashError);
    EXPECT_NE(session.failureText().find("refused"), std::string::npos);
}

TEST(OtaSessionTest, ALostAnswerIsResentWithTheSameBytes) {
    OtaSession session(makeImage(kImageBytes), kNewVersion);
    auto now = OtaSession::Clock::now();
    const auto first = session.start(now);

    // Before the deadline nothing happens.
    EXPECT_TRUE(session.onTick(now + std::chrono::milliseconds(500)).empty());

    now += std::chrono::seconds(3);
    const auto resent = session.onTick(now);
    EXPECT_EQ(resent, first) << "a resend must be the same frame, same SEQ";
    EXPECT_EQ(session.stage(), OtaStage::Probing);
}

TEST(OtaSessionTest, SilenceGivesUpAfterTheResendBudget) {
    OtaSession session(makeImage(kImageBytes), kNewVersion);
    auto now = OtaSession::Clock::now();
    (void)session.start(now);

    for (int attempt = 0; attempt < 3; ++attempt) {
        now += std::chrono::seconds(3);
        EXPECT_FALSE(session.onTick(now).empty()) << "resend " << attempt;
    }
    now += std::chrono::seconds(3);
    EXPECT_TRUE(session.onTick(now).empty());

    EXPECT_EQ(session.stage(), OtaStage::Failed);
    EXPECT_EQ(session.failure(), OtaFailure::NoAnswer);
    EXPECT_NE(session.failureText().find("stopped answering"),
              std::string::npos);
}

TEST(OtaSessionTest, ADamagedFrameIsResentButARefusalStops) {
    const auto image = makeImage(kImageBytes);
    auto now = OtaSession::Clock::now();

    {
        // BAD_CRC means the bytes were damaged on the wire, so the same
        // frame again is the right answer.
        OtaSession session(image, kNewVersion);
        const auto first = session.start(now);
        const auto sent  = decode(first);
        ASSERT_TRUE(sent.has_value());
        const auto nak = decode(nakFor(*sent, kNakBadCrc));
        ASSERT_TRUE(nak.has_value());
        EXPECT_EQ(session.onFrame(*nak, now), first);
        EXPECT_EQ(session.stage(), OtaStage::Probing);
    }
    {
        // Any other NAK is a decision. Resending would be refused the same
        // way, so the session stops and names the code.
        OtaSession session(image, kNewVersion);
        const auto sent = decode(session.start(now));
        ASSERT_TRUE(sent.has_value());
        const auto nak = decode(nakFor(*sent, kNakTooLarge));
        ASSERT_TRUE(nak.has_value());
        EXPECT_TRUE(session.onFrame(*nak, now).empty());
        EXPECT_EQ(session.stage(), OtaStage::Failed);
        EXPECT_EQ(session.failure(), OtaFailure::BoardRefused);
        EXPECT_EQ(session.nakCode(), kNakTooLarge);
        EXPECT_NE(session.failureText().find("0x03"), std::string::npos);
    }
}

TEST(OtaSessionTest, AFlashErrorAtCommitStopsTheSession) {
    // What the real board answers until its bank-switch piece lands.
    const auto image = makeImage(kChunkBytes);
    OtaSession session(image, kNewVersion);
    FakeBoard board(kOldVersion);
    auto now = OtaSession::Clock::now();

    auto next = session.start(now);
    while (!next.empty()) {
        const auto sent = decode(next);
        ASSERT_TRUE(sent.has_value());
        if (sent->type == kCommit) {
            const auto nak = decode(nakFor(*sent, kNakFlashError));
            ASSERT_TRUE(nak.has_value());
            next = session.onFrame(*nak, now);
            break;
        }
        const auto replyFrame = decode(board.answer(*sent));
        ASSERT_TRUE(replyFrame.has_value());
        next = session.onFrame(*replyFrame, now);
    }

    EXPECT_EQ(session.stage(), OtaStage::Failed);
    EXPECT_EQ(session.failure(), OtaFailure::BoardRefused);
    EXPECT_EQ(session.nakCode(), kNakFlashError);
}

TEST(OtaSessionTest, AWrongVersionAfterTheResetIsNotConfirmed) {
    const auto image = makeImage(kChunkBytes);
    OtaSession session(image, kNewVersion);
    FakeBoard board(kOldVersion);
    auto now = OtaSession::Clock::now();

    auto next = session.start(now);
    while (!next.empty()) {
        const auto sent = decode(next);
        ASSERT_TRUE(sent.has_value());
        std::vector<std::byte> reply;
        if (sent->type == kInfoReq && session.stage() == OtaStage::Rechecking) {
            // The board came back on the old image instead of the new one.
            reply = infoFor(*sent, kOldVersion, kStateConfirmed);
        } else {
            reply = board.answer(*sent);
        }
        const auto replyFrame = decode(reply);
        ASSERT_TRUE(replyFrame.has_value());
        next = session.onFrame(*replyFrame, now);
        if (next.empty() && session.stage() == OtaStage::Rebooting) {
            now += std::chrono::seconds(1);
            next = session.onTick(now);
        }
    }

    EXPECT_EQ(session.stage(), OtaStage::Failed);
    EXPECT_EQ(session.failure(), OtaFailure::WrongVersion);
    EXPECT_NE(session.failureText().find("version 1"), std::string::npos);
}

TEST(OtaSessionTest, ConfirmIsSentOnlyAfterTheBoardReportsTheNewVersion) {
    const auto image = makeImage(kChunkBytes);
    OtaSession session(image, kNewVersion);
    FakeBoard board(kOldVersion);
    auto now = OtaSession::Clock::now();

    std::vector<std::uint8_t> order;
    auto next = session.start(now);
    while (!next.empty()) {
        const auto sent = decode(next);
        ASSERT_TRUE(sent.has_value());
        order.push_back(sent->type);
        const auto replyFrame = decode(board.answer(*sent));
        ASSERT_TRUE(replyFrame.has_value());
        next = session.onFrame(*replyFrame, now);
        if (next.empty() && session.stage() == OtaStage::Rebooting) {
            // Nothing is sent into a board that is resetting.
            EXPECT_TRUE(session.onTick(now).empty());
            now += std::chrono::seconds(1);
            next = session.onTick(now);
        }
    }

    const std::vector<std::uint8_t> expected{kInfoReq, kBegin, kData,
                                             kCommit,  kInfoReq, kConfirm};
    EXPECT_EQ(order, expected);
    EXPECT_EQ(session.stage(), OtaStage::Done);
}

TEST(OtaSessionTest, AnEmptyImageIsRefusedBeforeAnythingIsSent) {
    OtaSession session({}, kNewVersion);
    EXPECT_TRUE(session.start(OtaSession::Clock::now()).empty());
    EXPECT_EQ(session.stage(), OtaStage::Failed);
    EXPECT_EQ(session.failure(), OtaFailure::ProtocolError);
    EXPECT_NE(session.failureText().find("empty"), std::string::npos);
}

TEST(OtaSessionTest, AStaleAnswerIsIgnored) {
    OtaSession session(makeImage(kChunkBytes), kNewVersion);
    FakeBoard board(kOldVersion);
    auto now = OtaSession::Clock::now();

    const auto first = decode(session.start(now));
    ASSERT_TRUE(first.has_value());
    const auto info = decode(board.answer(*first));
    ASSERT_TRUE(info.has_value());
    const auto begin = session.onFrame(*info, now);
    ASSERT_FALSE(begin.empty());

    // The ACK of the INFO_REQ arriving late, after BEGIN went out.
    const auto stale = decode(ackFor(*first));
    ASSERT_TRUE(stale.has_value());
    EXPECT_TRUE(session.onFrame(*stale, now).empty());
    EXPECT_EQ(session.stage(), OtaStage::Beginning) << "a stale ACK moved it";
}

}  // namespace
