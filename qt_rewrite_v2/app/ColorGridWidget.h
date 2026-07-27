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

    void setColors(const std::vector<uint16_t>& colors, std::size_t activeCount);
    void setColorClickedHandler(std::function<void(std::size_t)> handler);

private:
    std::vector<QToolButton*> m_buttons;
    std::function<void(std::size_t)> m_colorClickedHandler;
};

}  // namespace colorizing::app
