#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <QWidget>

class QToolButton;

namespace colorizing::app {

class ColorGridWidget : public QWidget {
public:
    explicit ColorGridWidget(std::size_t colorCount,
                             int columns,
                             QWidget* parent = nullptr);

    void setDisplayIndexOffset(int offset);
    void setColors(const std::vector<uint16_t>& colors, std::size_t activeCount);
    void setColorClickedHandler(std::function<void(std::size_t)> handler);
    void setColorDoubleClickedHandler(std::function<void(std::size_t)> handler);

private:
    std::vector<QToolButton*> m_buttons;
    int m_displayIndexOffset = 0;
    std::function<void(std::size_t)> m_colorClickedHandler;
    std::function<void(std::size_t)> m_colorDoubleClickedHandler;
};

}  // namespace colorizing::app
