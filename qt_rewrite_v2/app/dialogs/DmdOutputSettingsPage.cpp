#include "dialogs/DmdOutputSettingsPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace colorizing::app {

DmdOutputSettingsPage::DmdOutputSettingsPage(const DmdOutputSettings& settings, QWidget* parent)
    : QWidget(parent)
{
    m_localButton = new QRadioButton(tr("Displays connected to this computer"), this);
    m_serverButton = new QRadioButton(tr("Use DMDServer"), this);
    m_serverButton->setToolTip(
        tr("libdmdutil's server process, on this or another computer, drives the displays"));

    auto* local = new QGroupBox(this);
    m_localGroup = local;
    m_zedmdCheckBox = new QCheckBox(QStringLiteral("ZeDMD"), local);
    m_zedmdDeviceEdit = new QLineEdit(local);
    m_zedmdDeviceEdit->setPlaceholderText(tr("Any serial port"));
    m_zedmdDeviceEdit->setToolTip(
        tr("The serial port of the ZeDMD, e.g. COM3 or /dev/ttyUSB0; empty: look on all"));
    m_zedmdWiFiCheckBox = new QCheckBox(tr("ZeDMD over Wi-Fi"), local);
    m_zedmdWiFiAddressEdit = new QLineEdit(local);
    m_zedmdWiFiAddressEdit->setPlaceholderText(QStringLiteral("zedmd-wifi.local"));
    m_zedmdBrightnessSpinBox = new QSpinBox(local);
    m_zedmdBrightnessSpinBox->setRange(-1, 15);
    m_zedmdBrightnessSpinBox->setSpecialValueText(tr("Display's own"));
    m_zedmdBrightnessSpinBox->setToolTip(tr("ZeDMD brightness, 0 to 15"));
    m_pixelcadeCheckBox = new QCheckBox(QStringLiteral("Pixelcade"), local);
    m_pixelcadeDeviceEdit = new QLineEdit(local);
    m_pixelcadeDeviceEdit->setPlaceholderText(tr("Any serial port"));
    m_pin2dmdCheckBox = new QCheckBox(QStringLiteral("PIN2DMD"), local);
    // A grid, not a form: a form right-aligns its first column on macOS, and a row without a
    // field there starts at the left edge instead.
    auto* localGrid = new QGridLayout(local);
    localGrid->addWidget(m_zedmdCheckBox, 0, 0);
    localGrid->addWidget(m_zedmdDeviceEdit, 0, 1);
    localGrid->addWidget(m_zedmdWiFiCheckBox, 1, 0);
    localGrid->addWidget(m_zedmdWiFiAddressEdit, 1, 1);
    localGrid->addWidget(new QLabel(tr("ZeDMD brightness"), local), 2, 0);
    localGrid->addWidget(m_zedmdBrightnessSpinBox, 2, 1);
    localGrid->addWidget(m_pixelcadeCheckBox, 3, 0);
    localGrid->addWidget(m_pixelcadeDeviceEdit, 3, 1);
    localGrid->addWidget(m_pin2dmdCheckBox, 4, 0);
    localGrid->setColumnStretch(1, 1);

    auto* server = new QGroupBox(this);
    m_serverGroup = server;
    m_serverAddressEdit = new QLineEdit(server);
    m_serverPortSpinBox = new QSpinBox(server);
    m_serverPortSpinBox->setRange(1, 65535);
    auto* serverForm = new QFormLayout(server);
    serverForm->addRow(tr("Address"), m_serverAddressEdit);
    serverForm->addRow(tr("Port"), m_serverPortSpinBox);

    m_pictureComboBox = new QComboBox(this);
    m_pictureComboBox->addItem(tr("The plane the canvas shows"),
                               static_cast<int>(DmdOutputSettings::Picture::Canvas));
    m_pictureComboBox->addItem(tr("Native plane (128x32)"),
                               static_cast<int>(DmdOutputSettings::Picture::Native));
    m_pictureComboBox->addItem(tr("Extra plane (256x64)"),
                               static_cast<int>(DmdOutputSettings::Picture::Extra));
    m_pictureComboBox->setToolTip(
        tr("For projects with both planes; a project with one plane sends that one. "
           "A frame without extra data sends its native plane."));
    auto* pictureForm = new QFormLayout;
    pictureForm->addRow(tr("Picture"), m_pictureComboBox);

    auto* note = new QLabel(
        tr("The display shows libserum's picture in the Runtime view and in the "
           "Frame Tester, otherwise the frame as the editor composes it. libdmdutil "
           "scales it to the display."),
        this);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_localButton);
    layout->addWidget(local);
    layout->addWidget(m_serverButton);
    layout->addWidget(server);
    layout->addLayout(pictureForm);
    layout->addWidget(note);
    layout->addStretch(1);

    const bool toServer = settings.target == DmdOutputSettings::Target::DmdServer;
    m_localButton->setChecked(!toServer);
    m_serverButton->setChecked(toServer);
    m_zedmdCheckBox->setChecked(settings.zedmd);
    m_zedmdDeviceEdit->setText(settings.zedmdDevice);
    m_zedmdWiFiCheckBox->setChecked(settings.zedmdWiFi);
    m_zedmdWiFiAddressEdit->setText(settings.zedmdWiFiAddress);
    m_zedmdBrightnessSpinBox->setValue(settings.zedmdBrightness);
    m_pixelcadeCheckBox->setChecked(settings.pixelcade);
    m_pixelcadeDeviceEdit->setText(settings.pixelcadeDevice);
    m_pin2dmdCheckBox->setChecked(settings.pin2dmd);
    m_serverAddressEdit->setText(settings.serverAddress);
    m_serverPortSpinBox->setValue(settings.serverPort);
    m_pictureComboBox->setCurrentIndex(
        m_pictureComboBox->findData(static_cast<int>(settings.picture)));
    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{
             m_localButton, m_serverButton, m_zedmdCheckBox, m_zedmdWiFiCheckBox,
             m_pixelcadeCheckBox}) {
        connect(button, &QAbstractButton::toggled, this, [this]() { updateEnabled(); });
    }
    updateEnabled();
}

