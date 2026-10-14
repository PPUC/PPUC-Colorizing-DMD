#pragma once

#include <QWidget>

#include "DmdOutput.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace colorizing::app {

// The DMD Output page of the Settings dialog: which displays libdmdutil drives and which
// plane they get (see DmdOutputSettings).
class DmdOutputSettingsPage final : public QWidget {
public:
    explicit DmdOutputSettingsPage(const DmdOutputSettings& settings, QWidget* parent = nullptr);

    [[nodiscard]] DmdOutputSettings settings() const;

private:
    void updateEnabled();

    QRadioButton* m_localButton = nullptr;
    QRadioButton* m_serverButton = nullptr;
    QWidget* m_localGroup = nullptr;
    QWidget* m_serverGroup = nullptr;
    QCheckBox* m_zedmdCheckBox = nullptr;
    QLineEdit* m_zedmdDeviceEdit = nullptr;
    QCheckBox* m_zedmdWiFiCheckBox = nullptr;
    QLineEdit* m_zedmdWiFiAddressEdit = nullptr;
    QSpinBox* m_zedmdBrightnessSpinBox = nullptr;
    QCheckBox* m_pixelcadeCheckBox = nullptr;
    QLineEdit* m_pixelcadeDeviceEdit = nullptr;
    QCheckBox* m_pin2dmdCheckBox = nullptr;
    QLineEdit* m_serverAddressEdit = nullptr;
    QSpinBox* m_serverPortSpinBox = nullptr;
    QComboBox* m_pictureComboBox = nullptr;
};

}  // namespace colorizing::app
