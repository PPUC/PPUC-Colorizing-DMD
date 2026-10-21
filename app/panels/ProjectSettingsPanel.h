#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <array>
#include <functional>

#include "document/LibserumTarget.h"
#include "document/SpeakerLights.h"

class QComboBox;
class QLabel;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

class LightActionEditor;

// The settings of the whole colorization that libserum takes from it: the libserum the
// project is made for (document::LibserumTarget) and its scaling settings
// (document::ColorizationSettings), as ProjectSettingsDialog shows them. It shows them and
// reports a change; a target the project does not fit is named with the reasons.
class ProjectSettingsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ProjectSettingsPanel)

public:
    explicit ProjectSettingsPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);

    std::function<void(uint8_t scalingAlgorithm, uint8_t shadowOffset)> onScalingChanged;
    std::function<void(document::LibserumTarget)> onTargetChanged;
    // The values chosen.
    [[nodiscard]] uint8_t scalingAlgorithm() const;
    [[nodiscard]] uint8_t shadowOffset() const;
    [[nodiscard]] document::LibserumTarget libserumTarget() const;
    // Whether the project fits the target chosen.
    [[nodiscard]] bool targetFits() const;
    // Why the project does not fit the target; empty when it does.
    [[nodiscard]] QString targetProblemText() const;
    // The speaker lights' defaults as chosen (LightActionKind::Unchanged: off).
    [[nodiscard]] document::LightActions lightDefaults() const;
    [[nodiscard]] LightActionEditor* lightDefault(std::size_t channel) const
    {
        return m_lightDefaults[channel];
    }

private:
    void updateTargetProblems();

    const document::ProjectDocument* m_document = nullptr;
    QComboBox* m_targetComboBox = nullptr;
    QLabel* m_targetLabel = nullptr;
    QComboBox* m_scalingComboBox = nullptr;
    std::array<LightActionEditor*, document::kLightChannels> m_lightDefaults{};
    QComboBox* m_shadowComboBox = nullptr;
    bool m_filling = false;
};

}  // namespace colorizing::app
