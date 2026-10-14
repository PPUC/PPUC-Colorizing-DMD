#include "panels/EditColorBar.h"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";

QToolButton* makeButton(QWidget* parent, const QString& text, const QString& tip, bool checkable)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setCheckable(checkable);
    return button;
}

}  // namespace

// The color of an edit color slot over a strip in the color of the original shade it replaces.
class EditColorSwatch final : public QToolButton {
public:
    EditColorSwatch(std::size_t slot, QWidget* parent)
        : QToolButton(parent),
          m_slot(slot)
    {
        setCheckable(true);
        setAcceptDrops(true);
        setFixedSize(24, 24);
    }

    void setColors(uint16_t rgb565, QColor shade)
    {
        m_color = rgb565ToColor(rgb565);
        m_shade = shade;
        update();
    }

    std::function<void(std::size_t)> onEditRequested;
    std::function<void(std::size_t, uint16_t)> onDropped;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const QRect area = rect().adjusted(2, 2, -2, -2);
        const int stripHeight = 5;
        painter.fillRect(area.adjusted(0, 0, 0, -stripHeight), m_color);
        painter.fillRect(QRect(area.left(), area.bottom() - stripHeight + 1, area.width(),
                               stripHeight),
                         m_shade);
        painter.setPen(isEnabled() ? QColor(0x66, 0x66, 0x66) : QColor(0x33, 0x33, 0x33));
        painter.drawRect(area.adjusted(0, 0, -1, -1));
        if (isChecked()) {
            painter.setPen(QPen(Qt::white, 2));
            painter.drawRect(rect().adjusted(1, 1, -1, -1));
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && onEditRequested) {
            onEditRequested(m_slot);
            event->accept();
            return;
        }
        QToolButton::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        QMenu menu(this);
        QAction* editAction = menu.addAction(QStringLiteral("Edit Color..."));
        if (menu.exec(event->globalPos()) == editAction && onEditRequested) {
            onEditRequested(m_slot);
        }
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(kRgb565MimeType)) {
            event->acceptProposedAction();
            return;
        }
        QToolButton::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        bool ok = false;
        const int value =
            QString::fromLatin1(event->mimeData()->data(kRgb565MimeType)).toInt(&ok);
        if (ok && value >= 0 && value <= 0xffff && onDropped) {
            onDropped(m_slot, static_cast<uint16_t>(value));
            event->acceptProposedAction();
            return;
        }
        QToolButton::dropEvent(event);
    }

private:
    std::size_t m_slot;
    QColor m_color;
    QColor m_shade;
};

EditColorBar::EditColorBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_singleColorButton = makeButton(this,
                                     QStringLiteral("1 Color"),
                                     QStringLiteral("Draw with a single color"),
                                     true);
    m_colorSetReplaceButton =
        makeButton(this,
                   QStringLiteral("Set Replace"),
                   QStringLiteral("Draw replacing each original shade by its edit color "
                                  "(color set replace)"),
                   true);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_singleColorButton);
    modeGroup->addButton(m_colorSetReplaceButton);
    m_singleColorButton->setChecked(true);
    connect(m_singleColorButton, &QToolButton::clicked, this, [this]() {
        if (onDrawColorModeChanged) {
            onDrawColorModeChanged(edit::DrawColorMode::SingleColor);
        }
    });
    connect(m_colorSetReplaceButton, &QToolButton::clicked, this, [this]() {
        if (onDrawColorModeChanged) {
            onDrawColorModeChanged(edit::DrawColorMode::ColorSetReplace);
        }
    });
    layout->addWidget(m_singleColorButton);
    layout->addWidget(m_colorSetReplaceButton);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Edit colors"), this));

    for (std::size_t slot = 0; slot < document::kEditColorCount; ++slot) {
        auto* swatch = new EditColorSwatch(slot, this);
        swatch->setObjectName(QStringLiteral("editColorSwatch%1").arg(static_cast<int>(slot)));
        swatch->setVisible(false);
        swatch->onEditRequested = [this](std::size_t index) {
            if (onSlotEditRequested) {
                onSlotEditRequested(index);
            }
        };
        swatch->onDropped = [this](std::size_t index, uint16_t rgb565) {
            if (onColorDropped) {
                onColorDropped(index, rgb565);
            }
        };
        connect(swatch, &QToolButton::clicked, this, [this, slot]() {
            // The selection follows the owner, which calls setSelectedSlot().
            setSelectedSlot(m_selectedSlot);
            if (onSlotClicked) {
                onSlotClicked(slot);
            }
        });
        m_swatches.push_back(swatch);
        layout->addWidget(swatch);
    }
    layout->addSpacing(8);
    m_toDynamicButton =
        makeButton(this,
                   QStringLiteral("Edit > Dyn"),
                   QStringLiteral("Copy the edit colors into the current dynamic set of the "
                                  "selected frames"),
                   false);
    m_fromDynamicButton =
        makeButton(this,
                   QStringLiteral("Dyn > Edit"),
                   QStringLiteral("Copy the colors of the current frame's current dynamic set "
                                  "to the edit colors"),
                   false);
    connect(m_toDynamicButton, &QToolButton::clicked, this, [this]() {
        if (onEditColorsToDynamicRequested) {
            onEditColorsToDynamicRequested();
        }
    });
    connect(m_fromDynamicButton, &QToolButton::clicked, this, [this]() {
        if (onDynamicToEditColorsRequested) {
            onDynamicToEditColorsRequested();
        }
    });
    layout->addWidget(m_toDynamicButton);
    layout->addWidget(m_fromDynamicButton);
    layout->addStretch(1);
    refresh(nullptr);
}

