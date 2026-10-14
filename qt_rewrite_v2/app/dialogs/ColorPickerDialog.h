#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <QDialog>
#include <QString>

#include "edit/ColorModel.h"

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace colorizing::app {

class HsvSquareWidget;
class ValueBarWidget;

// The color picker of the Win32 editor (ColPickProc): hue/saturation square and value bar,
// RGB888 hex, R8/G8/B8, RGB565 as a decimal number, R5/G6/B5, H/S/V and the last 8 picked
// colors. Every field follows every other one. The state is an RGB888 color plus the HSV the
// square and bar show; the result is that color as RGB565.
//
// No drift: a field that is not being edited is only written, never read back, and every
// RGB565 color survives being shown and re-entered in any field (H, S and V have one decimal
// for that, see edit::roundedHsv()).
class ColorPickerDialog final : public QDialog {
public:
    static constexpr std::size_t kRecentColorCount = 8;

    ColorPickerDialog(uint16_t initialRgb565, const QString& title, QWidget* parent = nullptr);

    // Opens the dialog modally. The accepted color is remembered as the most recent one.
    [[nodiscard]] static std::optional<uint16_t> getColor(uint16_t initialRgb565,
                                                          QWidget* parent,
                                                          const QString& title);

    [[nodiscard]] uint16_t rgb565() const;
    [[nodiscard]] edit::Rgb888 rgb888() const;
    [[nodiscard]] edit::Hsv hsv() const;
    void setRgb565(uint16_t rgb565);

    // Most recent first, at most kRecentColorCount; kept in the application settings, not in
    // a project.
    [[nodiscard]] static std::vector<uint16_t> recentColors();
    static void rememberRecentColor(uint16_t rgb565);

private:
    void setFromRgb(edit::Rgb888 color);
    void setFromHsv(edit::Hsv hsv);
    void updateFields();
    void hexEdited();

    HsvSquareWidget* m_square = nullptr;
    ValueBarWidget* m_valueBar = nullptr;
    QLabel* m_newSwatch = nullptr;
    QLineEdit* m_hexEdit = nullptr;
    QSpinBox* m_red8 = nullptr;
    QSpinBox* m_green8 = nullptr;
    QSpinBox* m_blue8 = nullptr;
    QSpinBox* m_rgb565Edit = nullptr;
    QSpinBox* m_red5 = nullptr;
    QSpinBox* m_green6 = nullptr;
    QSpinBox* m_blue5 = nullptr;
    QDoubleSpinBox* m_hue = nullptr;
    QDoubleSpinBox* m_saturation = nullptr;
    QDoubleSpinBox* m_value = nullptr;
    std::vector<QToolButton*> m_recentButtons;
    edit::Rgb888 m_rgb;
    edit::Hsv m_hsv;
    bool m_updating = false;
};

}  // namespace colorizing::app
