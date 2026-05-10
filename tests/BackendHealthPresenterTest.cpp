// Tests for app::BackendHealthPresenter.
//
// The presenter has one job: walk an IntegrationManager's backends,
// build a BackendHealthViewModel, and notify observers. We verify
// three things:
//
//   1. Each backend's name() / connectionState() / metricsSummary()
//      lands as one ViewModel entry in registration order.
//   2. Empty manager -> empty ViewModel (no observer is invoked at
//      all? actually it IS invoked with an empty list -- the bar
//      uses the empty list to clear stale dots).
//   3. State changes between polls show up on the next poll, no
//      stale data.
//
// No GTK, no sockets, no real backends -- a tiny FakeBackend stands
// in for TCP/MQTT/OPC-UA so the test stays in pure C++.

#include "src/presenter/BackendHealthPresenter.h"

#include "src/integration/IntegrationBackend.h"
#include "src/integration/IntegrationManager.h"
#include "src/presenter/ViewObserver.h"
#include "src/presenter/modelview/BackendHealthViewModel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using app::BackendHealthPresenter;
using app::ViewObserver;
using app::integration::BackendState;
using app::integration::IntegrationBackend;
using app::integration::IntegrationManager;
using app::presenter::BackendHealthViewModel;

namespace {

/// Minimal IntegrationBackend stub: knobs for every property the
/// presenter reads, no actual I/O. Lifecycle methods are no-ops --
/// the presenter never calls them, only the inspectors.
class FakeBackend final : public IntegrationBackend {
public:
    FakeBackend(std::string name,
                BackendState state,
                std::string metrics)
        : name_(std::move(name)),
          state_(state),
          metrics_(std::move(metrics)) {}

    void start() override {}
    void stop()  override {}
    [[nodiscard]] bool isRunning() const override {
        return state_ == BackendState::Connected;
    }
    [[nodiscard]] std::string name() const override { return name_; }
    [[nodiscard]] BackendState connectionState() const noexcept override {
        return state_;
    }
    [[nodiscard]] std::string metricsSummary() const override {
        return metrics_;
    }

    void setState(BackendState s) noexcept { state_ = s; }
    void setMetrics(std::string m) { metrics_ = std::move(m); }

private:
    std::string  name_;
    BackendState state_;
    std::string  metrics_;
};

/// Capturing observer. Records every received ViewModel for
/// after-the-fact assertions. Lives only in tests.
class RecordingObserver final : public ViewObserver {
public:
    void onBackendHealthChanged(
        const BackendHealthViewModel& viewModel) override {
        captured_.push_back(viewModel);
    }

    [[nodiscard]] const std::vector<BackendHealthViewModel>&
        captured() const noexcept { return captured_; }

private:
    std::vector<BackendHealthViewModel> captured_;
};

/// Build a manager pre-loaded with fake backends. Returns raw
/// pointers to the fakes so tests can mutate state between polls
/// (the manager owns the unique_ptr).
struct ManagedFakes {
    IntegrationManager manager;
    std::vector<FakeBackend*> fakes;

