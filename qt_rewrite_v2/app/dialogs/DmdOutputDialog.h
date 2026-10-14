#pragma once

#include <QDialog>

#include "DmdOutput.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace colorizing::app {

// View -> DMD Output Settings: which displays libdmdutil drives (see DmdOutputSettings).
class DmdOutputDialog final : public QDialog {
public:
    explicit DmdOutputDialog(const DmdOutputSettings& settings, QWidget* parent = nullptr);

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
