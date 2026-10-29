#include "ColorDrag.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

// "<owner>:<slot>", the owner as its address: only a drop in the same process compares it.
constexpr char kColorSlotMimeType[] = "application/x-colorizing-color-slot";

QByteArray ownerKey(const QObject* owner)
{
    return QByteArray::number(reinterpret_cast<quintptr>(owner));
}

}  // namespace

QMimeData* colorDragMimeData(const QObject* owner, std::size_t slot, uint16_t rgb565)
{
    auto* mimeData = new QMimeData();
    mimeData->setData(kRgb565MimeType, QByteArray::number(rgb565));
    mimeData->setData(kColorSlotMimeType,
                      ownerKey(owner) + ':' + QByteArray::number(static_cast<qulonglong>(slot)));
    mimeData->setColorData(rgb565ToColor(rgb565));
    return mimeData;
}

void execColorDrag(QWidget* source, const QObject* owner, std::size_t slot, uint16_t rgb565)
{
    auto* drag = new QDrag(source);
    drag->setMimeData(colorDragMimeData(owner, slot, rgb565));
    QPixmap pixmap(24, 20);
    pixmap.fill(rgb565ToColor(rgb565));
    drag->setPixmap(pixmap);
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
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
    execColorDrag(this, m_owner, m_slot, *m_rgb565);
}

bool ColorSlotButton::takes(const QMimeData* mimeData) const
{
    if (!isEnabled()) {
        return false;
    }
    if (const std::optional<std::size_t> from = draggedSlotOf(mimeData, m_owner)) {
        return *from != m_slot && onSlotDropped;
    }
    return draggedColor(mimeData) && onColorDropped;
}

void ColorSlotButton::dragEnterEvent(QDragEnterEvent* event)
{
    if (takes(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void ColorSlotButton::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (!takes(mimeData)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    if (const std::optional<std::size_t> from = draggedSlotOf(mimeData, m_owner)) {
        onSlotDropped(*from, m_slot);
    } else {
        onColorDropped(m_slot, *draggedColor(mimeData));
    }
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
