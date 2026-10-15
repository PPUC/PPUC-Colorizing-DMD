#include "SectionStack.h"

#include <QScrollBar>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace colorizing::app {

SectionStack::SectionStack(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget(this);
    m_layout = new QVBoxLayout(inner);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    m_layout->addStretch(1);
    setWidget(inner);
}

int SectionStack::addTab(QWidget* content, const QString& title)
{
    return insertTab(count(), content, title);
}

int SectionStack::insertTab(int index, QWidget* content, const QString& title)
{
    index = std::clamp(index, 0, count());
    auto* header = new QToolButton(QScrollArea::widget());
    header->setText(title);
    header->setCheckable(true);
    header->setChecked(false);
    header->setArrowType(Qt::RightArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    auto* frame = new QWidget(QScrollArea::widget());
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(0, 0, 0, 4);
    content->setParent(frame);
    frameLayout->addWidget(content);
    frame->setVisible(false);
    // Header and content, one after the other; the stretch stays last.
    m_layout->insertWidget(index * 2, header);
    m_layout->insertWidget(index * 2 + 1, frame);
    m_sections.insert(m_sections.begin() + index, Section{title, content, header, frame});
    if (m_current >= index) {
        ++m_current;
    }
    connect(header, &QToolButton::toggled, this, [this, content](bool open) {
        const int position = indexOf(content);
        Section& section = m_sections[static_cast<std::size_t>(position)];
        section.header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        section.frame->setVisible(open);
        if (open) {
            setCurrent(position);
        }
        storeOpenSections();
    });
    return index;
}

int SectionStack::count() const
{
    return static_cast<int>(m_sections.size());
}

QString SectionStack::tabText(int index) const
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
    // Only up or down: the column never scrolls sideways.
    const Section& section = m_sections[static_cast<std::size_t>(index)];
    ensureWidgetVisible(section.header, 0, 0);
    horizontalScrollBar()->setValue(0);
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
    for (int index = 0; index < count(); ++index) {
        setSectionOpen(index, open.contains(tabText(index)));
    }
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
}

}  // namespace colorizing::app
