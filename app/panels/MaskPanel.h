#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <QCoreApplication>
#include <QWidget>

#include "document/ProjectDocument.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

// Inspector page for the comparison mask of a frame. It only displays and reports what the
// user asked for; the owner applies the edits and calls the refresh functions.
class MaskPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(MaskPanel)

public:
    explicit MaskPanel(QWidget* parent = nullptr);

    // Mask ID and shape mode of the frame.
    void refreshFrame(const document::ProjectDocument* document,
                      std::optional<std::size_t> frameIndex);
    // Selected-mask label and the Assign/Edit buttons. selectedMaskIndex is the mask picked in
    // the mask browser.
    void refreshSelection(const document::ProjectDocument* document,
                          std::optional<std::size_t> frameIndex,
                          std::optional<std::size_t> selectedMaskIndex,
                          bool hasSelectedFrames);
    // Whether the mask is being edited: Edit Mask shows it and, while it does, stays enabled
    // on a frame without a mask, so that the mode can be left there. Does not notify.
    void setEditChecked(bool checked);
    // Clamped to the offered IDs; does not notify.
    void setMaskId(int maskId);

    // -1 when no mask is assigned.
    [[nodiscard]] int maskId() const;

    // -1 clears the assignment.
    std::function<void(int maskId)> onMaskIdEdited;
    std::function<void(int mode)> onShapeModeEdited;
    std::function<void()> onAssignRequested;
    // Edit Mask was clicked; the owner enters or leaves the mode and calls setEditChecked().
    std::function<void()> onEditRequested;

private:
    QLabel* m_selectedMaskLabel = nullptr;
    QSpinBox* m_maskIdSpinBox = nullptr;
    QComboBox* m_shapeModeComboBox = nullptr;
    QToolButton* m_assignButton = nullptr;
    QToolButton* m_editButton = nullptr;
};

}  // namespace colorizing::app
