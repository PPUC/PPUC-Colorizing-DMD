#include "ColorDrag.h"

#include <algorithm>

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include "FrameImageUtils.h"
#include "edit/DrawingTools.h"

namespace colorizing::app {

namespace {

// "<owner>:<slot>", the owner as its address: only a drop in the same process compares it.
constexpr char kColorSlotMimeType[] = "application/x-colorizing-color-slot";
// Present on a drag started with Shift held.
constexpr char kColorGradientMimeType[] = "application/x-colorizing-color-gradient";

QByteArray ownerKey(const QObject* owner)
{
    return QByteArray::number(reinterpret_cast<quintptr>(owner));
}

}  // namespace

QMimeData* colorDragMimeData(const QObject* owner,
                             std::size_t slot,
                             uint16_t rgb565,
                             bool gradient)
{
    auto* mimeData = new QMimeData();
    mimeData->setData(kRgb565MimeType, QByteArray::number(rgb565));
    mimeData->setData(kColorSlotMimeType,
                      ownerKey(owner) + ':' + QByteArray::number(static_cast<qulonglong>(slot)));
    mimeData->setColorData(rgb565ToColor(rgb565));
    if (gradient) {
        mimeData->setData(kColorGradientMimeType, QByteArray("1"));
    }
    return mimeData;
}

void execColorDrag(QWidget* source,
                   const QObject* owner,
                   std::size_t slot,
                   uint16_t rgb565,
                   bool gradient)
{
    auto* drag = new QDrag(source);
    drag->setMimeData(colorDragMimeData(owner, slot, rgb565, gradient));
    QPixmap pixmap(gradient ? 48 : 24, 20);
    if (gradient) {
        // A ramp out of the dragged color, not the swatch a swap drags.
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        QLinearGradient ramp(0, 0, pixmap.width(), 0);
        const QColor color = rgb565ToColor(rgb565);
        QColor faded = color;
        faded.setAlpha(40);
        ramp.setColorAt(0, color);
        ramp.setColorAt(1, faded);
        painter.fillRect(pixmap.rect(), ramp);
        painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
        painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    } else {
        pixmap.fill(rgb565ToColor(rgb565));
    }
    drag->setPixmap(pixmap);
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

bool isGradientDrag(const QMimeData* mimeData)
{
    return mimeData && mimeData->hasFormat(kColorGradientMimeType);
}

std::optional<uint16_t> draggedColor(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasFormat(kRgb565MimeType)) {
        return std::nullopt;
    }
    bool ok = false;
    const int value = QString::fromLatin1(mimeData->data(kRgb565MimeType)).toInt(&ok);
    if (!ok || value < 0 || value > 0xffff) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(value);
}

std::optional<std::size_t> draggedSlotOf(const QMimeData* mimeData, const QObject* owner)
{
    if (!mimeData || !mimeData->hasFormat(kColorSlotMimeType)) {
        return std::nullopt;
    }
    const QList<QByteArray> parts = mimeData->data(kColorSlotMimeType).split(':');
    if (parts.size() != 2 || parts[0] != ownerKey(owner)) {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong slot = parts[1].toULongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(slot);
}

ColorSlotButton::ColorSlotButton(std::size_t slot, const QObject* owner, QWidget* parent)
    : QToolButton(parent),
      m_slot(slot),
      m_owner(owner)
{
    setAcceptDrops(true);
}

void ColorSlotButton::setSlotColor(std::optional<uint16_t> rgb565)
{
    m_rgb565 = rgb565;
}

void ColorSlotButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPosition = event->pos();
        // Ctrl with Shift is the palette's reversed range pick, no gradient.
        m_gradientDrag = event->modifiers().testFlag(Qt::ShiftModifier) &&
                         !event->modifiers().testFlag(Qt::ControlModifier);
    }
    QToolButton::mousePressEvent(event);
}

void ColorSlotButton::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_rgb565 || !isEnabled() || !(event->buttons() & Qt::LeftButton) ||
        (event->pos() - m_dragStartPosition).manhattanLength() <
            QApplication::startDragDistance()) {
        QToolButton::mouseMoveEvent(event);
        return;
    }
    // The press does not become a click once the color is dragged away.
    setDown(false);
    execColorDrag(this, m_owner, m_slot, *m_rgb565, m_gradientDrag);
}

bool ColorSlotButton::takes(const QMimeData* mimeData) const
{
    if (!isEnabled()) {
        return false;
    }
    if (const std::optional<std::size_t> from = draggedSlotOf(mimeData, m_owner)) {
        return *from != m_slot &&
               ((isGradientDrag(mimeData) && onSlotsGradient) || onSlotDropped);
    }
    return draggedColor(mimeData) && onColorDropped;
}

