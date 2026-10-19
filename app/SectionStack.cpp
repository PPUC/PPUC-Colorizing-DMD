#include "SectionStack.h"

#include "Appearance.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
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

SectionStack::SectionStack(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    // Takes the height while no section is open.
    m_layout->addStretch(1);
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
    m_layout->insertWidget(index * 3, header);
    m_layout->insertWidget(index * 3 + 1, frame);
    m_layout->insertWidget(index * 3 + 2, handle);
    m_sections.push_back(
        Section{key, title.isEmpty() ? key : title, content, header, frame, handle});
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
        section.frame->setVisible(open);
        updateStretch();
        if (open) {
            setCurrent(position);
        }
        storeOpenSections();
    });
    updateStretch();
    return index;
}

void SectionStack::updateStretch()
{
    bool anyOpen = false;
    for (std::size_t index = 0; index < m_sections.size(); ++index) {
        const Section& section = m_sections[index];
        const bool open = section.header->isChecked();
        anyOpen = anyOpen || open;
        m_layout->setStretch(static_cast<int>(index * 3 + 1), open ? section.weight : 0);
        section.handle->setVisible(open && nextOpenSection(static_cast<int>(index)) >= 0);
    }
    m_layout->setStretch(static_cast<int>(m_sections.size() * 3), anyOpen ? 0 : 1);
}

int SectionStack::nextOpenSection(int index) const
{
    for (int next = index + 1; next < count(); ++next) {
        if (isSectionOpen(next)) {
            return next;
        }
    }
    return -1;
}

int SectionStack::sectionWeight(int index) const
{
    return index >= 0 && index < count() ? m_sections[static_cast<std::size_t>(index)].weight : 0;
}

bool SectionStack::resizeBelow(int index, int dy)
{
    const int below = nextOpenSection(index);
    if (!isSectionOpen(index) || below < 0) {
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
    updateStretch();
    m_stateKey = stateKey;
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
