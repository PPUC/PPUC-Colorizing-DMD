#include "SectionStack.h"

#include "Appearance.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace colorizing::app {

// The thin bar below an open section that moves its border with the next open one.
class SectionResizeHandle final : public QWidget {
public:
    explicit SectionResizeHandle(QWidget* parent)
        : QWidget(parent)
    {
        setFixedHeight(6);
        setCursor(Qt::SizeVerCursor);
        setToolTip(QCoreApplication::translate(
            "SectionStack", "Drag to resize the sections; double-click to share the height equally"));
    }

    // The distance dragged since the press, in pixels; the press itself gives 0.
    std::function<void(int dy, bool pressed)> onDragged;
    std::function<void()> onDoubleClicked;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        const int y = height() / 2;
        for (int x = width() / 2 - 10; x <= width() / 2 + 10; x += 4) {
            painter.drawPoint(x, y);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressY = event->globalPosition().y();
            if (onDragged) {
                onDragged(0, true);
            }
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) && onDragged) {
            onDragged(static_cast<int>(event->globalPosition().y() - m_pressY), false);
            event->accept();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (onDoubleClicked) {
            onDoubleClicked();
        }
        event->accept();
    }

private:
    double m_pressY = 0.0;
};

namespace {

// A bell of the given height in the color, for the badge of a header.
QPixmap bellPixmap(int size, const QColor& color, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const qreal s = size;
    // The body: a dome over sides that flare out to the rim.
    QPainterPath body;
    body.moveTo(0.10 * s, 0.76 * s);
    body.cubicTo(0.22 * s, 0.64 * s, 0.22 * s, 0.50 * s, 0.24 * s, 0.38 * s);
    body.cubicTo(0.27 * s, 0.10 * s, 0.73 * s, 0.10 * s, 0.76 * s, 0.38 * s);
    body.cubicTo(0.78 * s, 0.50 * s, 0.78 * s, 0.64 * s, 0.90 * s, 0.76 * s);
    body.closeSubpath();
    painter.drawPath(body);
    // The knob on top and the clapper below the rim.
    painter.drawEllipse(QPointF(0.50 * s, 0.10 * s), 0.07 * s, 0.07 * s);
    painter.drawEllipse(QPointF(0.50 * s, 0.86 * s), 0.11 * s, 0.10 * s);
    return pixmap;
}

}  // namespace

SectionStack::SectionStack(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    // Takes the height while no section is open.
    m_layout->addStretch(1);
    // Where a dragged header would go.
    m_dropLine = new QFrame(this);
    m_dropLine->setFrameShape(QFrame::NoFrame);
    m_dropLine->setAutoFillBackground(true);
    m_dropLine->setBackgroundRole(QPalette::Highlight);
    m_dropLine->hide();
}

