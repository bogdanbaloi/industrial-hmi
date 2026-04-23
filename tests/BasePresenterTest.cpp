// Tests for app::BasePresenter
// Covers observer registration, removal, duplicate-prevention, and the two
// notifyAll overloads (member-function pointer + callable).

#include "src/presenter/BasePresenter.h"
#include "src/presenter/ViewObserver.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// Concrete BasePresenter so we can instantiate it - the production base is
// abstract because of pure-virtual initialize().
class ConcretePresenter : public app::BasePresenter {
public:
    void initialize() override {}

    // Expose notifyAll for testing
    template <typename Callable>
    void fireAll(Callable&& fn) {
        notifyAll(std::forward<Callable>(fn));
    }

    template <typename MethodPtr, typename... Args>
    void fireAll(MethodPtr m, Args&&... args) {
        notifyAll(m, std::forward<Args>(args)...);
    }
};

// Observer that records the few callbacks the tests use. ViewObserver's
// base supplies empty defaults for every method, so we only override the
// ones we actually inspect.
class RecordingObserver : public app::ViewObserver {
public:
    int productsLoadedCount = 0;
    std::string lastError;

    void onProductsLoaded(const app::presenter::ProductsViewModel&) override {
        ++productsLoadedCount;
    }
    void onError(const std::string& message) override { lastError = message; }
};

}  // namespace

// Observer registration

TEST(BasePresenterTest, AddObserverThenNotifyDeliversCallback) {
    ConcretePresenter p;
    RecordingObserver obs;
    p.addObserver(&obs);

    p.fireAll([](app::ViewObserver* o) {
        o->onProductsLoaded(app::presenter::ProductsViewModel{});
    });

    EXPECT_EQ(obs.productsLoadedCount, 1);
}

TEST(BasePresenterTest, AddingSameObserverTwiceIsIdempotent) {
    ConcretePresenter p;
    RecordingObserver obs;
    p.addObserver(&obs);
    p.addObserver(&obs);

    p.fireAll([](app::ViewObserver* o) {
        o->onProductsLoaded(app::presenter::ProductsViewModel{});
    });

    // Should only fire once even though we tried to register twice
    EXPECT_EQ(obs.productsLoadedCount, 1);
}

TEST(BasePresenterTest, AddObserverIgnoresNullptr) {
    ConcretePresenter p;
    EXPECT_NO_THROW(p.addObserver(nullptr));

    // Notifying with no observers shouldn't crash either
    EXPECT_NO_THROW(p.fireAll([](app::ViewObserver*) {}));
}

// Observer removal

TEST(BasePresenterTest, RemoveObserverStopsDelivery) {
    ConcretePresenter p;
    RecordingObserver obs;
    p.addObserver(&obs);
    p.removeObserver(&obs);

    p.fireAll([](app::ViewObserver* o) {
        o->onProductsLoaded(app::presenter::ProductsViewModel{});
    });

    EXPECT_EQ(obs.productsLoadedCount, 0);
}

TEST(BasePresenterTest, RemoveObserverThatWasNeverRegisteredIsSafe) {
    ConcretePresenter p;
    RecordingObserver obs;
    EXPECT_NO_THROW(p.removeObserver(&obs));
    EXPECT_NO_THROW(p.removeObserver(nullptr));
}

// Multi-observer dispatch

TEST(BasePresenterTest, NotifyAllReachesEveryRegisteredObserver) {
    ConcretePresenter p;
    RecordingObserver a;
    RecordingObserver b;
    RecordingObserver c;
    p.addObserver(&a);
    p.addObserver(&b);
    p.addObserver(&c);

    p.fireAll([](app::ViewObserver* o) {
        o->onProductsLoaded(app::presenter::ProductsViewModel{});
    });

    EXPECT_EQ(a.productsLoadedCount, 1);
    EXPECT_EQ(b.productsLoadedCount, 1);
    EXPECT_EQ(c.productsLoadedCount, 1);
}

TEST(BasePresenterTest, NotifyAllByMemberPointerForwardsArguments) {
    ConcretePresenter p;
    RecordingObserver obs;
    p.addObserver(&obs);

    // notifyAll(method-pointer, args...) overload
    p.fireAll(&app::ViewObserver::onError, std::string{"boom"});

    EXPECT_EQ(obs.lastError, "boom");
}
