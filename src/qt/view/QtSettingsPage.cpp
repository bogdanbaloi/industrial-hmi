#include "src/config/ConfigManager.h"

#include "src/qt/view/QtSettingsPage.h"

#include "ui_QtSettingsPage.h"

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
                              QWidget* parent)
    : QWidget(parent), ui_(std::make_unique<Ui::QtSettingsPage>()) {
    ui_->setupUi(this);
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
    addRow(tr("Theme"), QString::fromStdString(config.getDefaultTheme()));
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
