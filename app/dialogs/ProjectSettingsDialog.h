#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <cstdint>

#include "document/LibserumTarget.h"
#include "document/ProjectLicense.h"
#include "document/SpeakerLights.h"

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

class ProjectSettingsPanel;

// File -> Project Settings: the settings of the whole colorization (ProjectSettingsPanel),
// shown with the project's values. The owner applies the chosen ones when it is accepted; OK
// is disabled while the project does not fit the libserum target chosen.
class ProjectSettingsDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(ProjectSettingsDialog)

public:
    explicit ProjectSettingsDialog(const document::ProjectDocument& document,
                                   QWidget* parent = nullptr);

    [[nodiscard]] uint8_t scalingAlgorithm() const;
    [[nodiscard]] uint8_t shadowOffset() const;
    [[nodiscard]] document::LibserumTarget libserumTarget() const;
    [[nodiscard]] document::LightActions lightDefaults() const;
    [[nodiscard]] document::ProjectLicense license() const;
    [[nodiscard]] ProjectSettingsPanel* panel() const;

private:
    ProjectSettingsPanel* m_panel = nullptr;
};

}  // namespace colorizing::app
