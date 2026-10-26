#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <array>
#include <functional>

#include "document/ProjectLicense.h"
#include "document/SpeakerLights.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

class LightActionEditor;

// The settings of the whole colorization that libserum takes from it: its scaling settings
// (document::ColorizationSettings), the speaker lights' defaults and the license, as
// ProjectSettingsDialog shows them. It shows them and reports a change.
class ProjectSettingsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ProjectSettingsPanel)

public:
    explicit ProjectSettingsPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);

    std::function<void(uint8_t scalingAlgorithm, uint8_t shadowOffset)> onScalingChanged;
    // The values chosen.
    [[nodiscard]] uint8_t scalingAlgorithm() const;
    [[nodiscard]] uint8_t shadowOffset() const;
    // The speaker lights' defaults as chosen (LightActionKind::Unchanged: off).
    [[nodiscard]] document::LightActions lightDefaults() const;
    [[nodiscard]] LightActionEditor* lightDefault(std::size_t channel) const
    {
        return m_lightDefaults[channel];
    }
    // The license as chosen.
    [[nodiscard]] document::ProjectLicense license() const;
    [[nodiscard]] QComboBox* licenseComboBox() const { return m_licenseComboBox; }
    [[nodiscard]] QPlainTextEdit* licenseTextEdit() const { return m_licenseTextEdit; }
    [[nodiscard]] QLineEdit* attributionEdit() const { return m_attributionEdit; }
    [[nodiscard]] QLineEdit* licenseUrlEdit() const { return m_licenseUrlEdit; }
    [[nodiscard]] QLabel* licenseLabel() const { return m_licenseLabel; }

private:
    void updateLicenseLabel();

    const document::ProjectDocument* m_document = nullptr;
    QComboBox* m_scalingComboBox = nullptr;
    std::array<LightActionEditor*, document::kLightChannels> m_lightDefaults{};
    QComboBox* m_shadowComboBox = nullptr;
    QComboBox* m_licenseComboBox = nullptr;
    QLabel* m_licenseLabel = nullptr;
    QPlainTextEdit* m_licenseTextEdit = nullptr;
    QLineEdit* m_attributionEdit = nullptr;
    QLineEdit* m_licenseUrlEdit = nullptr;
    bool m_filling = false;
};

}  // namespace colorizing::app
