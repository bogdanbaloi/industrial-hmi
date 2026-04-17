#pragma once

#include "src/gtk/view/DialogManager.h"

#include <gmock/gmock.h>

namespace app::test {

/// gmock-backed DialogManager for View-layer tests.
///
/// Every dialog call is captured as a mock expectation instead of opening
/// a real GTK dialog. Tests can verify titles/messages and invoke the
/// callback argument of showConfirmAsync to simulate user confirmation.
class MockDialogManager : public view::DialogManager {
public:
    MockDialogManager() : DialogManager(nullptr) {}

    MOCK_METHOD(void, showInfo,
                (const std::string& title, const std::string& message,
                 Gtk::Window* parent),
                (override));

    MOCK_METHOD(void, showWarning,
                (const std::string& title, const std::string& message,
                 Gtk::Window* parent),
                (override));

    MOCK_METHOD(void, showError,
                (const std::string& title, const std::string& message,
                 Gtk::Window* parent),
                (override));

    MOCK_METHOD(bool, showConfirm,
                (const std::string& title, const std::string& message,
                 Gtk::Window* parent),
                (override));

    MOCK_METHOD(void, showConfirmAsync,
                (const std::string& title, const std::string& message,
                 std::function<void(bool)> callback, Gtk::Window* parent),
                (override));

    MOCK_METHOD((std::pair<bool, std::string>), showInput,
                (const std::string& title, const std::string& message,
                 const std::string& defaultValue, Gtk::Window* parent),
                (override));

    // Type alias avoids unprotected commas inside the MOCK_METHOD macro.
    using FormFields = std::vector<std::pair<std::string, std::string>>;

    MOCK_METHOD((std::pair<bool, std::vector<std::string>>), showForm,
                (const std::string& title,
                 const FormFields& fields,
                 Gtk::Window* parent),
                (override));
};

}  // namespace app::test
