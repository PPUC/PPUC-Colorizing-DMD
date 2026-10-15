#include "SectionStack.h"

#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace colorizing::app {

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
    header->setArrowType(Qt::RightArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    // The content scrolls within the section's share of the height.
    auto* frame = new QScrollArea(this);
    frame->setWidgetResizable(true);
    frame->setFrameShape(QFrame::NoFrame);
    frame->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frame->setWidget(content);
    frame->setVisible(false);
    // Header and content, one after the other, before the stretch.
    m_layout->insertWidget(index * 2, header);
    m_layout->insertWidget(index * 2 + 1, frame);
    m_sections.push_back(Section{key, title.isEmpty() ? key : title, content, header, frame});
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
        const bool open = m_sections[index].header->isChecked();
        anyOpen = anyOpen || open;
        m_layout->setStretch(static_cast<int>(index * 2 + 1), open ? 1 : 0);
    }
    m_layout->setStretch(static_cast<int>(m_sections.size() * 2), anyOpen ? 0 : 1);
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
