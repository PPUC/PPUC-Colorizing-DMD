#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <functional>
#include <string>

#include "document/Scene.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QSpinBox;

namespace colorizing::app {

// A scene's settings: what one line of a "<rom>.pup.csv" says. problem tells why the scene
// cannot be taken as it is (empty if it can); OK stays disabled while it objects.
class SceneDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(SceneDialog)

public:
    SceneDialog(const document::SceneDefinition& scene,
                std::function<std::string(const document::SceneDefinition&)> problem,
                QWidget* parent = nullptr);

    [[nodiscard]] document::SceneDefinition scene() const;

private:
    void updateState();

    document::SceneDefinition m_scene;
    std::function<std::string(const document::SceneDefinition&)> m_problem;
    QSpinBox* m_idSpinBox = nullptr;
    QSpinBox* m_framesSpinBox = nullptr;
    QSpinBox* m_groupsSpinBox = nullptr;
    QSpinBox* m_durationSpinBox = nullptr;
    QComboBox* m_repeatComboBox = nullptr;
    QSpinBox* m_timesSpinBox = nullptr;
    QCheckBox* m_randomCheckBox = nullptr;
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
    QDialogButtonBox* m_buttons = nullptr;
};

}  // namespace colorizing::app
