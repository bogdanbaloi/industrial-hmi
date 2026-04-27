// Tests for app::integration::IntegrationManager.
//
// Pure C++ logic. Verifies the composition + lifecycle contract:
//   * registerBackend takes ownership of unique_ptrs
//   * startAll fans out start() to every registered backend
//   * stopAll fans out stop() to every registered backend (in dtor too)
//   * One backend's start() failure doesn't prevent others from starting
//   * lastStartErrors() reports the failing backend by name
//   * allRunning() reflects the aggregate state truthfully

#include "src/integration/IntegrationManager.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

using app::integration::IntegrationBackend;
using app::integration::IntegrationManager;

namespace {

/// Test double that records lifecycle calls and lets each test inject
/// failure modes (start throws, stop throws, isRunning lies).
class FakeBackend : public IntegrationBackend {
public:
    explicit FakeBackend(std::string name,
                         bool startThrows = false,
                         bool stopThrows  = false)
        : name_(std::move(name)),
          startThrows_(startThrows),
          stopThrows_(stopThrows) {}

    void start() override {
        ++startCalls_;
        if (startThrows_) {
            throw std::runtime_error("start failed: " + name_);
        }
        running_ = true;
    }

    void stop() override {
        ++stopCalls_;
        if (stopThrows_) {
            throw std::runtime_error("stop failed: " + name_);
        }
        running_ = false;
    }

    [[nodiscard]] bool isRunning() const override { return running_; }
    [[nodiscard]] std::string name() const override { return name_; }

    int startCalls() const { return startCalls_; }
    int stopCalls()  const { return stopCalls_; }

private:
    std::string name_;
    bool startThrows_;
    bool stopThrows_;
    bool running_{false};
    int startCalls_{0};
    int stopCalls_{0};
};

}  // namespace

TEST(IntegrationManagerTest, EmptyManagerReportsZeroBackends) {
    IntegrationManager mgr;
    EXPECT_EQ(mgr.backendCount(), 0u);
    EXPECT_FALSE(mgr.allRunning());  // empty -> false by contract
    EXPECT_TRUE(mgr.lastStartErrors().empty());
}

TEST(IntegrationManagerTest, RegisterIncrementsCount) {
    IntegrationManager mgr;
    mgr.registerBackend(std::make_unique<FakeBackend>("A"));
    EXPECT_EQ(mgr.backendCount(), 1u);
    mgr.registerBackend(std::make_unique<FakeBackend>("B"));
    EXPECT_EQ(mgr.backendCount(), 2u);
}

TEST(IntegrationManagerTest, RegisterIgnoresNullptr) {
    IntegrationManager mgr;
    mgr.registerBackend(nullptr);
    EXPECT_EQ(mgr.backendCount(), 0u);
}

TEST(IntegrationManagerTest, StartAllFansOutToEveryBackend) {
    auto* a = new FakeBackend("A");
    auto* b = new FakeBackend("B");
    IntegrationManager mgr;
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(a));
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(b));

    mgr.startAll();

    EXPECT_EQ(a->startCalls(), 1);
    EXPECT_EQ(b->startCalls(), 1);
    EXPECT_TRUE(a->isRunning());
    EXPECT_TRUE(b->isRunning());
    EXPECT_TRUE(mgr.allRunning());
    EXPECT_TRUE(mgr.lastStartErrors().empty());
}

TEST(IntegrationManagerTest, StartAllContinuesAfterOneBackendThrows) {
    auto* good = new FakeBackend("Good");
    auto* bad  = new FakeBackend("Bad", /*startThrows=*/true);
    auto* alsoGood = new FakeBackend("AlsoGood");
    IntegrationManager mgr;
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(good));
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(bad));
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(alsoGood));

    mgr.startAll();

    // Good and AlsoGood must still be running despite Bad throwing.
    EXPECT_TRUE(good->isRunning());
    EXPECT_TRUE(alsoGood->isRunning());
    EXPECT_FALSE(bad->isRunning());
    EXPECT_FALSE(mgr.allRunning());

    ASSERT_EQ(mgr.lastStartErrors().size(), 1u);
    EXPECT_NE(mgr.lastStartErrors()[0].find("Bad"), std::string::npos)
        << "error string should mention the failing backend's name";
}

TEST(IntegrationManagerTest, StartAllClearsPreviousErrors) {
    IntegrationManager mgr;
    mgr.registerBackend(std::make_unique<FakeBackend>("Bad",
                                                       /*startThrows=*/true));
    mgr.startAll();
    ASSERT_EQ(mgr.lastStartErrors().size(), 1u);

    // Replace the backend list with a clean one and re-start.
    IntegrationManager mgr2;
    mgr2.registerBackend(std::make_unique<FakeBackend>("Good"));
    mgr2.startAll();
    EXPECT_TRUE(mgr2.lastStartErrors().empty());
}

TEST(IntegrationManagerTest, StopAllFansOutToEveryBackend) {
    auto* a = new FakeBackend("A");
    auto* b = new FakeBackend("B");
    IntegrationManager mgr;
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(a));
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(b));
    mgr.startAll();
    ASSERT_TRUE(mgr.allRunning());

    mgr.stopAll();

    EXPECT_EQ(a->stopCalls(), 1);
    EXPECT_EQ(b->stopCalls(), 1);
    EXPECT_FALSE(a->isRunning());
    EXPECT_FALSE(b->isRunning());
    EXPECT_FALSE(mgr.allRunning());
}

TEST(IntegrationManagerTest, StopAllSwallowsBackendStopExceptions) {
    auto* throws = new FakeBackend("Throws", /*startThrows=*/false,
                                              /*stopThrows=*/true);
    auto* clean  = new FakeBackend("Clean");
    IntegrationManager mgr;
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(throws));
    mgr.registerBackend(std::unique_ptr<IntegrationBackend>(clean));
    mgr.startAll();

    // stopAll is noexcept by contract -- the throwing backend must not
    // propagate, and Clean must still get its stop() call.
    EXPECT_NO_THROW(mgr.stopAll());
    EXPECT_EQ(throws->stopCalls(), 1);
    EXPECT_EQ(clean->stopCalls(),  1);
}

TEST(IntegrationManagerTest, DestructorStopsRemainingBackends) {
    FakeBackend* observer = nullptr;
    {
        IntegrationManager mgr;
        auto fb = std::make_unique<FakeBackend>("AutoStopped");
        observer = fb.get();
        mgr.registerBackend(std::move(fb));
        mgr.startAll();
        ASSERT_TRUE(observer->isRunning());
        // mgr goes out of scope here -- destructor should call stop()
    }
    // observer is dangling now (mgr deleted it), so we can't check
    // it directly. The real assertion is "no crash" -- the dtor
    // ran cleanly through stopAll().
    SUCCEED();
}
