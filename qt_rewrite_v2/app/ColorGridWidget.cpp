#include "ColorGridWidget.h"

#include <algorithm>

#include <QGridLayout>
#include <QToolButton>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

QString colorButtonStyle(uint16_t rgb565, bool active)
{
    const QColor color = rgb565ToColor(rgb565);
    const QString border = active ? QStringLiteral("#666") : QStringLiteral("#999");
    const QString opacity = active ? QStringLiteral("1.0") : QStringLiteral("0.45");
    return QStringLiteral(
               "QToolButton { background: %1; border: 1px solid %2; opacity: %3; }"
               "QToolButton:disabled { color: #777; }")
        .arg(color.name(), border, opacity);
}

}  // namespace

ColorGridWidget::ColorGridWidget(std::size_t colorCount, int columns, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(3);
    layout->setVerticalSpacing(3);

    const int safeColumns = std::max(1, columns);
    m_buttons.reserve(colorCount);
    for (std::size_t index = 0; index < colorCount; ++index) {
        auto* button = new QToolButton(this);
        button->setText(QString::number(static_cast<int>(index)));
        button->setFixedSize(24, 20);
        button->setEnabled(false);
        button->setToolTip(QStringLiteral("Color %1").arg(static_cast<int>(index)));
        const int row = static_cast<int>(index) / safeColumns;
        const int column = static_cast<int>(index) % safeColumns;
        layout->addWidget(button, row, column);
        m_buttons.push_back(button);
        QObject::connect(button, &QToolButton::clicked, this, [this, index]() {
            if (m_colorClickedHandler) {
                m_colorClickedHandler(index);
            }
        });
    }
}

void ColorGridWidget::setColors(const std::vector<uint16_t>& colors, std::size_t activeCount)
{
    const std::size_t visibleCount = std::min(colors.size(), m_buttons.size());
    for (std::size_t index = 0; index < m_buttons.size(); ++index) {
        QToolButton* button = m_buttons[index];
        const bool hasColor = index < visibleCount;
        const bool active = hasColor && index < activeCount;
        const uint16_t color = hasColor ? colors[index] : uint16_t{0};
        button->setEnabled(active);
        button->setStyleSheet(colorButtonStyle(color, active));
        button->setToolTip(active
                               ? QStringLiteral("Color %1: 0x%2")
                                     .arg(static_cast<int>(index))
                                     .arg(color, 4, 16, QLatin1Char('0'))
                               : QStringLiteral("Color %1 is outside the active rotation length")
                                     .arg(static_cast<int>(index)));
    }
}

void ColorGridWidget::setColorClickedHandler(std::function<void(std::size_t)> handler)
{
    m_colorClickedHandler = std::move(handler);
}

}  // namespace colorizing::app
