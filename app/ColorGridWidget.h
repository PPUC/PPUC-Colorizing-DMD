#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QCoreApplication>
#include <QWidget>

class QToolButton;

namespace colorizing::app {

class ColorGridWidget : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ColorGridWidget)

public:
    explicit ColorGridWidget(std::size_t colorCount,
                             int columns,
                             QWidget* parent = nullptr);

    void setDisplayIndexOffset(int offset);
    void setColors(const std::vector<uint16_t>& colors, std::size_t activeCount);
    void setColorClickedHandler(std::function<void(std::size_t)> handler);
    void setColorDoubleClickedHandler(std::function<void(std::size_t)> handler);
    // Ctrl+drag (Cmd+drag on macOS) across the colors picks a range, as the Win32 editor's
    // palette did for gradients: the handler hears the first and the last color in the order
    // of the drag (reversed with Shift held), while the drag goes on.
    void setRangeDraggedHandler(std::function<void(std::size_t first, std::size_t last)> handler);
    // The color under a point of the screen; nothing between and beside the colors.
    [[nodiscard]] std::optional<std::size_t> colorAt(const QPoint& globalPosition) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reportRange(std::size_t last, bool reversed);

    std::optional<std::size_t> m_rangeStart;
    std::vector<QToolButton*> m_buttons;
    int m_displayIndexOffset = 0;
    std::function<void(std::size_t)> m_colorClickedHandler;
    std::function<void(std::size_t)> m_colorDoubleClickedHandler;
    std::function<void(std::size_t, std::size_t)> m_rangeDraggedHandler;
};

}  // namespace colorizing::app
