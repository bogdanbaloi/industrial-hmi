# Unit Testing with Dependency Injection

This document demonstrates how the Dependency Injection pattern makes this codebase highly testable.

## Why Dependency Injection Enables Testing

### ❌ WITHOUT DI (Singleton - Hard to Test):

```cpp
class ProductsPage {
    void onError() {
        auto& dm = DialogManager::instance();  // ❌ Hardcoded dependency
        dm.showError("Error", "Message");
    }
};

// IMPOSSIBLE TO TEST:
TEST(ProductsPageTest, ShowsError) {
    ProductsPage page;
    page.onError();
    
    // ❌ How do we verify DialogManager::instance().showError() was called?
    // ❌ Can't inject mock - it's a global singleton!
}
```

### ✅ WITH DI (Testable):

```cpp
class ProductsPage {
    DialogManager& dialogManager_;  // ✅ Injected dependency
public:
    ProductsPage(DialogManager& dm) : dialogManager_(dm) {}
    
    void onError() {
        dialogManager_.showError("Error", "Message");  // ✅ Uses injected instance
    }
};

// EASY TO TEST:
TEST(ProductsPageTest, ShowsError) {
    MockDialogManager mockDM;  // ✅ Create mock
    ProductsPage page(mockDM);  // ✅ Inject mock
    
    page.onError();
    
    // ✅ Verify mock was called correctly
    EXPECT_TRUE(mockDM.errorWasShown);
    EXPECT_EQ(mockDM.lastErrorTitle, "Error");
    EXPECT_EQ(mockDM.lastErrorMessage, "Message");
}
```

---

## Example Test Suite

Below are example tests demonstrating how to test this codebase. 

**Note:** These are **demonstration tests** showing the *pattern*, not a full test suite. In production, you would use Google Test (gtest) or a similar framework.

### Mock DialogManager

```cpp
// tests/mocks/MockDialogManager.h
#pragma once

#include "src/gtk/view/DialogManager.h"
#include <string>
#include <functional>

namespace app::test {

/// Mock DialogManager for testing
/// 
/// Records all dialog calls for verification in tests.
/// No actual GTK dialogs shown during testing.
class MockDialogManager : public view::DialogManager {
public:
    MockDialogManager() : DialogManager(nullptr) {}
    
    // Track error dialogs
    bool errorShown{false};
    std::string lastErrorTitle;
    std::string lastErrorMessage;
    
    // Track confirmations
    bool confirmShown{false};
    std::string lastConfirmTitle;
    std::string lastConfirmMessage;
    std::function<void(bool)> lastConfirmCallback;
    
    // Override methods to record calls instead of showing dialogs
    void showError(const std::string& title, 
                   const std::string& message,
                   Gtk::Window* parent = nullptr) override {
        errorShown = true;
        lastErrorTitle = title;
        lastErrorMessage = message;
        // Don't show actual dialog
    }
    
    void showConfirmAsync(const std::string& title,
                         const std::string& message,
                         std::function<void(bool)> callback,
                         Gtk::Window* parent = nullptr) override {
        confirmShown = true;
        lastConfirmTitle = title;
        lastConfirmMessage = message;
        lastConfirmCallback = callback;
        // Don't show actual dialog
    }
    
    // Helper: Simulate user clicking OK on confirmation
    void simulateConfirmOK() {
        if (lastConfirmCallback) {
            lastConfirmCallback(true);
        }
    }
    
    // Helper: Simulate user clicking Cancel
    void simulateConfirmCancel() {
        if (lastConfirmCallback) {
            lastConfirmCallback(false);
        }
    }
    
    // Reset state between tests
    void reset() {
        errorShown = false;
        confirmShown = false;
        lastErrorTitle.clear();
        lastErrorMessage.clear();
        lastConfirmTitle.clear();
        lastConfirmMessage.clear();
        lastConfirmCallback = nullptr;
    }
};

}  // namespace app::test
```

### Example Tests for ProductsPage

