// [utest->req~integration-009~1]
// SerialBackend end-to-end over an emulated serial endpoint: a pseudo-
// terminal (PTY) pair stands in for the microcontroller's virtual COM
// port, so the async read loop is exercised with no real hardware.
//
// POSIX-only (posix_openpt / ptsname); the cross-platform framing logic is
// covered by SerialFrameParserTest. The test opens a PTY master, hands the
// slave device path to the backend, writes frames to the master, and
// asserts the injected sink receives the decoded readings.

#include "src/integration/SerialBackend.h"

#include <gtest/gtest.h>

#include <fcntl.h>   // posix_openpt, O_RDWR, O_NOCTTY
#include <stdlib.h>  // grantpt, unlockpt, ptsname
#include <unistd.h>  // write, close

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using app::integration::SerialBackend;
using app::integration::SerialReading;

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

}  // namespace
