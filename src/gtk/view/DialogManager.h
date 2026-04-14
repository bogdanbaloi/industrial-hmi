#pragma once

#include <gtkmm.h>
#include <string>
#include <functional>
#include <memory>

namespace app::view {

/// Centralized dialog manager for consistent UI across the application
///
/// @design Dependency Injection pattern (NOT Singleton!)
/// @threading All methods marshal to GTK main thread for safety
/// @pattern Factory pattern for dialog creation with consistent styling
///
/// Benefits over Singleton:
/// - Explicit dependencies (visible in constructor)
/// - Testable (can inject mock DialogManager)
/// - No global state
/// - Thread-safe by design (no shared mutable state)
///
/// Usage:
///   DialogManager dialogManager(parentWindow);
///   
///   class ProductsPage {
///       DialogManager& dialogManager_;
///   public:
///       ProductsPage(DialogManager& dm) : dialogManager_(dm) {}
///   };
class DialogManager {
public:
    /// Constructor with default parent window
    /// @param defaultParent Default parent for all dialogs (can be nullptr)
    explicit DialogManager(Gtk::Window* defaultParent = nullptr)
        : defaultParent_(defaultParent) {}
    
    // Delete copy (but allow move if needed)
    DialogManager(const DialogManager&) = delete;
    DialogManager& operator=(const DialogManager&) = delete;
    DialogManager(DialogManager&&) = default;
    DialogManager& operator=(DialogManager&&) = default;
    
    ~DialogManager() = default;
    
    /// Dialog types for consistent styling
    enum class Type {
        INFO,       ///< Information message (blue icon)
        WARNING,    ///< Warning message (yellow icon)
        ERROR,      ///< Error message (red icon)
        QUESTION    ///< Question/Confirm (gray icon)
    };
    
    /// Show information dialog
    /// @param title Dialog title
    /// @param message Dialog message
    /// @param parent Parent window (nullptr = find root)
    void showInfo(const std::string& title, 
                  const std::string& message,
                  Gtk::Window* parent = nullptr);
    
    /// Show warning dialog
    /// @param title Dialog title
    /// @param message Dialog message
    /// @param parent Parent window (nullptr = find root)
    void showWarning(const std::string& title,
                     const std::string& message,
                     Gtk::Window* parent = nullptr);
    
    /// Show error dialog
    /// @param title Dialog title
    /// @param message Dialog message
    /// @param parent Parent window (nullptr = find root)
    void showError(const std::string& title,
                   const std::string& message,
                   Gtk::Window* parent = nullptr);
    
    /// Show confirmation dialog (blocking)
    /// @param title Dialog title
    /// @param message Dialog message
    /// @param parent Parent window (nullptr = find root)
    /// @return true if user clicked OK/Yes, false if Cancel/No
    [[nodiscard]] bool showConfirm(const std::string& title,
                     const std::string& message,
                     Gtk::Window* parent = nullptr);
    
    /// Show confirmation dialog (async with callback)
    /// @param title Dialog title
    /// @param message Dialog message
    /// @param callback Called with true if OK, false if Cancel
    /// @param parent Parent window (nullptr = find root)
    void showConfirmAsync(const std::string& title,
                         const std::string& message,
                         std::function<void(bool)> callback,
                         Gtk::Window* parent = nullptr);
    
    /// Show custom input dialog
    /// @param title Dialog title
    /// @param message Dialog prompt message
    /// @param defaultValue Default input value
    /// @param parent Parent window (nullptr = find root)
    /// @return Pair of (ok_clicked, input_value)
    [[nodiscard]] std::pair<bool, std::string> showInput(const std::string& title,
                                          const std::string& message,
                                          const std::string& defaultValue = "",
                                          Gtk::Window* parent = nullptr);
    
    /// Show custom form dialog with multiple fields
    /// @param title Dialog title
    /// @param fields Vector of (label, default_value) pairs
    /// @param parent Parent window (nullptr = find root)
    /// @return Pair of (ok_clicked, field_values)
    [[nodiscard]] std::pair<bool, std::vector<std::string>>
    showForm(const std::string& title,
             const std::vector<std::pair<std::string, std::string>>& fields,
             Gtk::Window* parent = nullptr);

private:
    
    /// Create standard message dialog with consistent styling
    /// @param type Dialog type (info/warning/error/question)
    /// @param title Dialog title
    /// @param message Dialog message
    /// @param parent Parent window
    /// @return MessageDialog pointer (must be presented by caller)
    Gtk::MessageDialog* createMessageDialog(Type type,
                                           const std::string& title,
                                           const std::string& message,
                                           Gtk::Window* parent);
    
    /// Get parent window (uses provided parent or default)
    /// @param parent Preferred parent window
    /// @return Parent window to use (never nullptr if possible)
    Gtk::Window* getParent(Gtk::Window* parent);
    
    /// Marshal callback to GTK main thread
    /// @param callback Function to execute on main thread
    template<typename Func>
    void marshalToMainThread(Func&& callback) {
        Glib::signal_idle().connect_once(std::forward<Func>(callback));
    }
    
    /// Default parent window for dialogs
    Gtk::Window* defaultParent_{nullptr};
};

}  // namespace app::view
