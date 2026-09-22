// [utest->req~integration-009~1]
// [utest->req~integration-012~1]
// [utest->req~integration-014~1]
// SerialBackend end-to-end over an emulated serial endpoint: a pseudo-
// terminal (PTY) pair stands in for the microcontroller's virtual COM
// port, so the async read loop is exercised with no real hardware.
//
// POSIX-only (posix_openpt / ptsname); the cross-platform framing logic is
// covered by SerialFrameParserTest. The test opens a PTY master, hands the
// slave device path to the backend, writes frames to the master, and
// asserts the injected sink receives the decoded readings.
//
// The transmit tests run the other direction: the backend sends, the test
// reads what arrives on the master and compares it byte for byte.
//
// The routing tests write telemetry text and flash protocol frames mixed on
// one stream, the way the board will. Each must reach its own sink.

#include "src/integration/FlashFrame.h"
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
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using app::integration::encodeFlashFrame;
using app::integration::FlashFrame;
using app::integration::SerialBackend;
using app::integration::SerialReading;

constexpr unsigned kBaudRate = 115200;

/// `ACK`, the board's answer code from section 4 of the flash protocol.
constexpr std::uint8_t kAckType = 0x82;

/// Telemetry lines as the board sends them (ADR-0029 format).
constexpr std::string_view kTempLine     = "temp,23.5\n";
constexpr std::string_view kHumidityLine = "humidity,60\n";

/// How long a transmit test waits for bytes to reach the PTY master.
constexpr int kReadTimeoutMs = 1000;

/// Pause that lets the read loop take in bytes already written, before a
/// test stops the backend on purpose.
constexpr int kSettleMs = 50;

/// The flash protocol's worked example, `INFO_REQ` with sequence number 1
/// (docs/protocols/uart-flash-v1.md, section 3). A real frame the OTA
/// agent will send, so the first transmit test uses exactly that.
constexpr std::array<std::byte, 8> kInfoRequestFrame{
    std::byte{0xA5}, std::byte{0x01}, std::byte{0x01}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xE9}, std::byte{0xCD}};

/// Bytes a terminal layer would rewrite or swallow: LF (often expanded to
/// CR LF), CR, NUL and 0xFF. Binary frames contain all of them.
constexpr std::array<std::byte, 4> kLineDisciplineBytes{
    std::byte{0x0A}, std::byte{0x0D}, std::byte{0x00}, std::byte{0xFF}};

/// Owns one PTY pair: the master fd the test reads and writes plus the
/// slave device path the backend opens as its serial port.
class PtyPair {
public:
    PtyPair() : master_(posix_openpt(O_RDWR | O_NOCTTY)) {
        if (master_ >= 0 && grantpt(master_) == 0 &&
            unlockpt(master_) == 0) {
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

    [[nodiscard]] bool ok() const { return !slavePath_.empty(); }
    [[nodiscard]] const std::string& slavePath() const { return slavePath_; }

    /// Write bytes to the master, as the device would. Returns false on a
    /// short or failed write, so the test can assert on it.
    [[nodiscard]] bool writeAll(std::span<const std::byte> bytes) const {
        return ::write(master_, bytes.data(), bytes.size()) ==
               static_cast<ssize_t>(bytes.size());
    }

    /// Read from the master until `count` bytes arrived or the timeout
    /// passed. Returns what arrived, so a short result shows in the
    /// assertion instead of hanging the test.
    [[nodiscard]] std::vector<std::byte> readExactly(std::size_t count) const {
        std::vector<std::byte> received;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kReadTimeoutMs);
        while (received.size() < count &&
               std::chrono::steady_clock::now() < deadline) {
            pollfd pfd{master_, POLLIN, 0};
            if (::poll(&pfd, 1, kReadTimeoutMs) <= 0) {
                break;
            }
            std::array<std::byte, 64> chunk{};
            const ssize_t got = ::read(master_, chunk.data(),
                                       std::min(chunk.size(),
                                                count - received.size()));
            if (got <= 0) {
                break;
            }
            received.insert(received.end(), chunk.begin(),
                            chunk.begin() + got);
        }
        return received;
    }

private:
    int         master_;
    std::string slavePath_;
};

std::vector<std::byte> toVector(std::span<const std::byte> bytes) {
    return {bytes.begin(), bytes.end()};
}

// Poll up to a timeout for the io_context thread to deliver `expected`
// readings, so the test does not race the async read loop.
template <typename Predicate>
bool waitFor(Predicate ready) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (ready()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

TEST(SerialBackendTest, ReadsFramesFromSerialIntoSink) {
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master, 0) << "posix_openpt failed";
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    const char* slavePath = ptsname(master);
    ASSERT_NE(slavePath, nullptr);
    const std::string device(slavePath);

    std::mutex mutex;
    std::vector<SerialReading> received;
    SerialBackend backend(device, 115200, [&](const SerialReading& reading) {
        // Runs on the io_context thread -- cross-thread hand-off to the
        // test thread, so guard the shared vector.
        const std::lock_guard<std::mutex> lock(mutex);
        received.push_back(reading);
    });

    backend.start();

    const std::string frames = "temp,23.5\nhumidity,60\n";
    ASSERT_EQ(::write(master, frames.data(), frames.size()),
              static_cast<ssize_t>(frames.size()));

    const bool got = waitFor([&] {
        const std::lock_guard<std::mutex> lock(mutex);
        return received.size() >= 2;
    });

    backend.stop();
    ::close(master);

    ASSERT_TRUE(got) << "sink did not receive both readings in time";
    const std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[0].sensorId, "temp");
    EXPECT_EQ(received[0].value, "23.5");
    EXPECT_EQ(received[1].sensorId, "humidity");
    EXPECT_EQ(received[1].value, "60");
}

