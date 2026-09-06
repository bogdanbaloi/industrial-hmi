#pragma once

#include <QWidget>

#include <functional>
#include <memory>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtSettingsPage;
}

namespace app::config {
class ConfigManager;
}

namespace app::view {

class QtPaletteManager;

/// Configuration page, grouped into Appearance (palette swatches), Display
/// (windowed / fullscreen) and Configuration (a read-only overview of the
/// current ConfigManager state). The palette applies + persists through
/// QtPaletteManager; the display-mode toggle is reported through an injected
/// callback so the page never touches the window directly (DIP), mirroring the
/// GTK settings page's runtime display-mode signal.
class QtSettingsPage : public QWidget {
public:
    /// `onDisplayModeChanged(true)` requests fullscreen, `false` windowed.
    using DisplayModeCallback = std::function<void(bool)>;

    QtSettingsPage(const config::ConfigManager& config,
                   QtPaletteManager& paletteManager,
                   DisplayModeCallback onDisplayModeChanged,
                   QWidget* parent = nullptr);
    ~QtSettingsPage() override;

    QtSettingsPage(const QtSettingsPage&)            = delete;
    QtSettingsPage& operator=(const QtSettingsPage&) = delete;
    QtSettingsPage(QtSettingsPage&&)                 = delete;
    QtSettingsPage& operator=(QtSettingsPage&&)      = delete;

private:
    void populate(const config::ConfigManager& config);

    QtPaletteManager&                   paletteManager_;
    DisplayModeCallback                 onDisplayModeChanged_;
    std::unique_ptr<Ui::QtSettingsPage> ui_;
};

}  // namespace app::view
