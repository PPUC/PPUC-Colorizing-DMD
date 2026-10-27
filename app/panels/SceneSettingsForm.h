#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <functional>
#include <string>

#include "document/Scene.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace colorizing::app {

// The fields of a scene's settings: what one line of a "<rom>.pup.csv" says. Used by the scene
// window (a new scene) and the Scene section of the left sidebar (the scene of the frame shown).
// problem tells why the scene cannot be taken as it is (empty if it can); the form shows it.
class SceneSettingsForm final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SceneSettingsForm)

public:
    explicit SceneSettingsForm(QWidget* parent = nullptr);

    // Fills the fields; reports no change.
    void setScene(const document::SceneDefinition& scene,
                  std::function<std::string(const document::SceneDefinition&)> problem);
    [[nodiscard]] document::SceneDefinition scene() const;
    // Why the fields cannot be taken; empty when they can.
    [[nodiscard]] std::string problem() const;

    // A field changed.
    std::function<void()> onChanged;

private:
    void updateState();

    document::SceneDefinition m_scene;
    std::function<std::string(const document::SceneDefinition&)> m_problem;
    bool m_filling = false;
    QSpinBox* m_idSpinBox = nullptr;
    QSpinBox* m_framesSpinBox = nullptr;
    QSpinBox* m_groupsSpinBox = nullptr;
    QSpinBox* m_durationSpinBox = nullptr;
    QComboBox* m_repeatComboBox = nullptr;
    QSpinBox* m_timesSpinBox = nullptr;
    QCheckBox* m_randomCheckBox = nullptr;
    QCheckBox* m_backAndForthCheckBox = nullptr;
    QCheckBox* m_interruptableCheckBox = nullptr;
    QCheckBox* m_immediateCheckBox = nullptr;
    QComboBox* m_finishComboBox = nullptr;
    QSpinBox* m_autoStartSpinBox = nullptr;
    QLabel* m_autoStartLabel = nullptr;
    QCheckBox* m_backgroundCheckBox = nullptr;
    QCheckBox* m_onlyDynamicCheckBox = nullptr;
    QCheckBox* m_resumeCheckBox = nullptr;
    QCheckBox* m_replaceBlackCheckBox = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_problemLabel = nullptr;
};

}  // namespace colorizing::app
