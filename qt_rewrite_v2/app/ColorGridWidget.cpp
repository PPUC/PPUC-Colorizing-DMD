#include "ColorGridWidget.h"

#include <algorithm>

#include <QApplication>
#include <QDrag>
#include <QGridLayout>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolButton>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";

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

class ColorGridButton final : public QToolButton {
public:
    explicit ColorGridButton(std::size_t colorIndex, QWidget* parent = nullptr)
        : QToolButton(parent),
          m_colorIndex(colorIndex)
    {
    }

    void setGridColor(uint16_t rgb565, bool active)
    {
        m_rgb565 = rgb565;
        m_colorActive = active;
    }

    void setDoubleClickedHandler(std::function<void(std::size_t)> handler)
    {
        m_doubleClickedHandler = std::move(handler);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragStartPosition = event->pos();
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_colorActive || !(event->buttons() & Qt::LeftButton)) {
            QToolButton::mouseMoveEvent(event);
            return;
        }
        if ((event->pos() - m_dragStartPosition).manhattanLength() <
            QApplication::startDragDistance()) {
            QToolButton::mouseMoveEvent(event);
            return;
        }

        auto* mimeData = new QMimeData();
        mimeData->setData(kRgb565MimeType, QByteArray::number(m_rgb565));
        mimeData->setColorData(rgb565ToColor(m_rgb565));

        auto* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        QPixmap pixmap(24, 20);
        pixmap.fill(rgb565ToColor(m_rgb565));
        drag->setPixmap(pixmap);
        drag->exec(Qt::CopyAction);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (m_colorActive && event->button() == Qt::LeftButton && m_doubleClickedHandler) {
            m_doubleClickedHandler(m_colorIndex);
            event->accept();
            return;
        }
        QToolButton::mouseDoubleClickEvent(event);
    }

private:
    std::size_t m_colorIndex = 0;
    uint16_t m_rgb565 = 0;
    bool m_colorActive = false;
    QPoint m_dragStartPosition;
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
            gridButton->setGridColor(color, active);
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

}  // namespace colorizing::app