TEST(SerialBackendTest, StartOnMissingDeviceThrows) {
    SerialBackend backend("/dev/nonexistent-serial-xyz", 115200,
                          [](const SerialReading&) {});
    EXPECT_ANY_THROW(backend.start());
    EXPECT_FALSE(backend.isRunning());
}

TEST(SerialBackendTest, SendDeliversAProtocolFrameUnchanged) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";
    SerialBackend backend(pty.slavePath(), kBaudRate,
                          [](const SerialReading&) {});
    backend.start();

    ASSERT_TRUE(backend.send(kInfoRequestFrame));
    const std::vector<std::byte> received =
        pty.readExactly(kInfoRequestFrame.size());

    backend.stop();
    EXPECT_EQ(received, toVector(kInfoRequestFrame));
}

TEST(SerialBackendTest, SendDoesNotTranslateLineEndingsOrControlBytes) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";
    SerialBackend backend(pty.slavePath(), kBaudRate,
                          [](const SerialReading&) {});
    backend.start();

    ASSERT_TRUE(backend.send(kLineDisciplineBytes));
    // Ask for one byte more than was sent: a LF expanded to CR LF would
    // show up as that extra byte instead of passing unnoticed.
    const std::vector<std::byte> received =
        pty.readExactly(kLineDisciplineBytes.size() + 1);

    backend.stop();
    EXPECT_EQ(received, toVector(kLineDisciplineBytes));
}

TEST(SerialBackendTest, SuccessiveSendsArriveInCallOrder) {
    // Many one-byte sends in a row keep the write queue non-empty, so the
    // chain in writeNext() has to hand over from one write to the next
    // without reordering or dropping any of them.
    constexpr std::size_t kSendCount = 200;

    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";
    SerialBackend backend(pty.slavePath(), kBaudRate,
                          [](const SerialReading&) {});
    backend.start();

    std::vector<std::byte> expected;
    for (std::size_t i = 0; i < kSendCount; ++i) {
        const std::array<std::byte, 1> one{static_cast<std::byte>(i)};
        ASSERT_TRUE(backend.send(one));
        expected.push_back(one[0]);
    }
    const std::vector<std::byte> received = pty.readExactly(kSendCount);

    backend.stop();
    EXPECT_EQ(received, expected);
}

TEST(SerialBackendTest, SendIsRejectedWhenNotRunning) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";
    SerialBackend backend(pty.slavePath(), kBaudRate,
                          [](const SerialReading&) {});

    EXPECT_FALSE(backend.send(kInfoRequestFrame)) << "before start()";
    backend.start();
    backend.stop();
    EXPECT_FALSE(backend.send(kInfoRequestFrame)) << "after stop()";
}

std::vector<std::byte> asBytes(std::string_view text) {
    const auto view = std::as_bytes(std::span<const char>(text));
    return {view.begin(), view.end()};
}

TEST(SerialBackendTest, FramesAndTelemetryOnOneStreamReachTheirOwnSinks) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";

    std::mutex mutex;
    std::vector<SerialReading> readings;
    std::vector<FlashFrame> frames;
    SerialBackend backend(
        pty.slavePath(), kBaudRate,
        [&](const SerialReading& reading) {
            const std::lock_guard<std::mutex> lock(mutex);
            readings.push_back(reading);
        },
        [&](const FlashFrame& frame) {
            const std::lock_guard<std::mutex> lock(mutex);
            frames.push_back(frame);
        });
    backend.start();

    // Text, a frame whose payload holds a newline, then more text.
    const FlashFrame ack{kAckType, 1, {std::byte{0x0A}}};
    std::vector<std::byte> wire = asBytes(kTempLine);
    const std::vector<std::byte> frameBytes = encodeFlashFrame(ack);
    wire.insert(wire.end(), frameBytes.begin(), frameBytes.end());
    const std::vector<std::byte> tail = asBytes(kHumidityLine);
    wire.insert(wire.end(), tail.begin(), tail.end());
    ASSERT_TRUE(pty.writeAll(wire));

    const bool got = waitFor([&] {
        const std::lock_guard<std::mutex> lock(mutex);
        return readings.size() >= 2 && !frames.empty();
    });
    backend.stop();

    ASSERT_TRUE(got) << "sinks did not receive the readings and the frame";
    const std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0], ack);
    ASSERT_EQ(readings.size(), 2U) << "the frame's newline must not split text";
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[1].sensorId, "humidity");
}