void ColorSlotButton::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (takes(mimeData)) {
        event->acceptProposedAction();
        const std::optional<std::size_t> from = draggedSlotOf(mimeData, m_owner);
        const std::optional<uint16_t> fromColor = draggedColor(mimeData);
        if (from && fromColor && isGradientDrag(mimeData) && onSlotsGradient) {
            showGradientPreview(*from, *fromColor);
        }
        return;
    }
    event->ignore();
}

void ColorSlotButton::dragLeaveEvent(QDragLeaveEvent* event)
{
    clearGradientPreview();
    QToolButton::dragLeaveEvent(event);
}

std::vector<ColorSlotButton*> ColorSlotButton::rowButtons() const
{
    std::vector<ColorSlotButton*> buttons;
    if (!m_owner) {
        return buttons;
    }
    // ColorSlotButton has no Q_OBJECT of its own: look for the tool buttons that are one.
    for (QToolButton* candidate : m_owner->findChildren<QToolButton*>()) {
        auto* button = dynamic_cast<ColorSlotButton*>(candidate);
        if (button && button->m_owner == m_owner) {
            buttons.push_back(button);
        }
    }
    return buttons;
}

void ColorSlotButton::showGradientPreview(std::size_t from, uint16_t fromColor)
{
    if (!m_rgb565) {
        return;
    }
    const std::size_t first = std::min(from, m_slot);
    const std::size_t last = std::max(from, m_slot);
    for (ColorSlotButton* button : rowButtons()) {
        const std::size_t slot = button->m_slot;
        std::optional<uint16_t> preview;
        if (slot > first && slot < last && button->m_rgb565) {
            const std::size_t step = from < m_slot ? slot - from : from - slot;
            preview = edit::interpolateRgb565(fromColor, *m_rgb565, step, last - first);
        }
        const bool end = slot == first || slot == last;
        if (button->m_gradientPreview != preview || button->m_gradientEnd != end) {
            button->m_gradientPreview = preview;
            button->m_gradientEnd = end;
            button->update();
        }
    }
}

void ColorSlotButton::clearGradientPreview()
{
    for (ColorSlotButton* button : rowButtons()) {
        if (button->m_gradientPreview || button->m_gradientEnd) {
            button->m_gradientPreview.reset();
            button->m_gradientEnd = false;
            button->update();
        }
    }
}

void ColorSlotButton::paintEvent(QPaintEvent* event)
{
    QToolButton::paintEvent(event);
    QPainter painter(this);
    paintGradientPreview(painter);
}

void ColorSlotButton::paintGradientPreview(QPainter& painter) const
{
    if (!m_gradientPreview && !m_gradientEnd) {
        return;
    }
    painter.save();
    if (m_gradientPreview) {
        // The color the drop gives the slot, over its own.
        painter.fillRect(rect().adjusted(3, 3, -3, -3), rgb565ToColor(*m_gradientPreview));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 200), 3));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
    painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
    painter.restore();
}

void ColorSlotButton::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (!takes(mimeData)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    clearGradientPreview();
    if (const std::optional<std::size_t> from = draggedSlotOf(mimeData, m_owner)) {
        if (isGradientDrag(mimeData) && onSlotsGradient) {
            onSlotsGradient(*from, m_slot);
        } else {
            onSlotDropped(*from, m_slot);
        }
    } else {
        onColorDropped(m_slot, *draggedColor(mimeData));
    }
}

DrawColorButton::DrawColorButton(QWidget* parent)
    // Its own owner: a color dragged from anywhere else is no slot of it.
    : ColorSlotButton(0, this, parent)
{
}

void DrawColorButton::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (isEnabled() && event->button() == Qt::LeftButton && onDoubleClicked) {
        onDoubleClicked();
        event->accept();
        return;
    }
    ColorSlotButton::mouseDoubleClickEvent(event);
}

FavoriteColorButton::FavoriteColorButton(QWidget* parent)
    : QToolButton(parent)
{
    setAcceptDrops(true);
    setCheckable(true);
    setFixedSize(28, 24);
}

void FavoriteColorButton::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (isEnabled() && event->button() == Qt::LeftButton && onDoubleClicked) {
        onDoubleClicked();
        event->accept();
        return;
    }
    QToolButton::mouseDoubleClickEvent(event);
}

void FavoriteColorButton::dragEnterEvent(QDragEnterEvent* event)
{
    if (draggedColor(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QToolButton::dragEnterEvent(event);
}

void FavoriteColorButton::dropEvent(QDropEvent* event)
{
    const std::optional<uint16_t> rgb565 = draggedColor(event->mimeData());
    if (rgb565 && onColorDropped) {
        onColorDropped(*rgb565);
        event->acceptProposedAction();
        return;
    }
    QToolButton::dropEvent(event);
}

}  // namespace colorizing::app
