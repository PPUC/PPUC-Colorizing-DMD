#pragma once

#include <QCoreApplication>
#include <QDialog>

#include <cstddef>
#include <map>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/FrameSettingsCopy.h"

class QCheckBox;
class QLabel;

namespace colorizing::app {

// Edit -> Copy Frame Settings: which settings of the source frame replace those of the target
// frames. Each choice says how many targets it changes (edit::framesChangedBy()); the choices
// are kept for the next time.
class FrameSettingsDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(FrameSettingsDialog)

public:
    FrameSettingsDialog(const document::ProjectDocument& document,
                        std::size_t source,
                        std::vector<std::size_t> targets,
                        QWidget* parent = nullptr);

    [[nodiscard]] std::vector<edit::FrameSettingsPart> parts() const;
    // The check box of a part, for tests.
    [[nodiscard]] QCheckBox* checkBox(edit::FrameSettingsPart part) const;

    // Stores the choices for the next time.
    void accept() override;

private:
    void updateNote();

    std::map<edit::FrameSettingsPart, QCheckBox*> m_checkBoxes;
    QLabel* m_note = nullptr;
    std::size_t m_sceneFrames = 0;
};

// The part's name in the dialog.
[[nodiscard]] QString frameSettingsPartName(edit::FrameSettingsPart part);

}  // namespace colorizing::app
