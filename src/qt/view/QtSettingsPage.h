#pragma once

#include <QWidget>

#include <functional>
#include <memory>
#include <string>

class QEvent;

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

    /// Reports a language pick (a LINGUAS code or "auto") so the composition
    /// root rebinds the shared catalog. The page never touches i18n directly.
    using LanguageChangeCallback = std::function<void(const std::string&)>;

    QtSettingsPage(const config::ConfigManager& config,
                   QtPaletteManager& paletteManager,
                   DisplayModeCallback onDisplayModeChanged,
                   LanguageChangeCallback onLanguageChanged = {},
                   QWidget* parent = nullptr);
    ~QtSettingsPage() override;

    QtSettingsPage(const QtSettingsPage&)            = delete;
    QtSettingsPage& operator=(const QtSettingsPage&) = delete;
    QtSettingsPage(QtSettingsPage&&)                 = delete;
    QtSettingsPage& operator=(QtSettingsPage&&)      = delete;

protected:
    /// Rebuild the localised rows when the app broadcasts a language change.
    void changeEvent(QEvent* event) override;

private:
    void populate();
    void buildLanguageCombo();

    const config::ConfigManager&        config_;
    QtPaletteManager&                   paletteManager_;
    DisplayModeCallback                 onDisplayModeChanged_;
    LanguageChangeCallback              onLanguageChanged_;
    std::unique_ptr<Ui::QtSettingsPage> ui_;
};

}  // namespace app::view