```cpp
// tests/ProductsPageTest.cpp
#include "mocks/MockDialogManager.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/presenter/ProductsPresenter.h"
#include <gtest/gtest.h>

using namespace app;
using namespace app::test;

class ProductsPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockDialogManager = std::make_unique<MockDialogManager>();
        presenter = std::make_shared<ProductsPresenter>();
        
        // ProductsPage with injected mock
        page = std::make_unique<view::ProductsPage>(*mockDialogManager);
        page->initialize(presenter);
    }
    
    void TearDown() override {
        page.reset();
        presenter.reset();
        mockDialogManager.reset();
    }
    
    std::unique_ptr<MockDialogManager> mockDialogManager;
    std::shared_ptr<ProductsPresenter> presenter;
    std::unique_ptr<view::ProductsPage> page;
};

TEST_F(ProductsPageTest, ShowsErrorOnAddProductFailure) {
    // Arrange: Try to add product with duplicate code
    // (PROD-001 already exists in demo data)
    
    // Act: Attempt to add duplicate
    bool success = presenter->addProduct("PROD-001", "Duplicate", "Active", 100, 95.0);
    
    // Assert: Operation failed
    EXPECT_FALSE(success);
    
    // Assert: Error dialog was shown
    EXPECT_TRUE(mockDialogManager->errorShown);
    EXPECT_EQ(mockDialogManager->lastErrorTitle, "Error");
    EXPECT_THAT(mockDialogManager->lastErrorMessage, 
                ::testing::HasSubstr("already exist"));
}

TEST_F(ProductsPageTest, ShowsConfirmDialogOnDelete) {
    // Arrange: Select a product
    int productId = 1;
    
    // Act: Click delete button
    // (In real test, would simulate button click)
    // For demo, directly call the handler
    page->onDeleteProductClicked();  // Assumes product 1 selected
    
    // Assert: Confirmation dialog was shown
    EXPECT_TRUE(mockDialogManager->confirmShown);
    EXPECT_EQ(mockDialogManager->lastConfirmTitle, "Confirm Delete");
    EXPECT_THAT(mockDialogManager->lastConfirmMessage, 
                ::testing::HasSubstr("soft delete"));
}

TEST_F(ProductsPageTest, DeletesProductOnConfirmation) {
    // Arrange: Product exists
    auto product = presenter->getProduct(1);
    ASSERT_NE(product.id, -1);
    
    // Act: User confirms deletion
    page->onDeleteProductClicked();
    mockDialogManager->simulateConfirmOK();  // ✅ Simulate user clicking OK
    
    // Assert: Product was deleted (soft delete)
    auto deletedProduct = presenter->getProduct(1);
    EXPECT_NE(deletedProduct.id, -1);  // Still exists
    EXPECT_FALSE(deletedProduct.deletedAt.empty());  // But marked deleted
}

TEST_F(ProductsPageTest, DoesNotDeleteOnCancel) {
    // Arrange: Product exists
    auto productBefore = presenter->getProduct(1);
    ASSERT_NE(productBefore.id, -1);
    
    // Act: User cancels deletion
    page->onDeleteProductClicked();
    mockDialogManager->simulateConfirmCancel();  // ✅ Simulate Cancel
    
    // Assert: Product NOT deleted
    auto productAfter = presenter->getProduct(1);
    EXPECT_EQ(productAfter.id, productBefore.id);
    EXPECT_TRUE(productAfter.deletedAt.empty());  // Still active
}
```

### Example Tests for DashboardPage

