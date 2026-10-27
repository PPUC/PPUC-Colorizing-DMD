#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "document/Scene.h"

class QLabel;
class QToolButton;

namespace colorizing::app {

class SceneSettingsForm;

// The Scene section of the left sidebar: the settings of the scene the frame on the canvas
// belongs to, to change them later (a new scene is made in the scene window). Changes wait for
// Apply, since fewer frames delete frames; Revert shows the scene's settings again.
class SceneSettingsPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SceneSettingsPanel)

public:
    explicit SceneSettingsPanel(QWidget* parent = nullptr);

    // The scene shown, as the project has it; unapplied changes are dropped. none: no scene.
    // projectScenes: those a variant can play the frames of.
    void setScene(const std::optional<document::SceneDefinition>& scene,
                  std::function<std::string(const document::SceneDefinition&)> problem,
                  const std::vector<document::SceneDefinition>& projectScenes = {});
    // The scene the section is about (its ID before any change); none without one.
    [[nodiscard]] std::optional<uint16_t> sceneId() const;
    [[nodiscard]] bool hasChanges() const { return m_changed; }
    [[nodiscard]] SceneSettingsForm* form() const { return m_form; }

    // Apply: the scene's settings as the fields say.
    std::function<void(uint16_t sceneId, const document::SceneDefinition& scene)> onApplyRequested;

private:
    void updateState();

    std::optional<document::SceneDefinition> m_scene;
    std::function<std::string(const document::SceneDefinition&)> m_problem;
    std::vector<document::SceneDefinition> m_projectScenes;
    bool m_changed = false;
    QLabel* m_titleLabel = nullptr;
    SceneSettingsForm* m_form = nullptr;
    QToolButton* m_applyButton = nullptr;
    QToolButton* m_revertButton = nullptr;
};

}  // namespace colorizing::app