    /// Adopt the fake, register it with the manager, return the raw
    /// pointer so tests can mutate state knobs after registration.
    FakeBackend* add(std::unique_ptr<FakeBackend> fake) {
        auto* raw = fake.get();
        fakes.push_back(raw);
        manager.registerBackend(std::move(fake));
        return raw;
    }
};

TEST(BackendHealthPresenterTest, EmptyManagerEmitsEmptyViewModel) {
    IntegrationManager manager;
    BackendHealthPresenter presenter(manager);
    RecordingObserver observer;
    presenter.addObserver(&observer);

    presenter.poll();

    ASSERT_EQ(observer.captured().size(), 1U);
    EXPECT_TRUE(observer.captured()[0].entries.empty());
}

TEST(BackendHealthPresenterTest, EachBackendBecomesOneViewModelEntry) {
    ManagedFakes fakes;
    fakes.add(std::make_unique<FakeBackend>(
        "TCP", BackendState::Connected, "port 5555 | 1 client"));
    fakes.add(std::make_unique<FakeBackend>(
        "MQTT", BackendState::Connecting, "broker 127.0.0.1:1883 | 0 publishes"));
    fakes.add(std::make_unique<FakeBackend>(
        "OPC-UA", BackendState::Disconnected, ""));

    BackendHealthPresenter presenter(fakes.manager);
    RecordingObserver observer;
    presenter.addObserver(&observer);

    presenter.poll();

    ASSERT_EQ(observer.captured().size(), 1U);
    const auto& vm = observer.captured()[0];
    ASSERT_EQ(vm.entries.size(), 3U);

    EXPECT_EQ(vm.entries[0].name, "TCP");
    EXPECT_EQ(vm.entries[0].state, BackendState::Connected);
    EXPECT_EQ(vm.entries[0].metricsLine, "port 5555 | 1 client");

    EXPECT_EQ(vm.entries[1].name, "MQTT");
    EXPECT_EQ(vm.entries[1].state, BackendState::Connecting);

    EXPECT_EQ(vm.entries[2].name, "OPC-UA");
    EXPECT_EQ(vm.entries[2].state, BackendState::Disconnected);
    EXPECT_TRUE(vm.entries[2].metricsLine.empty());
}

TEST(BackendHealthPresenterTest, RegistrationOrderIsPreserved) {
    ManagedFakes fakes;
    fakes.add(std::make_unique<FakeBackend>("A", BackendState::Connected, ""));
    fakes.add(std::make_unique<FakeBackend>("B", BackendState::Connected, ""));
    fakes.add(std::make_unique<FakeBackend>("C", BackendState::Connected, ""));

    BackendHealthPresenter presenter(fakes.manager);
    RecordingObserver observer;
    presenter.addObserver(&observer);
    presenter.poll();

    const auto& vm = observer.captured().back();
    ASSERT_EQ(vm.entries.size(), 3U);
    EXPECT_EQ(vm.entries[0].name, "A");
    EXPECT_EQ(vm.entries[1].name, "B");
    EXPECT_EQ(vm.entries[2].name, "C");
}

TEST(BackendHealthPresenterTest, StateChangesPropagateAcrossPolls) {
    ManagedFakes fakes;
    auto* mqtt = fakes.add(std::make_unique<FakeBackend>(
        "MQTT", BackendState::Connecting, "handshake"));

    BackendHealthPresenter presenter(fakes.manager);
    RecordingObserver observer;
    presenter.addObserver(&observer);

    presenter.poll();
    mqtt->setState(BackendState::Connected);
    mqtt->setMetrics("broker live");
    presenter.poll();
    mqtt->setState(BackendState::Degraded);
    mqtt->setMetrics("broker dropped");
    presenter.poll();

    ASSERT_EQ(observer.captured().size(), 3U);
    EXPECT_EQ(observer.captured()[0].entries[0].state,
              BackendState::Connecting);
    EXPECT_EQ(observer.captured()[1].entries[0].state,
              BackendState::Connected);
    EXPECT_EQ(observer.captured()[1].entries[0].metricsLine, "broker live");
    EXPECT_EQ(observer.captured()[2].entries[0].state,
              BackendState::Degraded);
    EXPECT_EQ(observer.captured()[2].entries[0].metricsLine, "broker dropped");
}

TEST(BackendHealthPresenterTest, MultipleObserversAllReceiveViewModel) {
    ManagedFakes fakes;
    fakes.add(std::make_unique<FakeBackend>(
        "TCP", BackendState::Connected, ""));
    BackendHealthPresenter presenter(fakes.manager);
    RecordingObserver observer1;
    RecordingObserver observer2;
    presenter.addObserver(&observer1);
    presenter.addObserver(&observer2);

    presenter.poll();

    EXPECT_EQ(observer1.captured().size(), 1U);
    EXPECT_EQ(observer2.captured().size(), 1U);
}

TEST(BackendHealthPresenterTest, RemovedObserverNoLongerReceives) {
    ManagedFakes fakes;
    fakes.add(std::make_unique<FakeBackend>(
        "TCP", BackendState::Connected, ""));
    BackendHealthPresenter presenter(fakes.manager);
    RecordingObserver observer;
    presenter.addObserver(&observer);
    presenter.poll();
    presenter.removeObserver(&observer);
    presenter.poll();

    EXPECT_EQ(observer.captured().size(), 1U);
}

}  // namespace
