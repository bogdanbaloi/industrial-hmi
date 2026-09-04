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

/// Read-only configuration overview page. It reads the current ConfigManager
/// state and renders it as a key/value form. No presenter: settings are a
/// config-direct concern, not a Model/Presenter flow. The ConfigManager is
/// injected (DIP) so the page never reaches the singleton itself. Editing and
/// persistence are a future extension that would not change this display.
class QtSettingsPage : public QWidget {
public:
    explicit QtSettingsPage(const config::ConfigManager& config,
                            QWidget* parent = nullptr);
    ~QtSettingsPage() override;

    QtSettingsPage(const QtSettingsPage&)            = delete;
    QtSettingsPage& operator=(const QtSettingsPage&) = delete;
    QtSettingsPage(QtSettingsPage&&)                 = delete;
    QtSettingsPage& operator=(QtSettingsPage&&)      = delete;

private:
    void populate(const config::ConfigManager& config);

    std::unique_ptr<Ui::QtSettingsPage> ui_;
};

}  // namespace app::view