TEST(SerialBackendTest, WithoutAFrameSinkFramesAreDroppedNotReadAsText) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";

    std::mutex mutex;
    std::vector<SerialReading> readings;
    SerialBackend backend(pty.slavePath(), kBaudRate,
                          [&](const SerialReading& reading) {
                              const std::lock_guard<std::mutex> lock(mutex);
                              readings.push_back(reading);
                          });
    backend.start();

    std::vector<std::byte> wire = encodeFlashFrame(FlashFrame{kAckType, 2, {}});
    const std::vector<std::byte> text = asBytes(kTempLine);
    wire.insert(wire.end(), text.begin(), text.end());
    ASSERT_TRUE(pty.writeAll(wire));

    const bool got = waitFor([&] {
        const std::lock_guard<std::mutex> lock(mutex);
        return !readings.empty();
    });
    backend.stop();

    ASSERT_TRUE(got) << "the telemetry line after the frame never arrived";
    const std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(readings.size(), 1U) << "frame bytes leaked into the text path";
    EXPECT_EQ(readings[0].sensorId, "temp");
}

TEST(SerialBackendTest, NoisyLinkShowsFalseStartsInTheHealthSummary) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";

    std::mutex mutex;
    std::vector<SerialReading> readings;
    SerialBackend backend(pty.slavePath(), kBaudRate,
                          [&](const SerialReading& reading) {
                              const std::lock_guard<std::mutex> lock(mutex);
                              readings.push_back(reading);
                          });
    backend.start();
    const std::string quietSummary = backend.metricsSummary();

    // A start byte followed by an impossible LEN of 65535: one false start.
    std::vector<std::byte> wire{std::byte{0xA5}, std::byte{0x00},
                                std::byte{0x00}, std::byte{0x00},
                                std::byte{0xFF}, std::byte{0xFF}};
    const std::vector<std::byte> text = asBytes(kTempLine);
    wire.insert(wire.end(), text.begin(), text.end());
    ASSERT_TRUE(pty.writeAll(wire));

    const bool got = waitFor([&] {
        const std::lock_guard<std::mutex> lock(mutex);
        return !readings.empty();
    });
    backend.stop();

    ASSERT_TRUE(got) << "the telemetry line after the noise never arrived";
    EXPECT_EQ(quietSummary.find("false starts"), std::string::npos)
        << "a clean link must keep the summary terse";
    EXPECT_EQ(backend.falseStarts(), 1U);
    EXPECT_NE(backend.metricsSummary().find("1 false starts"), std::string::npos)
        << backend.metricsSummary();
}

TEST(SerialBackendTest, RestartDoesNotGlueAnOldPartialFrameOntoNewBytes) {
    const PtyPair pty;
    ASSERT_TRUE(pty.ok()) << "could not open a PTY pair";

    std::mutex mutex;
    std::vector<FlashFrame> frames;
    SerialBackend backend(
        pty.slavePath(), kBaudRate, [](const SerialReading&) {},
        [&](const FlashFrame& frame) {
            const std::lock_guard<std::mutex> lock(mutex);
            frames.push_back(frame);
        });

    // First session ends in the middle of a frame.
    backend.start();
    const std::vector<std::byte> cut =
        encodeFlashFrame(FlashFrame{kAckType, 3, {}});
    ASSERT_TRUE(pty.writeAll(std::span<const std::byte>(cut).first(3)));
    // Give the read loop time to take in the half frame before stopping.
    std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
    backend.stop();

    // Second session: a whole frame. With stale state the half frame would
    // be read first and rejected, counting a false start.
    backend.start();
    const FlashFrame next{kAckType, 4, {}};
    ASSERT_TRUE(pty.writeAll(encodeFlashFrame(next)));
    const bool got = waitFor([&] {
        const std::lock_guard<std::mutex> lock(mutex);
        return !frames.empty();
    });
    backend.stop();

    ASSERT_TRUE(got) << "the frame of the second session never arrived";
    const std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0], next);
    EXPECT_EQ(backend.falseStarts(), 0U)
        << "the half frame from before stop() was glued onto the new bytes";
}

}  // namespace
