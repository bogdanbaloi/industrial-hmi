#include "src/config/ConfigManager.h"

#include "src/qt/view/QtPaletteManager.h"

#include <QApplication>

namespace app::view {

namespace {

std::vector<PaletteDef> makePalettes() {
    return {
        {"light", "Light", "#eef1f5", "#ffffff", "#f6f8fa", "#1b2432",
         "#667085", "#e0e5ec", "#35688f", "#ffffff"},
        {"dark", "Dark", "#12161d", "#1b212b", "#161b23", "#e7ebf2", "#8a94a3",
         "#29313d", "#4f93d2", "#0b1017"},
        {"nord", "Nord", "#e7ecf3", "#ffffff", "#eef2f7", "#2e3440", "#4c566a",
         "#d5dde8", "#5e81ac", "#ffffff"},
        {"cockpit", "Cockpit", "#14130d", "#1e1c14", "#17150f", "#ece6d4",
         "#9c9377", "#2c2819", "#e0a326", "#14130d"},
    };
}

}  // namespace

QtPaletteManager::QtPaletteManager(config::ConfigManager& config)
    : config_(config), palettes_(makePalettes()) {}

QString QtPaletteManager::buildStyleSheet(const PaletteDef& palette) {
    // Chrome QSS driven by the palette's semantic roles. Per-widget semantic
    // status colours (ok / warn / alarm) stay on the cards, so severity reads
    // consistently across palettes. Token order matters: longer tokens
    // (surface2, accentInk) are replaced before their prefixes.
    QString qss = QStringLiteral(R"QSS(
        QWidget { background: @bg; color: @text; }
        QMainWindow { background: @bg; }
        QTabWidget::pane { border: 1px solid @border; background: @bg; }
        QTabBar::tab { background: @surface2; color: @muted;
            padding: 7px 14px; border: 1px solid @border; border-bottom: none; }
        QTabBar::tab:selected { background: @surface; color: @text; }
        QGroupBox { background: @surface; border: 1px solid @border;
            border-radius: 8px; margin-top: 10px; padding-top: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px;
            padding: 0 4px; color: @muted; }
        QLabel { background: transparent; }
        QPushButton { background: @accent; color: @accentInk; border: none;
            border-radius: 6px; padding: 6px 14px; }
        QPushButton:disabled { background: @muted; color: @surface; }
        QComboBox { background: @surface; color: @text; border: 1px solid @border;
            border-radius: 6px; padding: 4px 8px; }
        QCheckBox { background: transparent; }
        QCheckBox::indicator { width: 15px; height: 15px;
            border: 1px solid @border; border-radius: 3px; background: @surface; }
        QCheckBox::indicator:checked { background: @accent; border-color: @accent; }
        QCheckBox::indicator:disabled { border-color: @muted; }
        QRadioButton::indicator { width: 15px; height: 15px;
            border: 1px solid @border; border-radius: 8px; background: @surface; }
        QRadioButton::indicator:checked { background: @accent;
            border-color: @accent; }
        QTableWidget { background: @surface; alternate-background-color: @surface2;
            gridline-color: @border; border: 1px solid @border; }
        QHeaderView::section { background: @surface2; color: @muted;
            border: none; border-bottom: 1px solid @border; padding: 6px; }
        QProgressBar { background: @surface2; border: 1px solid @border;
            border-radius: 4px; text-align: center; color: @text; }
        QProgressBar::chunk { background: @accent; border-radius: 3px; }
        #sidebar { background: @surface; border-right: 1px solid @border; }
        #sidebar QPushButton { background: transparent; color: @text;
            text-align: left; padding: 9px 12px; border: none; border-radius: 6px; }
        #sidebar QPushButton:hover { background: @surface2; }
        #sidebar QPushButton:checked { background: @accent; color: @accentInk; }
        #sidebarBrand { color: @text; font-weight: bold; font-size: 15px;
            padding: 4px 4px 8px; }
        #sidebarUser { color: @muted; padding: 6px 4px; }
        #sidebarQuit { background: @surface2; color: @text; font-weight: bold;
            padding: 10px 12px; border: 1px solid @border; border-radius: 6px;
            margin-top: 6px; }
        #sidebarQuit:hover { background: @accent; color: @accentInk; }
        #navBadge { background: #c62828; color: white; border-radius: 8px;
            padding: 0 5px; font-weight: bold; }
        #kpiTile { background: @surface; border: 1px solid @border;
            border-radius: 8px; }
        #statusStrip { background: @surface; border-top: 1px solid @border; }
        #logPanel { background: @surface; border-top: 1px solid @border; }
        #logHeader { color: @muted; font-weight: bold; padding: 2px 4px; }
        #logView { background: @surface2; color: @text; border: 1px solid @border;
            border-radius: 4px; font-family: monospace; }
    )QSS");

    qss.replace("@surface2", palette.surface2)
        .replace("@surface", palette.surface)
        .replace("@accentInk", palette.accentInk)
        .replace("@accent", palette.accent)
        .replace("@border", palette.border)
        .replace("@muted", palette.muted)
        .replace("@text", palette.text)
        .replace("@bg", palette.bg);
    return qss;
}

void QtPaletteManager::apply(const QString& id) {
    for (const auto& palette : palettes_) {
        if (palette.id == id) {
            qApp->setStyleSheet(buildStyleSheet(palette));
            currentId_ = id;
            static_cast<void>(config_.setPalette(id.toStdString()));
            return;
        }
    }
}

void QtPaletteManager::applyInitial() {
    const QString stored = QString::fromStdString(config_.getPalette());
    for (const auto& palette : palettes_) {
        if (palette.id == stored) {
            apply(stored);
            return;
        }
    }
    apply(palettes_.front().id);
}

}  // namespace app::view