void DmdOutputSettingsPage::updateEnabled()
{
    const bool local = m_localButton->isChecked();
    m_localGroup->setEnabled(local);
    m_serverGroup->setEnabled(!local);
    m_zedmdDeviceEdit->setEnabled(m_zedmdCheckBox->isChecked());
    m_zedmdWiFiAddressEdit->setEnabled(m_zedmdWiFiCheckBox->isChecked());
    m_zedmdBrightnessSpinBox->setEnabled(m_zedmdCheckBox->isChecked() ||
                                         m_zedmdWiFiCheckBox->isChecked());
    m_pixelcadeDeviceEdit->setEnabled(m_pixelcadeCheckBox->isChecked());
}

DmdOutputSettings DmdOutputSettingsPage::settings() const
{
    DmdOutputSettings result;
    result.target = m_serverButton->isChecked() ? DmdOutputSettings::Target::DmdServer
                                                : DmdOutputSettings::Target::LocalDisplays;
    result.zedmd = m_zedmdCheckBox->isChecked();
    result.zedmdDevice = m_zedmdDeviceEdit->text().trimmed();
    result.zedmdWiFi = m_zedmdWiFiCheckBox->isChecked();
    result.zedmdWiFiAddress = m_zedmdWiFiAddressEdit->text().trimmed();
    result.zedmdBrightness = m_zedmdBrightnessSpinBox->value();
    result.pixelcade = m_pixelcadeCheckBox->isChecked();
    result.pixelcadeDevice = m_pixelcadeDeviceEdit->text().trimmed();
    result.pin2dmd = m_pin2dmdCheckBox->isChecked();
    result.serverAddress = m_serverAddressEdit->text().trimmed();
    result.serverPort = m_serverPortSpinBox->value();
    result.picture =
        static_cast<DmdOutputSettings::Picture>(m_pictureComboBox->currentData().toInt());
    return result;
}

}  // namespace colorizing::app
