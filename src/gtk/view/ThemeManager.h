#pragma once

#include <gtkmm.h>
#include <filesystem>
#include <string>
#include "src/config/config_defaults.h"
#include "src/gtk/view/css_classes.h"

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
    
    [[nodiscard]] bool isDarkMode() const {
        return currentTheme_ == Theme::DARK;
    }

    /// Switch the active color palette. Empty / "industrial" loads no
    /// extra stylesheet and the app keeps the baseline look; any other
    /// id loads `assets/styles/themes/<id>.css` at higher priority on
    /// top of the base CSS so it redefines colors without touching
    /// layout.
    void setPalette(const std::string& paletteId) {
        if (paletteId == currentPalette_) return;
        currentPalette_ = paletteId;
        applyPalette();
    }

    [[nodiscard]] const std::string& getPalette() const {
        return currentPalette_;
    }

    /// Apply current theme CSS class to a dialog window
    void applyToDialog(Gtk::Window* dialog) {
        if (!dialog) return;
        dialog->add_css_class(currentTheme_ == Theme::LIGHT
                              ? css::kLightMode : css::kDarkMode);

        // Custom titlebar that respects our theme
        auto* titlebar = Gtk::make_managed<Gtk::HeaderBar>();
        titlebar->add_css_class(currentTheme_ == Theme::LIGHT
                                ? css::kDialogTitlebarLight
                                : css::kDialogTitlebarDark);
        dialog->set_titlebar(*titlebar);
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
            cssProvider_->load_from_path(app::config::defaults::kThemeCSS);
            
            // Apply to display
            Gtk::StyleContext::add_provider_for_display(
                Gdk::Display::get_default(),
                cssProvider_,
                GTK_STYLE_PROVIDER_PRIORITY_USER
            );
        } catch (const Glib::Error& e) {
            g_warning("Failed to load theme CSS: %s", e.what());
        }
    }
    
    /// Apply theme to window
    /// @param theme Theme to apply
    void applyTheme(Theme theme) {
        if (!mainWindow_) {
            return;
        }

        const auto* adding   = theme == Theme::LIGHT ? css::kLightMode : css::kDarkMode;
        const auto* removing = theme == Theme::LIGHT ? css::kDarkMode : css::kLightMode;
        mainWindow_->remove_css_class(removing);
        if (!mainWindow_->has_css_class(adding)) {
            mainWindow_->add_css_class(adding);
        }
    }
    
    // Remove the previous palette provider (if any) and, if the new
    // palette id is non-empty and has a matching file, load it at a
    // higher priority than the base sidebar.css so its rules win.
    void applyPalette() {
        auto* display = Gdk::Display::get_default().get();
        if (!display) return;

        if (paletteProvider_) {
            Gtk::StyleContext::remove_provider_for_display(
                Gdk::Display::get_default(), paletteProvider_);
            paletteProvider_.reset();
        }

        if (currentPalette_.empty() || currentPalette_ == "industrial") {
            return;  // Baseline look — nothing extra to load.
        }

        const std::string path =
            std::string(app::config::defaults::kPaletteDir) + "/"
            + currentPalette_ + ".css";
        if (!std::filesystem::exists(path)) {
            g_warning("Palette '%s' not found at %s — keeping previous look",
                      currentPalette_.c_str(), path.c_str());
            currentPalette_.clear();
            return;
        }

        paletteProvider_ = Gtk::CssProvider::create();
        try {
            paletteProvider_->load_from_path(path);
            Gtk::StyleContext::add_provider_for_display(
                Gdk::Display::get_default(),
                paletteProvider_,
                GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
        } catch (const Glib::Error& e) {
            g_warning("Failed to load palette CSS '%s': %s",
                      path.c_str(), e.what());
            paletteProvider_.reset();
            currentPalette_.clear();
        }
    }

    Gtk::Window* mainWindow_{nullptr};
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
    Glib::RefPtr<Gtk::CssProvider> paletteProvider_;
    Theme currentTheme_{Theme::DARK};  // Default to dark for industrial
    std::string currentPalette_;       // Empty = baseline industrial
};

}  // namespace app::view
