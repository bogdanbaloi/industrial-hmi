#include "src/config/ConfigManager.h"

#include "src/qt/view/QtSettingsPage.h"

#include "src/qt/view/QtPaletteManager.h"

#include "ui_QtSettingsPage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QString>

namespace app::view {

namespace {

QString onOff(bool enabled) {
    return enabled ? QObject::tr("on") : QObject::tr("off");
}

}  // namespace

QtSettingsPage::QtSettingsPage(const config::ConfigManager& config,
                              QtPaletteManager& paletteManager, QWidget* parent)
    : QWidget(parent),
      paletteManager_(paletteManager),
      ui_(std::make_unique<Ui::QtSettingsPage>()) {
    ui_->setupUi(this);

    // Palette picker: populate from the manager, select the active one, then
    // wire changes. Connecting after the initial fill avoids re-applying on
    // setup.
    for (const auto& palette : paletteManager_.palettes()) {
        ui_->paletteCombo->addItem(palette.displayName, palette.id);
    }
    const int active = ui_->paletteCombo->findData(paletteManager_.currentId());
    if (active >= 0) {
        ui_->paletteCombo->setCurrentIndex(active);
    }
    connect(ui_->paletteCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                paletteManager_.apply(ui_->paletteCombo->itemData(index).toString());
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
