#pragma once

#include <QString>

#include <vector>

namespace app::config {
class ConfigManager;
}

namespace app::view {

/// One palette described by its semantic role colours. Palettes are DATA: one
/// QSS template renders any of them, so adding a palette is one struct, not new
/// code.
struct PaletteDef {
    QString id;           // stable key persisted in config: "light", "dark", ...
    QString displayName;  // shown in the picker
    QString bg;
    QString surface;
    QString surface2;
    QString text;
    QString muted;
    QString border;
    QString accent;
    QString accentInk;    // text/icon on an accent ground
};

/// Runtime palette manager: applies a named palette as an application-wide Qt
/// style sheet (the Qt analog of the GTK ThemeManager CSS palettes, REQ-ARCH-006
/// and ADR-0008) and persists the choice through ConfigManager. Light and Dark
/// are two of the palettes, so choosing them is the light/dark mode.
///
/// SOLID: SRP -- it only builds + applies + persists palettes. DIP -- it takes
/// ConfigManager by reference, it does not reach a singleton. OCP -- a new
/// palette is a new PaletteDef, the QSS builder is unchanged.
class QtPaletteManager {
public:
    explicit QtPaletteManager(config::ConfigManager& config);

    [[nodiscard]] const std::vector<PaletteDef>& palettes() const {
        return palettes_;
    }

    /// Apply by id: set the app-wide style sheet and persist the choice.
    /// Unknown ids are ignored.
    void apply(const QString& id);

    /// Apply the palette stored in config, falling back to the default.
    void applyInitial();

    [[nodiscard]] QString currentId() const { return currentId_; }

private:
    [[nodiscard]] static QString buildStyleSheet(const PaletteDef& palette);

    config::ConfigManager&  config_;
    std::vector<PaletteDef> palettes_;
    QString                 currentId_;
};

}  // namespace app::view