void EditColorBar::refresh(const document::ProjectDocument* document)
{
    m_colorCount = document ? std::min<uint32_t>(document->colorCount(),
                                                 static_cast<uint32_t>(m_swatches.size()))
                            : 0;
    m_colors = document ? document->editorMetadata().editColors : std::vector<uint16_t>{};
    for (std::size_t slot = 0; slot < m_swatches.size(); ++slot) {
        EditColorSwatch* swatch = m_swatches[slot];
        const bool used = slot < m_colorCount;
        swatch->setVisible(used);
        if (used) {
            swatch->setColors(m_colors[slot],
                              originalIndexToColor(static_cast<uint8_t>(slot), m_colorCount));
        }
    }
    const bool enabled = document != nullptr;
    m_singleColorButton->setEnabled(enabled);
    m_colorSetReplaceButton->setEnabled(enabled);
    m_toDynamicButton->setEnabled(enabled);
    m_fromDynamicButton->setEnabled(enabled);
    updateToolTips();
    setSelectedSlot(m_selectedSlot);
}

void EditColorBar::setSelectedSlot(std::optional<std::size_t> slot)
{
    m_selectedSlot = slot;
    for (std::size_t index = 0; index < m_swatches.size(); ++index) {
        m_swatches[index]->setChecked(slot && *slot == index);
    }
}

void EditColorBar::setDrawColorMode(edit::DrawColorMode mode)
{
    m_singleColorButton->setChecked(mode == edit::DrawColorMode::SingleColor);
    m_colorSetReplaceButton->setChecked(mode == edit::DrawColorMode::ColorSetReplace);
}

edit::DrawColorMode EditColorBar::drawColorMode() const
{
    return m_colorSetReplaceButton->isChecked() ? edit::DrawColorMode::ColorSetReplace
                                                : edit::DrawColorMode::SingleColor;
}

void EditColorBar::setSlotKeyNames(std::vector<QString> keyNames)
{
    m_keyNames = std::move(keyNames);
    updateToolTips();
}

void EditColorBar::updateToolTips()
{
    for (std::size_t slot = 0; slot < m_swatches.size(); ++slot) {
        QString tip = QStringLiteral("Edit color %1").arg(static_cast<int>(slot));
        if (slot < m_colorCount && slot < m_colors.size()) {
            tip += QStringLiteral(": 0x%1, replaces shade %2")
                       .arg(m_colors[slot], 4, 16, QLatin1Char('0'))
                       .arg(static_cast<int>(slot));
        }
        if (slot < m_keyNames.size() && !m_keyNames[slot].isEmpty()) {
            tip += QStringLiteral(" (key %1)").arg(m_keyNames[slot]);
        }
        m_swatches[slot]->setToolTip(tip);
    }
}

}  // namespace colorizing::app