int SectionStack::addTab(QWidget* content, const QString& key, const QString& title)
{
    const int index = count();
    auto* header = new QToolButton(this);
    header->setText(title.isEmpty() ? key : title);
    header->setCheckable(true);
    header->setChecked(false);
    // Open is shown by the arrow, not by a highlighted button.
    header->setProperty(kPlainCheckedProperty, true);
    header->setArrowType(Qt::RightArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    // A slim bar: the text's height and a little room.
    header->setIconSize(QSize(9, 9));
    header->setFixedHeight(QFontMetrics(font).height() + 6);
    // The content scrolls within the section's share of the height.
    auto* frame = new QScrollArea(this);
    frame->setWidgetResizable(true);
    frame->setFrameShape(QFrame::NoFrame);
    frame->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frame->setWidget(content);
    frame->setVisible(false);
    auto* handle = new SectionResizeHandle(this);
    handle->setVisible(false);
    // Header, content and handle, one after the other, before the stretch.
    const int position = static_cast<int>(m_order.size());
    m_layout->insertWidget(position * 3, header);
    m_layout->insertWidget(position * 3 + 1, frame);
    m_layout->insertWidget(position * 3 + 2, handle);
    // The badge at the right end of the header; clicks and drags go to the header.
    auto* badgeLayout = new QHBoxLayout(header);
    badgeLayout->setContentsMargins(0, 0, 6, 0);
    badgeLayout->addStretch(1);
    auto* badge = new QLabel(header);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setVisible(false);
    badgeLayout->addWidget(badge);
    m_sections.push_back(
        Section{key, title.isEmpty() ? key : title, content, header, frame, handle, badge});
    m_order.push_back(index);
    // Dragged to move the section; the context menu arranges the sections.
    header->installEventFilter(this);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setToolTip(tr("Click to open or close; drag to move; right-click to arrange the "
                          "sections"));
    connect(header, &QToolButton::customContextMenuRequested, this,
            [this, content, header](const QPoint& at) {
                showContextMenu(indexOf(content), header->mapToGlobal(at));
            });
    // The heights at the press: the drag moves from them.
    auto pressed = std::make_shared<std::vector<int>>();
    handle->onDragged = [this, content, pressed](int dy, bool press) {
        const int position = indexOf(content);
        if (press) {
            pressed->clear();
            for (const Section& section : m_sections) {
                pressed->push_back(section.frame->height());
            }
            return;
        }
        // From the heights at the press, so the border follows the mouse.
        for (std::size_t i = 0; i < m_sections.size() && i < pressed->size(); ++i) {
            if (m_sections[i].header->isChecked()) {
                m_sections[i].weight = std::max(1, (*pressed)[i]);
            }
        }
        resizeBelow(position, dy);
    };
    handle->onDoubleClicked = [this]() { resetSectionSizes(); };
    connect(header, &QToolButton::toggled, this, [this, content](bool open) {
        const int position = indexOf(content);
        Section& section = m_sections[static_cast<std::size_t>(position)];
        section.header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        section.frame->setVisible(open && !isSectionOutOfView(position));
        updateStretch();
        if (open) {
            setCurrent(position);
        }
        storeOpenSections();
    });
    updateStretch();
    return index;
}

void SectionStack::setSectionBadge(int index, int count, const QString& toolTip)
{
    if (index < 0 || index >= this->count()) {
        return;
    }
    Section& section = m_sections[static_cast<std::size_t>(index)];
    section.badgeCount = std::max(0, count);
    section.badge->setVisible(section.badgeCount > 0);
    section.header->setToolTip(
        section.badgeCount > 0 && !toolTip.isEmpty()
            ? toolTip
            : tr("Click to open or close; drag to move; right-click to arrange the sections"));
    if (section.badgeCount == 0) {
        return;
    }
    // The bell and the count in the accent color, as one picture: a label shows either a
    // picture or a text.
    const QColor color = palette().color(QPalette::Highlight);
    QFont font = section.header->font();
    const int height = QFontMetrics(font).height();
    const QString text = QString::number(section.badgeCount);
    const int bellSize = height - 2;
    const int width = bellSize + 3 + QFontMetrics(font).horizontalAdvance(text);
    const qreal ratio = devicePixelRatioF();
    QPixmap pixmap(QSize(width, height) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.drawPixmap(0, 1, bellPixmap(bellSize, color, ratio));
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRect(bellSize + 3, 0, width - bellSize - 3, height),
                     Qt::AlignLeft | Qt::AlignVCenter, text);
    painter.end();
    section.badge->setPixmap(pixmap);
}

int SectionStack::sectionBadge(int index) const
{
    return index >= 0 && index < count() ? m_sections[static_cast<std::size_t>(index)].badgeCount
                                         : 0;
}

void SectionStack::updateStretch()
{
    bool anyOpen = false;
    for (std::size_t position = 0; position < m_order.size(); ++position) {
        const int index = m_order[position];
        const Section& section = m_sections[static_cast<std::size_t>(index)];
        const bool open = isSectionShown(index);
        anyOpen = anyOpen || open;
        m_layout->setStretch(static_cast<int>(position * 3 + 1), open ? section.weight : 0);
        section.handle->setVisible(open && nextOpenSection(index) >= 0);
    }
    m_layout->setStretch(static_cast<int>(m_order.size() * 3), anyOpen ? 0 : 1);
}

int SectionStack::positionOf(int index) const
{
    const auto found = std::find(m_order.begin(), m_order.end(), index);
    return found == m_order.end() ? -1 : static_cast<int>(found - m_order.begin());
}

int SectionStack::nextOpenSection(int index) const
{
    const int position = positionOf(index);
    for (std::size_t next = static_cast<std::size_t>(position + 1); next < m_order.size();
         ++next) {
        if (isSectionShown(m_order[next])) {
            return m_order[next];
        }
    }
    return -1;
}

void SectionStack::relayout()
{
    while (m_layout->count() > 0) {
        delete m_layout->takeAt(0);
    }
    for (int index : m_order) {
        const Section& section = m_sections[static_cast<std::size_t>(index)];
        m_layout->addWidget(section.header);
        m_layout->addWidget(section.frame);
        m_layout->addWidget(section.handle);
    }
    m_layout->addStretch(1);
    updateStretch();
}

QStringList SectionStack::sectionOrder() const
{
    QStringList keys;
    for (int index : m_order) {
        keys << tabText(index);
    }
    return keys;
}

void SectionStack::moveSection(int index, int position)
{
    const int from = positionOf(index);
    if (from < 0) {
        return;
    }
    position = std::clamp(position, 0, count() - 1);
    if (position == from) {
        return;
    }
    m_order.erase(m_order.begin() + from);
    m_order.insert(m_order.begin() + position, index);
    relayout();
    storeArrangement();
}

void SectionStack::resetArrangement()
{
    for (int index = 0; index < count(); ++index) {
        m_order[static_cast<std::size_t>(index)] = index;
        m_sections[static_cast<std::size_t>(index)].hiddenByAuthor = false;
        applyVisibility(index);
    }
    relayout();
    storeArrangement();
}

void SectionStack::setSectionOptional(int index, bool optional)
{
    if (index >= 0 && index < count()) {
        m_sections[static_cast<std::size_t>(index)].optional = optional;
        if (!optional) {
            setSectionHiddenByAuthor(index, false);
        }
    }
}

bool SectionStack::isSectionOptional(int index) const
{
    return index >= 0 && index < count() && m_sections[static_cast<std::size_t>(index)].optional;
}

bool SectionStack::isSectionHiddenByAuthor(int index) const
{
    return index >= 0 && index < count() &&
           m_sections[static_cast<std::size_t>(index)].hiddenByAuthor;
}

void SectionStack::setSectionHiddenByAuthor(int index, bool hidden)
{
    if (index < 0 || index >= count()) {
        return;
    }
    Section& section = m_sections[static_cast<std::size_t>(index)];
    if ((hidden && !section.optional) || section.hiddenByAuthor == hidden) {
        return;
    }
    section.hiddenByAuthor = hidden;
    applyVisibility(index);
    updateStretch();
    storeArrangement();
}

bool SectionStack::isSectionOutOfView(int index) const
{
    const Section& section = m_sections[static_cast<std::size_t>(index)];
    return section.hidden || section.hiddenByAuthor;
}

void SectionStack::applyVisibility(int index)
{
    Section& section = m_sections[static_cast<std::size_t>(index)];
    const bool outOfView = isSectionOutOfView(index);
    section.header->setVisible(!outOfView);
    section.frame->setVisible(!outOfView && section.header->isChecked());
}

QStringList SectionStack::contextMenuEntries(int index) const
{
    QStringList entries;
    entries << tr("Move Up") << tr("Move Down");
    if (isSectionOptional(index)) {
        entries << tr("Hide %1").arg(title(index));
    }
    for (int other = 0; other < count(); ++other) {
        if (isSectionHiddenByAuthor(other)) {
            entries << tr("Show %1").arg(title(other));
        }
    }
    entries << tr("Restore the Sections");
    return entries;
}

void SectionStack::showContextMenu(int index, const QPoint& globalPos)
{
    if (index < 0) {
        return;
    }
    // Up and down past the sections in view: those the editor hides for this canvas are
    // passed over.
    const int position = positionOf(index);
    int previous = -1;
    for (int p = position - 1; p >= 0 && previous < 0; --p) {
        if (!isSectionOutOfView(m_order[static_cast<std::size_t>(p)])) {
            previous = p;
        }
    }
    int next = -1;
    for (int p = position + 1; p < count() && next < 0; ++p) {
        if (!isSectionOutOfView(m_order[static_cast<std::size_t>(p)])) {
            next = p;
        }
    }
    QMenu menu(this);
    QAction* up = menu.addAction(tr("Move Up"));
    up->setEnabled(previous >= 0);
    connect(up, &QAction::triggered, this, [this, index, previous]() {
        moveSection(index, previous);
    });
    QAction* down = menu.addAction(tr("Move Down"));
    down->setEnabled(next >= 0);
    connect(down, &QAction::triggered, this, [this, index, next]() { moveSection(index, next); });
    menu.addSeparator();
    if (isSectionOptional(index)) {
        QAction* hide = menu.addAction(tr("Hide %1").arg(title(index)));
        connect(hide, &QAction::triggered, this,
                [this, index]() { setSectionHiddenByAuthor(index, true); });
    }
    for (int other = 0; other < count(); ++other) {
        if (isSectionHiddenByAuthor(other)) {
            QAction* show = menu.addAction(tr("Show %1").arg(title(other)));
            connect(show, &QAction::triggered, this,
                    [this, other]() { setSectionHiddenByAuthor(other, false); });
        }
    }
    menu.addSeparator();
    QAction* reset = menu.addAction(tr("Restore the Sections"));
    reset->setToolTip(tr("The first order, every section shown"));
    connect(reset, &QAction::triggered, this, [this]() { resetArrangement(); });
    menu.exec(globalPos);
}

int SectionStack::dropPosition(int globalY) const
{
    const int y = mapFromGlobal(QPoint(0, globalY)).y();
    for (std::size_t position = 0; position < m_order.size(); ++position) {
        const Section& section = m_sections[static_cast<std::size_t>(m_order[position])];
        if (section.header->isHidden()) {
            continue;
        }
        if (y < section.header->geometry().center().y()) {
            return static_cast<int>(position);
        }
    }
    return static_cast<int>(m_order.size());
}

bool SectionStack::eventFilter(QObject* watched, QEvent* event)
{
    int index = -1;
    for (int candidate = 0; candidate < count(); ++candidate) {
        if (m_sections[static_cast<std::size_t>(candidate)].header == watched) {
            index = candidate;
        }
    }
    if (index < 0) {
        return QWidget::eventFilter(watched, event);
    }
    auto* header = m_sections[static_cast<std::size_t>(index)].header;
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragIndex = index;
                m_dragging = false;
                m_pressY = static_cast<int>(mouse->globalPosition().y());
            }
            break;
        }
        case QEvent::MouseMove: {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (m_dragIndex != index || !(mouse->buttons() & Qt::LeftButton)) {
                break;
            }
            const int globalY = static_cast<int>(mouse->globalPosition().y());
            if (!m_dragging &&
                std::abs(globalY - m_pressY) >= QApplication::startDragDistance()) {
                m_dragging = true;
                header->setDown(false);
                header->setCursor(Qt::ClosedHandCursor);
            }
            if (m_dragging) {
                // The line above the header the section would go before, or below the last.
                const int position = dropPosition(globalY);
                int lineY = 0;
                if (position < count()) {
                    lineY = m_sections[static_cast<std::size_t>(m_order[position])]
                                .header->geometry()
                                .top() -
                            2;
                } else {
                    for (int p = count() - 1; p >= 0; --p) {
                        const int lastIndex = m_order[static_cast<std::size_t>(p)];
                        const Section& last = m_sections[static_cast<std::size_t>(lastIndex)];
                        if (!last.header->isHidden()) {
                            const QWidget* bottom =
                                last.frame->isVisible() ? static_cast<const QWidget*>(last.frame)
                                                        : last.header;
                            lineY = bottom->geometry().bottom() + 1;
                            break;
                        }
                    }
                }
                m_dropLine->setGeometry(0, std::max(0, lineY), width(), 3);
                m_dropLine->show();
                m_dropLine->raise();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const bool wasDragging = m_dragging && m_dragIndex == index;
            m_dragging = false;
            m_dragIndex = -1;
            if (wasDragging && mouse->button() == Qt::LeftButton) {
                m_dropLine->hide();
                header->unsetCursor();
                header->setDown(false);
                int position = dropPosition(static_cast<int>(mouse->globalPosition().y()));
                // Placed before the header at that position, counted with the section itself
                // still in the order.
                if (position > positionOf(index)) {
                    --position;
                }
                moveSection(index, position);
                // Not a click: the section stays open or closed.
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

int SectionStack::sectionWeight(int index) const
{
    return index >= 0 && index < count() ? m_sections[static_cast<std::size_t>(index)].weight : 0;
}

bool SectionStack::resizeBelow(int index, int dy)
{
    const int below = nextOpenSection(index);
    if (!isSectionShown(index) || below < 0) {
        return false;
    }
    Section& upper = m_sections[static_cast<std::size_t>(index)];
    Section& lower = m_sections[static_cast<std::size_t>(below)];
    const int total = upper.weight + lower.weight;
    const int minimum = std::min(kMinimumHeight, total / 2);
    upper.weight = std::clamp(upper.weight + dy, minimum, total - minimum);
    lower.weight = total - upper.weight;
    updateStretch();
    storeOpenSections();
    return true;
}

void SectionStack::resetSectionSizes()
{
    for (Section& section : m_sections) {
        section.weight = kDefaultWeight;
    }
    updateStretch();
    storeOpenSections();
}

int SectionStack::count() const
{
    return static_cast<int>(m_sections.size());
}

QString SectionStack::tabText(int index) const
{
    return index >= 0 && index < count() ? m_sections[static_cast<std::size_t>(index)].key
                                         : QString();
}

QString SectionStack::title(int index) const
{
    return index >= 0 && index < count() ? m_sections[static_cast<std::size_t>(index)].title
                                         : QString();
}

QWidget* SectionStack::widget(int index) const
{
    return index >= 0 && index < count() ? m_sections[static_cast<std::size_t>(index)].content
                                         : nullptr;
}

int SectionStack::indexOf(const QWidget* content) const
{
    for (int index = 0; index < count(); ++index) {
        if (m_sections[static_cast<std::size_t>(index)].content == content) {
            return index;
        }
    }
    return -1;
}

void SectionStack::setCurrentIndex(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    setSectionOpen(index, true);
    setCurrent(index);
}

void SectionStack::setCurrentWidget(QWidget* content)
{
    setCurrentIndex(indexOf(content));
}

int SectionStack::currentIndex() const
{
    return m_current;
}

QWidget* SectionStack::currentWidget() const
{
    return widget(m_current);
}

void SectionStack::markCurrent(const QWidget* content)
{
    const int index = indexOf(content);
    if (index >= 0) {
        setCurrent(index);
    }
}

bool SectionStack::isSectionOpen(int index) const
{
    return index >= 0 && index < count() &&
           m_sections[static_cast<std::size_t>(index)].header->isChecked();
}

bool SectionStack::isSectionHidden(int index) const
{
    return index >= 0 && index < count() && m_sections[static_cast<std::size_t>(index)].hidden;
}

void SectionStack::setSectionHidden(int index, bool hidden)
{
    if (index < 0 || index >= count()) {
        return;
    }
    Section& section = m_sections[static_cast<std::size_t>(index)];
    if (section.hidden == hidden) {
        return;
    }
    section.hidden = hidden;
    applyVisibility(index);
    updateStretch();
}

bool SectionStack::isSectionShown(int index) const
{
    return isSectionOpen(index) && !isSectionOutOfView(index);
}

void SectionStack::setSectionOpen(int index, bool open)
{
    if (index >= 0 && index < count()) {
        m_sections[static_cast<std::size_t>(index)].header->setChecked(open);
    }
}

void SectionStack::restoreOpenSections(const QString& stateKey, const QStringList& openByDefault)
{
    m_stateKey.clear();
    const QVariant stored = QSettings().value(QStringLiteral("layout/") + stateKey);
    const QStringList open = stored.isValid() ? stored.toStringList() : openByDefault;
    const QVariantMap weights =
        QSettings().value(QStringLiteral("layout/") + stateKey + QStringLiteral("Sizes")).toMap();
    for (int index = 0; index < count(); ++index) {
        const int weight = weights.value(tabText(index), kDefaultWeight).toInt();
        m_sections[static_cast<std::size_t>(index)].weight = std::max(1, weight);
        setSectionOpen(index, open.contains(tabText(index)));
    }
    // The author's order: the stored keys first in their order, a section added since at its
    // own place among them.
    const QStringList order =
        QSettings().value(QStringLiteral("layout/") + stateKey + QStringLiteral("Order"))
            .toStringList();
    if (!order.isEmpty()) {
        std::vector<int> arranged;
        for (const QString& key : order) {
            for (int index = 0; index < count(); ++index) {
                if (tabText(index) == key &&
                    std::find(arranged.begin(), arranged.end(), index) == arranged.end()) {
                    arranged.push_back(index);
                }
            }
        }
        for (int index = 0; index < count(); ++index) {
            if (std::find(arranged.begin(), arranged.end(), index) == arranged.end()) {
                arranged.insert(arranged.begin() +
                                    std::min<std::ptrdiff_t>(index,
                                                             static_cast<std::ptrdiff_t>(
                                                                 arranged.size())),
                                index);
            }
        }
        m_order = arranged;
    }
    const QStringList hidden =
        QSettings().value(QStringLiteral("layout/") + stateKey + QStringLiteral("Hidden"))
            .toStringList();
    for (int index = 0; index < count(); ++index) {
        Section& section = m_sections[static_cast<std::size_t>(index)];
        section.hiddenByAuthor = section.optional && hidden.contains(tabText(index));
        applyVisibility(index);
    }
    relayout();
    m_stateKey = stateKey;
}

void SectionStack::storeArrangement() const
{
    if (m_stateKey.isEmpty()) {
        return;
    }
    QStringList hidden;
    for (int index = 0; index < count(); ++index) {
        if (isSectionHiddenByAuthor(index)) {
            hidden << tabText(index);
        }
    }
    QSettings().setValue(QStringLiteral("layout/") + m_stateKey + QStringLiteral("Order"),
                         sectionOrder());
    QSettings().setValue(QStringLiteral("layout/") + m_stateKey + QStringLiteral("Hidden"),
                         hidden);
}

void SectionStack::setCurrent(int index)
{
    if (index == m_current) {
        return;
    }
    m_current = index;
    if (onCurrentChanged) {
        onCurrentChanged(index);
    }
}

void SectionStack::storeOpenSections() const
{
    if (m_stateKey.isEmpty()) {
        return;
    }
    QStringList open;
    for (int index = 0; index < count(); ++index) {
        if (isSectionOpen(index)) {
            open << tabText(index);
        }
    }
    QSettings().setValue(QStringLiteral("layout/") + m_stateKey, open);
    QVariantMap weights;
    for (int index = 0; index < count(); ++index) {
        weights.insert(tabText(index), sectionWeight(index));
    }
    QSettings().setValue(QStringLiteral("layout/") + m_stateKey + QStringLiteral("Sizes"), weights);
}

}  // namespace colorizing::app
