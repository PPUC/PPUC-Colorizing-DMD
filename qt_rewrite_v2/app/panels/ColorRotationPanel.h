#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include <QCoreApplication>
#include <QWidget>

#include "document/ProjectDocument.h"

class QComboBox;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

class ColorGridWidget;

// Inspector page for the color rotation slots of a frame. It only displays and reports what the
// user asked for; the owner applies the edits and calls refresh().
class ColorRotationPanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ColorRotationPanel)

public:
    explicit ColorRotationPanel(QWidget* parent = nullptr);

    void refresh(const document::ProjectDocument* document,
                 std::optional<std::size_t> frameIndex,
                 render::Resolution resolution);
    [[nodiscard]] std::size_t selectedSlotIndex() const;

    std::function<void()> onSlotSelectionChanged;
    std::function<void(std::size_t slotIndex, int length)> onLengthEdited;
    std::function<void(std::size_t slotIndex, int delayMs)> onDelayEdited;
    std::function<void(std::size_t slotIndex, std::size_t colorIndex)> onColorClicked;
    std::function<void(std::size_t slotIndex)> onClearSlotRequested;

private:
    QComboBox* m_slotComboBox = nullptr;
    QSpinBox* m_lengthSpinBox = nullptr;
    QSpinBox* m_delaySpinBox = nullptr;
    ColorGridWidget* m_grid = nullptr;
    QToolButton* m_clearSlotButton = nullptr;
};

}  // namespace colorizing::app
