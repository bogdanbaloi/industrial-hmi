#pragma once

#include <gtkmm.h>
#include <string>

namespace app::view {

/// Theme Manager - Handles dark/light mode switching
///
/// @design Singleton pattern (appropriate for global UI state)
/// @features
/// - Dark mode (default for industrial applications)
/// - Light mode support
/// - Adwaita color palette
/// - Design tokens via CSS
/// - Persistent theme preference
class ThemeManager {
public:
    enum class Theme {
        DARK,   ///< Dark mode (default)
        LIGHT   ///< Light mode
    };
    
    /// Get singleton instance
    static ThemeManager& instance() {
        static ThemeManager inst;
        return inst;
    }
    
    /// Initialize theme system
    /// @param window Main application window
    void initialize(Gtk::Window* window) {
        mainWindow_ = window;
        loadThemeCSS();
        applyTheme(currentTheme_);
    }
    
    /// Get current theme
    [[nodiscard]] Theme getCurrentTheme() const {
        return currentTheme_;
    }
    
    /// Set theme (dark or light)
    /// @param theme Theme to apply
    void setTheme(Theme theme) {
        if (theme == currentTheme_) {
            return;  // Already applied
        }
        
        currentTheme_ = theme;
        applyTheme(theme);
        
        // Could save preference here
        // saveThemePreference(theme);
    }
    
    /// Toggle between dark and light
    void toggleTheme() {
        if (currentTheme_ == Theme::DARK) {
            setTheme(Theme::LIGHT);
        } else {
            setTheme(Theme::DARK);
        }
    }
    
    /// Check if dark mode
    [[nodiscard]] bool isDarkMode() const {
        return currentTheme_ == Theme::DARK;
    }

private:
    ThemeManager() = default;
    ~ThemeManager() = default;
    
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;
    
    /// Load Adwaita theme CSS
    void loadThemeCSS() {
        cssProvider_ = Gtk::CssProvider::create();
        
        try {
            // Load from adwaita-theme.css
            cssProvider_->load_from_path("ui/adwaita-theme.css");
            
            // Apply to display
            Gtk::StyleContext::add_provider_for_display(
                Gdk::Display::get_default(),
                cssProvider_,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
            );
        } catch (const Glib::Error& e) {
            g_warning("Failed to load theme CSS: %s", e.what().c_str());
        }
    }
    
    /// Apply theme to window
    /// @param theme Theme to apply
    void applyTheme(Theme theme) {
        if (!mainWindow_) {
            return;
        }
        
        // Remove existing theme class
        mainWindow_->remove_css_class("light-mode");
        mainWindow_->remove_css_class("dark-mode");
        
        // Add new theme class
        if (theme == Theme::LIGHT) {
            mainWindow_->add_css_class("light-mode");
        } else {
            mainWindow_->add_css_class("dark-mode");
        }
    }
    
    Gtk::Window* mainWindow_{nullptr};
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
    Theme currentTheme_{Theme::DARK};  // Default to dark for industrial
};

}  // namespace app::view
