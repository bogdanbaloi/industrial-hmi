#include "src/config/ConfigManager.h"

#include "src/qt/view/QtSettingsPage.h"

#include "src/qt/view/QtPaletteManager.h"

#include "ui_QtSettingsPage.h"

#include <QButtonGroup>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QString>
#include <QToolButton>

#include <array>
#include <cstddef>
#include <utility>

namespace app::view {

namespace {

QString onOff(bool enabled) {
    return enabled ? QObject::tr("on") : QObject::tr("off");
}

// Selectable interface languages: the "auto" sentinel plus every LINGUAS code
// with a real .po catalog. Names are endonyms (shown in their own language, the
// usual convention for a language picker) so they read the same in any UI
// locale; only the "auto" entry is translated (built with tr() in the combo).
struct LangOption {
    const char* code;
    const char* endonym;
};
constexpr std::array<LangOption, 11> kLanguages{{
    {.code = "en", .endonym = "English"},
    {.code = "de", .endonym = "Deutsch"},
    {.code = "es", .endonym = "Espanol"},
    {.code = "es_MX", .endonym = "Espanol (Mexico)"},
    {.code = "fi", .endonym = "Suomi"},
    {.code = "fr", .endonym = "Francais"},
    {.code = "ga", .endonym = "Gaeilge"},
    {.code = "it", .endonym = "Italiano"},
    {.code = "pt", .endonym = "Portugues"},
    {.code = "pt_BR", .endonym = "Portugues (Brasil)"},
    {.code = "sv", .endonym = "Svenska"},
}};

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
                              LanguageChangeCallback onLanguageChanged,
                              QWidget* parent)
    : QWidget(parent),
      config_(config),
      paletteManager_(paletteManager),
      onDisplayModeChanged_(std::move(onDisplayModeChanged)),
      onLanguageChanged_(std::move(onLanguageChanged)),
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

    buildLanguageCombo();
    populate();
}

QtSettingsPage::~QtSettingsPage() = default;

void QtSettingsPage::buildLanguageCombo() {
    auto* combo = ui_->languageCombo;
    {
        // Fill + preselect without firing the change callback.
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItem(tr("System default"), QStringLiteral("auto"));
        for (const auto& lang : kLanguages) {
            combo->addItem(QString::fromUtf8(lang.endonym),
                           QString::fromUtf8(lang.code));
        }
        const QString current = QString::fromStdString(config_.getLanguage());
        const int idx = combo->findData(current);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo](int) {
        if (onLanguageChanged_) {
            onLanguageChanged_(combo->currentData().toString().toStdString());
        }
    });
}

void QtSettingsPage::populate() {
    const auto& config = config_;
    auto* form = ui_->settingsForm;
    // Clear rows from a previous build so a language switch can repopulate.
    while (form->rowCount() > 0) {
        form->removeRow(0);
    }
    const auto addRow = [form](const QString& label, const QString& value) {
        form->addRow(label, new QLabel(value));
    };

    addRow(tr("Application"),
           QString::fromStdString(config.getAppName()) + " "
               + QString::fromStdString(config.getAppVersion()));

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

void QtSettingsPage::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        // Refresh the .ui labels (group titles, radios) then rebuild the
        // dynamically-added config rows so their tr() labels + values re-read
        // the new catalog. The language combo's endonyms stay as they are.
        ui_->retranslateUi(this);
        populate();
    }
    QWidget::changeEvent(event);
}

}  // namespace app::view
