#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <functional>
#include <string>
#include <vector>

#include "document/Scene.h"

class QDialogButtonBox;

namespace colorizing::app {

class SceneSettingsForm;

// A scene's settings in a window (SceneSettingsForm): for a new scene. problem tells why the
// scene cannot be taken as it is (empty if it can); OK stays disabled while it objects.
class SceneDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(SceneDialog)

public:
    // projectScenes: those a new scene can play the frames of (SceneSettingsForm::setScene()).
    SceneDialog(const document::SceneDefinition& scene,
                std::function<std::string(const document::SceneDefinition&)> problem,
                const std::vector<document::SceneDefinition>& projectScenes = {},
                QWidget* parent = nullptr);

    [[nodiscard]] document::SceneDefinition scene() const;

private:
    void updateState();

    SceneSettingsForm* m_form = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

}  // namespace colorizing::app