```cpp
// tests/DashboardPageTest.cpp
#include "mocks/MockDialogManager.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/presenter/DashboardPresenter.h"
#include <gtest/gtest.h>

using namespace app;
using namespace app::test;

class DashboardPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockDialogManager = std::make_unique<MockDialogManager>();
        presenter = std::make_shared<DashboardPresenter>();
        
        // DashboardPage with injected mock
        page = std::make_unique<view::DashboardPage>(*mockDialogManager);
        page->initialize(presenter);
    }
    
    std::unique_ptr<MockDialogManager> mockDialogManager;
    std::shared_ptr<DashboardPresenter> presenter;
    std::unique_ptr<view::DashboardPage> page;
};

TEST_F(DashboardPageTest, ShowsConfirmDialogOnReset) {
    // Act: Click reset button
    page->onResetButtonClicked();
    
    // Assert: Confirmation shown
    EXPECT_TRUE(mockDialogManager->confirmShown);
    EXPECT_EQ(mockDialogManager->lastConfirmTitle, "Confirm Reset");
    EXPECT_THAT(mockDialogManager->lastConfirmMessage,
                ::testing::HasSubstr("stop all operations"));
}

TEST_F(DashboardPageTest, ShowsConfirmDialogOnCalibration) {
    // Act: Click calibration button
    page->onCalibrationButtonClicked();
    
    // Assert: Confirmation shown
    EXPECT_TRUE(mockDialogManager->confirmShown);
    EXPECT_EQ(mockDialogManager->lastConfirmTitle, "Confirm Calibration");
    EXPECT_THAT(mockDialogManager->lastConfirmMessage,
                ::testing::HasSubstr("calibration procedure"));
}

TEST_F(DashboardPageTest, ExecutesResetOnConfirmation) {
    // Arrange: System running
    
    // Act: User confirms reset
    page->onResetButtonClicked();
    mockDialogManager->simulateConfirmOK();
    
    // Assert: Presenter's reset method was called
    // (Would need to mock presenter too for full verification)
}
```

---

## Running Tests

### Build with Tests Enabled

```bash
# CMakeLists.txt addition:
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    
    add_executable(industrial_hmi_tests
        tests/ProductsPageTest.cpp
        tests/DashboardPageTest.cpp
    )
    
    target_link_libraries(industrial_hmi_tests
        GTest::GTest
        GTest::Main
        objectsView
        objectsPresenter
        objectsModel
    )
    
    add_test(NAME AllTests COMMAND industrial_hmi_tests)
endif()
```

### Run Tests

```bash
cd build
cmake -DBUILD_TESTS=ON ..
ninja
./industrial_hmi_tests

# Or with CTest:
ctest --output-on-failure
```

---

## Key Benefits of This Testing Approach

### ✅ **Testability Through DI**
- Mock objects easily injected
- No global state to manage
- Tests run in isolation

### ✅ **Comprehensive Coverage**
- UI logic (button clicks, dialogs)
- Business logic (CRUD operations)
- Error handling
- User workflows

### ✅ **Fast Tests**
- No actual GTK dialogs shown
- No database persistence (in-memory)
- Run in milliseconds

### ✅ **Maintainable**
- Clear arrange-act-assert structure
- Descriptive test names
- Easy to understand failures

---

## Interview Talking Points

**Q:** "How do you test UI code?"

**A:** "I use Dependency Injection to make UI code testable. For example, ProductsPage takes a DialogManager reference in its constructor. In production, I inject the real DialogManager. In tests, I inject a MockDialogManager that records calls instead of showing actual dialogs. This lets me verify:
- Error dialogs shown on failures
- Confirmation dialogs shown before dangerous operations
- User interactions trigger correct business logic

Without DI, this would be impossible - you can't mock a Singleton."

**Q:** "Show me a test example."

**A:** "Here's a test verifying delete confirmation:
```cpp
TEST(ProductsPageTest, ShowsConfirmDialogOnDelete) {
    MockDialogManager mockDM;
    ProductsPage page(mockDM);  // Inject mock
    
    page.onDeleteProductClicked();
    
    EXPECT_TRUE(mockDM.confirmShown);  // Dialog was shown
    EXPECT_THAT(mockDM.lastConfirmMessage, 
                HasSubstr(\"soft delete\"));  // Correct message
}
```
This proves the UI shows proper warnings before destructive operations."

---

## Production Testing Strategy

In a real production environment, the test suite would include:

1. **Unit Tests** (shown above)
   - Mock all dependencies
   - Test individual components

2. **Integration Tests**
   - Real database (test DB)
   - Real presenters
   - Mock only UI

3. **End-to-End Tests**
   - Full application stack
   - Real GTK windows (headless)
   - Selenium/automated UI testing

4. **Property-Based Tests**
   - Generate random inputs
   - Verify invariants hold

5. **Performance Tests**
   - Load testing
   - Memory leak detection
   - Response time validation

---

**This demonstrates professional software engineering practices!** ✅
