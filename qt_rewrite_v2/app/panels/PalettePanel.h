#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include "document/ProjectDocument.h"
#include "edit/DrawingGeometry.h"
#include "edit/DrawingTools.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

class ColorGridWidget;

using PaletteGradientShape = edit::PaletteGradientShape;

// Inspector page for the palettes and the palette gradient settings. It only displays and
// reports what the user asked for; the owner applies the edits and calls refresh().
class PalettePanel final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PalettePanel)

public:
    explicit PalettePanel(QWidget* parent = nullptr);

    // Rebuilds the palette list and keeps the selected palette if it still exists.
    void setPalettes(const document::ProjectDocument* document);
    // Falls back to the first palette when none is selected.
    void ensurePaletteSelected(const document::ProjectDocument* document);
    void refresh(const document::ProjectDocument* document,
                 bool hasCanvasSelection,
                 bool selectionDirectionMode);
    void setSelectionDirectionChecked(bool checked);

    [[nodiscard]] std::optional<std::size_t> selectedPaletteIndex(
        const document::ProjectDocument* document) const;
    // Zero-based color indices; the start may be the larger one.
    [[nodiscard]] std::pair<int, int> gradientEndpoints() const;
    [[nodiscard]] PaletteGradientShape gradientShape() const;
    [[nodiscard]] edit::PaletteGradientDirection gradientDirection() const;
    [[nodiscard]] edit::PaletteGradientPattern gradientPattern() const;

    std::function<void()> onPaletteSelectionChanged;
    // Shape, pattern or direction.
    std::function<void()> onGradientSettingsChanged;
    std::function<void(const QString& name)> onNameEdited;
    std::function<void(std::size_t colorIndex)> onColorClicked;
    std::function<void(std::size_t colorIndex)> onColorDoubleClicked;
    std::function<void()> onApplyGradientRequested;
    std::function<void()> onFillSelectionRequested;
    std::function<void()> onSelectionDirectionClicked;
    std::function<void()> onClearSelectionRequested;

private:
    QLabel* m_selectedPaletteLabel = nullptr;
    QComboBox* m_paletteComboBox = nullptr;
    QLineEdit* m_nameLineEdit = nullptr;
    QSpinBox* m_gradientStartSpinBox = nullptr;
    QSpinBox* m_gradientEndSpinBox = nullptr;
    QComboBox* m_gradientShapeComboBox = nullptr;
    QComboBox* m_gradientPatternComboBox = nullptr;
    QComboBox* m_gradientDirectionComboBox = nullptr;
    ColorGridWidget* m_grid = nullptr;
    QToolButton* m_applyGradientButton = nullptr;
    QToolButton* m_fillSelectionButton = nullptr;
    QToolButton* m_selectionDirectionButton = nullptr;
    QToolButton* m_clearSelectionButton = nullptr;
};

}  // namespace colorizing::app
