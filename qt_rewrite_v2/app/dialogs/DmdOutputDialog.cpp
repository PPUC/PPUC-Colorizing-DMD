#include "dialogs/DmdOutputDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace colorizing::app {

DmdOutputDialog::DmdOutputDialog(const DmdOutputSettings& settings, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("DMD Output Settings"));
    m_localButton = new QRadioButton(QStringLiteral("Displays connected to this computer"), this);
    m_serverButton = new QRadioButton(QStringLiteral("A dmdserver"), this);
    m_serverButton->setToolTip(QStringLiteral(
        "libdmdutil's server process, on this or another computer, drives the displays"));

    auto* local = new QGroupBox(this);
    m_localGroup = local;
    m_zedmdCheckBox = new QCheckBox(QStringLiteral("ZeDMD"), local);
    m_zedmdDeviceEdit = new QLineEdit(local);
    m_zedmdDeviceEdit->setPlaceholderText(QStringLiteral("Any serial port"));
    m_zedmdDeviceEdit->setToolTip(
        QStringLiteral("The serial port of the ZeDMD, e.g. COM3 or /dev/ttyUSB0; empty: look on "
                       "all"));
    m_zedmdWiFiCheckBox = new QCheckBox(QStringLiteral("ZeDMD over Wi-Fi"), local);
    m_zedmdWiFiAddressEdit = new QLineEdit(local);
    m_zedmdWiFiAddressEdit->setPlaceholderText(QStringLiteral("zedmd-wifi.local"));
    m_zedmdBrightnessSpinBox = new QSpinBox(local);
    m_zedmdBrightnessSpinBox->setRange(-1, 15);
    m_zedmdBrightnessSpinBox->setSpecialValueText(QStringLiteral("Display's own"));
    m_zedmdBrightnessSpinBox->setToolTip(QStringLiteral("ZeDMD brightness, 0 to 15"));
    m_pixelcadeCheckBox = new QCheckBox(QStringLiteral("Pixelcade"), local);
    m_pixelcadeDeviceEdit = new QLineEdit(local);
    m_pixelcadeDeviceEdit->setPlaceholderText(QStringLiteral("Any serial port"));
    m_pin2dmdCheckBox = new QCheckBox(QStringLiteral("PIN2DMD"), local);
    auto* localForm = new QFormLayout(local);
    localForm->addRow(m_zedmdCheckBox, m_zedmdDeviceEdit);
    localForm->addRow(m_zedmdWiFiCheckBox, m_zedmdWiFiAddressEdit);
    localForm->addRow(QStringLiteral("ZeDMD brightness"), m_zedmdBrightnessSpinBox);
    localForm->addRow(m_pixelcadeCheckBox, m_pixelcadeDeviceEdit);
    localForm->addRow(m_pin2dmdCheckBox);

    auto* server = new QGroupBox(this);
    m_serverGroup = server;
    m_serverAddressEdit = new QLineEdit(server);
    m_serverPortSpinBox = new QSpinBox(server);
    m_serverPortSpinBox->setRange(1, 65535);
    auto* serverForm = new QFormLayout(server);
    serverForm->addRow(QStringLiteral("Address"), m_serverAddressEdit);
    serverForm->addRow(QStringLiteral("Port"), m_serverPortSpinBox);

    m_pictureComboBox = new QComboBox(this);
    m_pictureComboBox->addItem(QStringLiteral("The plane the canvas shows"),
                               static_cast<int>(DmdOutputSettings::Picture::Canvas));
    m_pictureComboBox->addItem(QStringLiteral("Native plane (128x32)"),
                               static_cast<int>(DmdOutputSettings::Picture::Native));
    m_pictureComboBox->addItem(QStringLiteral("Extra plane (256x64)"),
                               static_cast<int>(DmdOutputSettings::Picture::Extra));
    m_pictureComboBox->setToolTip(
        QStringLiteral("For projects with both planes; a project with one plane sends that one. "
                       "A frame without extra data sends its native plane."));
    auto* pictureForm = new QFormLayout;
    pictureForm->addRow(QStringLiteral("Picture"), m_pictureComboBox);

    auto* note = new QLabel(
        QStringLiteral("The display shows libserum's picture in the Runtime view and in the "
                       "Frame Tester, otherwise the frame as the editor composes it. libdmdutil "
                       "scales it to the display."),
        this);
    note->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_localButton);
    layout->addWidget(local);
    layout->addWidget(m_serverButton);
    layout->addWidget(server);
    layout->addLayout(pictureForm);
    layout->addWidget(note);
    layout->addWidget(buttons);

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

void DmdOutputDialog::updateEnabled()
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

DmdOutputSettings DmdOutputDialog::settings() const
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
