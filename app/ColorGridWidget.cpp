#include "ColorGridWidget.h"

#include <algorithm>

#include <QGridLayout>
#include <QMouseEvent>
#include <QToolButton>

#include "ColorDrag.h"
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

class ColorGridButton final : public ColorSlotButton {
public:
    ColorGridButton(std::size_t colorIndex, QWidget* parent)
        : ColorSlotButton(colorIndex, parent, parent)
    {
    }

    void setDoubleClickedHandler(std::function<void(std::size_t)> handler)
    {
        m_doubleClickedHandler = std::move(handler);
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (isEnabled() && event->button() == Qt::LeftButton && m_doubleClickedHandler) {
            m_doubleClickedHandler(slot());
            event->accept();
            return;
        }
        QToolButton::mouseDoubleClickEvent(event);
    }

private:
    std::function<void(std::size_t)> m_doubleClickedHandler;
};

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
        auto* button = new ColorGridButton(index, this);
        button->setText(QString::number(static_cast<int>(index)));
        button->setFixedSize(24, 20);
        button->setEnabled(false);
        button->setToolTip(tr("Color %1").arg(static_cast<int>(index)));
        const int row = static_cast<int>(index) / safeColumns;
        const int column = static_cast<int>(index) % safeColumns;
        layout->addWidget(button, row, column);
        m_buttons.push_back(button);
        button->installEventFilter(this);
        QObject::connect(button, &QToolButton::clicked, this, [this, index]() {
            if (m_colorClickedHandler) {
                m_colorClickedHandler(index);
            }
        });
        button->setDoubleClickedHandler([this](std::size_t colorIndex) {
            if (m_colorDoubleClickedHandler) {
                m_colorDoubleClickedHandler(colorIndex);
            }
        });
    }
}

void ColorGridWidget::setDisplayIndexOffset(int offset)
{
    m_displayIndexOffset = offset;
    for (std::size_t index = 0; index < m_buttons.size(); ++index) {
        m_buttons[index]->setText(
            QString::number(static_cast<int>(index) + m_displayIndexOffset));
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
        if (auto* gridButton = dynamic_cast<ColorGridButton*>(button)) {
            gridButton->setSlotColor(active ? std::optional<uint16_t>(color) : std::nullopt);
        }
        button->setEnabled(active);
        button->setStyleSheet(colorButtonStyle(color, active));
        const int displayIndex = static_cast<int>(index) + m_displayIndexOffset;
        button->setToolTip(active
                               ? tr("Color %1: 0x%2")
                                     .arg(displayIndex)
                                     .arg(color, 4, 16, QLatin1Char('0'))
                               : tr("Color %1 is outside the active rotation length")
                                     .arg(displayIndex));
    }
}

void ColorGridWidget::setColorClickedHandler(std::function<void(std::size_t)> handler)
{
    m_colorClickedHandler = std::move(handler);
}

void ColorGridWidget::setColorDoubleClickedHandler(std::function<void(std::size_t)> handler)
{
    m_colorDoubleClickedHandler = std::move(handler);
}

void ColorGridWidget::setColorsSwappedHandler(
    std::function<void(std::size_t, std::size_t)> handler)
{
    for (QToolButton* button : m_buttons) {
        if (auto* gridButton = dynamic_cast<ColorGridButton*>(button)) {
            gridButton->onSlotDropped = handler;
        }
    }
}

void ColorGridWidget::setRangeDraggedHandler(
    std::function<void(std::size_t, std::size_t)> handler)
{
    m_rangeDraggedHandler = std::move(handler);
}

std::optional<std::size_t> ColorGridWidget::colorAt(const QPoint& globalPosition) const
{
    for (std::size_t index = 0; index < m_buttons.size(); ++index) {
        const QToolButton* button = m_buttons[index];
        if (button->isEnabled() &&
            button->rect().contains(button->mapFromGlobal(globalPosition))) {
            return index;
        }
    }
    return std::nullopt;
}

void ColorGridWidget::reportRange(std::size_t last, bool reversed)
{
    if (m_rangeStart && m_rangeDraggedHandler) {
        if (reversed) {
            m_rangeDraggedHandler(last, *m_rangeStart);
        } else {
            m_rangeDraggedHandler(*m_rangeStart, last);
        }
    }
}

bool ColorGridWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_rangeDraggedHandler) {
        return QWidget::eventFilter(watched, event);
    }
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove &&
        type != QEvent::MouseButtonRelease) {
        return QWidget::eventFilter(watched, event);
    }
    auto* mouse = static_cast<QMouseEvent*>(event);
    const bool reversed = mouse->modifiers().testFlag(Qt::ShiftModifier);
    if (type == QEvent::MouseButtonPress) {
        if (mouse->button() != Qt::LeftButton ||
            !mouse->modifiers().testFlag(Qt::ControlModifier)) {
            return QWidget::eventFilter(watched, event);
        }
        m_rangeStart = colorAt(mouse->globalPosition().toPoint());
        if (!m_rangeStart) {
            return QWidget::eventFilter(watched, event);
        }
        reportRange(*m_rangeStart, reversed);
        // Not a click on the color, and no drag of it.
        return true;
    }
    if (!m_rangeStart) {
        return QWidget::eventFilter(watched, event);
    }
    if (const std::optional<std::size_t> color = colorAt(mouse->globalPosition().toPoint())) {
        reportRange(*color, reversed);
    }
    if (type == QEvent::MouseButtonRelease) {
        m_rangeStart.reset();
    }
    return true;
}

}  // namespace colorizing::app
