#pragma once

#include <QWidget>

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

/// Configuration page. The palette picker (light / dark / themed) applies +
/// persists through QtPaletteManager; the rest is a read-only overview of the
/// current ConfigManager state. Both collaborators are injected (DIP); the page
/// reaches no singleton itself. Editing the other settings is a future
/// extension that would not change this structure.
class QtSettingsPage : public QWidget {
public:
    QtSettingsPage(const config::ConfigManager& config,
                   QtPaletteManager& paletteManager, QWidget* parent = nullptr);
    ~QtSettingsPage() override;

    QtSettingsPage(const QtSettingsPage&)            = delete;
    QtSettingsPage& operator=(const QtSettingsPage&) = delete;
    QtSettingsPage(QtSettingsPage&&)                 = delete;
    QtSettingsPage& operator=(QtSettingsPage&&)      = delete;

private:
    void populate(const config::ConfigManager& config);

    QtPaletteManager&                   paletteManager_;
    std::unique_ptr<Ui::QtSettingsPage> ui_;
};

}  // namespace app::view
