#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <functional>

class QComboBox;

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

// The Project Settings section of the inspector: settings of the whole colorization that
// libserum takes from it (document::ColorizationSettings), for now its scaling settings. It
// shows them and reports a change; the owner changes the project (undoably) and calls
// setProject().
class ProjectSettingsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ProjectSettingsPanel)

public:
    explicit ProjectSettingsPanel(QWidget* parent = nullptr);

    // nullptr without a project.
    void setProject(const document::ProjectDocument* document);

    std::function<void(uint8_t scalingAlgorithm, uint8_t shadowOffset)> onScalingChanged;

private:
    QComboBox* m_scalingComboBox = nullptr;
    QComboBox* m_shadowComboBox = nullptr;
    bool m_filling = false;
};

}  // namespace colorizing::app
