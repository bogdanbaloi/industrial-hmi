#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <gtkmm.h>
#include <memory>

// Forward declarations
namespace app {
    class DashboardPresenter;
    class ProductsPresenter;
    
    namespace view {
        class DashboardPage;
        class ProductsPage;
        class DialogManager;
    }
    
    namespace core {
        class Logger;
        class ExceptionHandler;
    }
}

/// Main application window with sidebar controls
/// 
/// Responsibilities:
/// - Load and manage UI layout from .ui file
/// - Handle display mode switching (fullscreen/windowed)
/// - Coordinate between sidebar controls and main content
/// - Manage keyboard shortcuts
class MainWindow : public Gtk::ApplicationWindow {
public:
    MainWindow();
    ~MainWindow() override;

private:
    // UI initialization
    void loadUI();
    void loadSidebarCSS();
    void setupKeyboardShortcuts();
    void connectSignals();
    void initializePages();
    
    // Event handlers
    void onDisplayModeChanged();
    void onThemeChanged();
    bool onKeyPressed(guint keyval, guint keycode, Gdk::ModifierType state);
    void toggleFullscreen();
    
    // UI state
    bool isFullscreen_ = false;
    
    // Widgets from .ui file
    Gtk::CheckButton* radioFullscreen_ = nullptr;
    Gtk::CheckButton* radioWindowed_ = nullptr;
    Gtk::CheckButton* radioDark_ = nullptr;
    Gtk::CheckButton* radioLight_ = nullptr;
    Gtk::Box* dashboardContainer_ = nullptr;
    Gtk::Box* productsContainer_ = nullptr;
    
    // Services (injected into pages)
    std::unique_ptr<core::Logger> logger_;
    std::unique_ptr<core::ExceptionHandler> exceptionHandler_;
    std::unique_ptr<app::view::DialogManager> dialogManager_;
    
    // MVP components - Dashboard
    std::shared_ptr<app::DashboardPresenter> dashboardPresenter_;
    app::view::DashboardPage* dashboardPage_ = nullptr;
    
    // MVP components - Products
    std::shared_ptr<app::ProductsPresenter> productsPresenter_;
    app::view::ProductsPage* productsPage_ = nullptr;
};

#endif // MAIN_WINDOW_H
