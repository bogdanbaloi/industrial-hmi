#include "src/config/ConfigManager.h"

#include "src/qt/view/QtSettingsPage.h"

#include "src/qt/view/QtPaletteManager.h"

#include "ui_QtSettingsPage.h"

#include <QButtonGroup>
#include <QColor>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QRadioButton>
#include <QString>
#include <QToolButton>

#include <cstddef>
#include <utility>

namespace app::view {

namespace {

QString onOff(bool enabled) {
    return enabled ? QObject::tr("on") : QObject::tr("off");
}

// A palette thumbnail: a two-tone chip (base + accent) painted from the
// palette's role colours, with the name underneath -- the Qt analog of the GTK
// settings palette thumbnails.
QToolButton* makeSwatch(const PaletteDef& palette) {
    constexpr int kIconW = 46;
    constexpr int kIconH = 28;

    QPixmap pixmap(kIconW, kIconH);
    pixmap.fill(QColor(palette.bg));
    QPainter painter(&pixmap);
    painter.fillRect(kIconW / 2, 0, kIconW / 2, kIconH, QColor(palette.accent));
    painter.setPen(QColor(palette.border));
    painter.drawRect(0, 0, kIconW - 1, kIconH - 1);
    painter.end();

    auto* button = new QToolButton();
    button->setText(palette.displayName);
    button->setIcon(QIcon(pixmap));
    button->setIconSize(pixmap.size());
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setStyleSheet(
        "QToolButton { border: 2px solid transparent; border-radius: 6px;"
        " padding: 4px; }"
        "QToolButton:checked { border: 2px solid #2563eb; }");
    return button;
}

}  // namespace

QtSettingsPage::QtSettingsPage(const config::ConfigManager& config,
                              QtPaletteManager& paletteManager,
                              DisplayModeCallback onDisplayModeChanged,
                              QWidget* parent)
    : QWidget(parent),
      paletteManager_(paletteManager),
      onDisplayModeChanged_(std::move(onDisplayModeChanged)),
      ui_(std::make_unique<Ui::QtSettingsPage>()) {
    ui_->setupUi(this);

    // Palette picker: one thumbnail swatch per palette, exclusive selection.
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const auto& palettes = paletteManager_.palettes();
    for (std::size_t i = 0; i < palettes.size(); ++i) {
        auto* swatch = makeSwatch(palettes[i]);
        if (palettes[i].id == paletteManager_.currentId()) {
            swatch->setChecked(true);
        }
        group->addButton(swatch, static_cast<int>(i));
        ui_->paletteSwatches->addWidget(swatch);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int index) {
        paletteManager_.apply(
            paletteManager_.palettes()[static_cast<std::size_t>(index)].id);
    });

    // Display mode: report changes through the injected callback so the page
    // never touches the window. Fullscreen is the default (kiosk); set before
    // connecting so this initial state does not fire the callback.
    ui_->fullscreenRadio->setChecked(true);
    connect(ui_->fullscreenRadio, &QRadioButton::toggled, this,
            [this](bool fullscreen) {
                if (onDisplayModeChanged_) {
                    onDisplayModeChanged_(fullscreen);
                }
            });

    populate(config);
}

QtSettingsPage::~QtSettingsPage() = default;

void QtSettingsPage::populate(const config::ConfigManager& config) {
    auto* form = ui_->settingsForm;
    const auto addRow = [form](const QString& label, const QString& value) {
        form->addRow(label, new QLabel(value));
    };

    addRow(tr("Application"),
           QString::fromStdString(config.getAppName()) + " "
               + QString::fromStdString(config.getAppVersion()));
    addRow(tr("Language"), QString::fromStdString(config.getLanguage()));

    addRow(tr("TCP backend"),
           config.isTcpBackendEnabled()
               ? tr("on (port %1)").arg(config.getTcpBackendPort())
               : onOff(false));
    addRow(tr("MQTT backend"),
           config.isMqttBackendEnabled()
               ? tr("on (%1:%2)")
                     .arg(QString::fromStdString(config.getMqttBrokerHost()))
                     .arg(config.getMqttBrokerPort())
               : onOff(false));
    addRow(tr("OPC-UA backend"), onOff(config.isOpcUaBackendEnabled()));
    addRow(tr("Modbus backend"),
           config.isModbusBackendEnabled()
               ? tr("on (%1:%2)")
                     .arg(QString::fromStdString(config.getModbusHost()))
                     .arg(config.getModbusPort())
               : onOff(false));

    addRow(tr("Historian"), onOff(config.isHistorianEnabled()));
    addRow(tr("Multi-station"), onOff(config.isMultiStationEnabled()));
    addRow(tr("Auth"), onOff(config.isAuthEnabled()));
}

}  // namespace app::view
