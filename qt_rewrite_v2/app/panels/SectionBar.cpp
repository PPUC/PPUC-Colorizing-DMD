#include "panels/SectionBar.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace colorizing::app {

SectionBar::SectionBar(QWidget* parent)
    : QWidget(parent)
{
    m_sectionComboBox = new QComboBox(this);
    m_sectionComboBox->setToolTip(
        QStringLiteral("Section of the current frame; choose one to go to its first frame"));
    m_sectionComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sectionComboBox->setMinimumContentsLength(8);
    m_nameButton = new QToolButton(this);
    m_nameButton->setText(QStringLiteral("Name"));
    m_nameButton->setToolTip(QStringLiteral(
        "Start a section at the current frame, or rename the section that starts there"));
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setText(QStringLiteral("Delete"));
    m_deleteButton->setToolTip(QStringLiteral("Delete the section of the current frame"));
    m_orderButton = new QToolButton(this);
    m_orderButton->setText(QStringLiteral("Order"));
    m_orderButton->setToolTip(QStringLiteral("Change the order of the section list"));
    m_triggerComboBox = new QComboBox(this);
    m_triggerComboBox->setToolTip(
        QStringLiteral("Frames with a trigger ID, by ID; choose one to go to the frame"));
    m_triggerComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_triggerComboBox->setMinimumContentsLength(8);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* sectionRow = new QHBoxLayout();
    sectionRow->setSpacing(2);
    sectionRow->addWidget(m_sectionComboBox, 1);
    sectionRow->addWidget(m_nameButton);
    sectionRow->addWidget(m_deleteButton);
    sectionRow->addWidget(m_orderButton);
    layout->addLayout(sectionRow);
    layout->addWidget(m_triggerComboBox);

    connect(m_sectionComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            this,
            [this](int row) {
                const QVariant section = m_sectionComboBox->itemData(row);
                if (section.isValid() && onSectionChosen) {
                    onSectionChosen(section.toULongLong());
                }
            });
    connect(m_triggerComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            this,
            [this](int row) {
                const QVariant frame = m_triggerComboBox->itemData(row);
                if (frame.isValid() && onTriggerChosen) {
                    onTriggerChosen(frame.toULongLong());
                }
            });
    connect(m_nameButton, &QToolButton::clicked, this, [this]() {
        if (onNameRequested) {
            onNameRequested();
        }
    });
    connect(m_deleteButton, &QToolButton::clicked, this, [this]() {
        if (onDeleteRequested) {
            onDeleteRequested();
        }
    });
    connect(m_orderButton, &QToolButton::clicked, this, [this]() {
        if (onOrderRequested) {
            onOrderRequested();
        }
    });
    setContent(false, {}, {});
    setCurrent(std::nullopt, false, std::nullopt);
}

void SectionBar::setContent(bool hasProject,
                            const std::vector<SectionBarEntry>& sections,
                            const std::vector<TriggerBarEntry>& triggers)
{
    m_hasProject = hasProject;
    {
        const QSignalBlocker blocker(m_sectionComboBox);
        m_sectionComboBox->clear();
        m_sectionComboBox->addItem(QStringLiteral("- None -"));
        for (std::size_t i = 0; i < sections.size(); ++i) {
            m_sectionComboBox->addItem(QStringLiteral("%1 (frame %2)")
                                           .arg(sections[i].name)
                                           .arg(static_cast<qulonglong>(sections[i].firstFrame)),
                                       QVariant::fromValue<qulonglong>(i));
        }
    }
    m_sectionComboBox->setEnabled(hasProject && !sections.empty());
    m_orderButton->setEnabled(hasProject && sections.size() > 1);

    const QSignalBlocker blocker(m_triggerComboBox);
    m_triggerComboBox->clear();
    m_triggerComboBox->addItem(triggers.empty()
                                   ? QStringLiteral("No triggers")
                                   : QStringLiteral("%1 trigger(s)")
                                         .arg(static_cast<int>(triggers.size())));
    m_triggerFrames.clear();
    for (const TriggerBarEntry& trigger : triggers) {
        m_triggerComboBox->addItem(QStringLiteral("Fr: %1 / TID: %2")
                                       .arg(static_cast<qulonglong>(trigger.frameIndex))
                                       .arg(trigger.triggerId),
                                   QVariant::fromValue<qulonglong>(trigger.frameIndex));
        m_triggerFrames.push_back(trigger.frameIndex);
    }
    m_triggerComboBox->setEnabled(hasProject && !triggers.empty());
}

void SectionBar::setCurrent(std::optional<std::size_t> currentSection,
                            bool startsHere,
                            std::optional<std::size_t> currentFrame)
{
    {
        const QSignalBlocker blocker(m_sectionComboBox);
        m_sectionComboBox->setCurrentIndex(
            currentSection ? static_cast<int>(*currentSection) + 1 : 0);
    }
    m_nameButton->setEnabled(m_hasProject && currentFrame.has_value());
    m_nameButton->setText(startsHere ? QStringLiteral("Rename") : QStringLiteral("Name"));
    m_deleteButton->setEnabled(m_hasProject && currentSection.has_value());
    int row = 0;
    if (currentFrame) {
        for (std::size_t i = 0; i < m_triggerFrames.size(); ++i) {
            if (m_triggerFrames[i] == *currentFrame) {
                row = static_cast<int>(i) + 1;
                break;
            }
        }
    }
    const QSignalBlocker blocker(m_triggerComboBox);
    m_triggerComboBox->setCurrentIndex(row);
}

SectionOrderDialog::SectionOrderDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Order Sections"));
    m_list = new QListWidget(this);
    m_upButton = new QPushButton(QStringLiteral("Up"), this);
    m_downButton = new QPushButton(QStringLiteral("Down"), this);
    m_sortButton = new QPushButton(QStringLiteral("A-Z"), this);
    m_sortButton->setToolTip(QStringLiteral("Sort the list by name"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto* side = new QVBoxLayout();
    side->addWidget(m_upButton);
    side->addWidget(m_downButton);
    side->addWidget(m_sortButton);
    side->addStretch(1);
    auto* row = new QHBoxLayout();
    row->addWidget(m_list, 1);
    row->addLayout(side);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(row);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateButtons(); });
    connect(m_upButton, &QPushButton::clicked, this, [this]() {
        if (onMoveRequested && m_list->currentRow() > 0) {
            onMoveRequested(m_list->currentRow(), -1);
        }
    });
    connect(m_downButton, &QPushButton::clicked, this, [this]() {
        if (onMoveRequested && m_list->currentRow() >= 0 &&
            m_list->currentRow() + 1 < m_list->count()) {
            onMoveRequested(m_list->currentRow(), 1);
        }
    });
    connect(m_sortButton, &QPushButton::clicked, this, [this]() {
        if (onSortRequested) {
            onSortRequested();
        }
    });
    updateButtons();
}

void SectionOrderDialog::setNames(const QStringList& names, int selectedRow)
{
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    m_list->addItems(names);
    m_list->setCurrentRow(selectedRow);
    updateButtons();
}

void SectionOrderDialog::updateButtons()
{
    const int row = m_list->currentRow();
    m_upButton->setEnabled(row > 0);
    m_downButton->setEnabled(row >= 0 && row + 1 < m_list->count());
    m_sortButton->setEnabled(m_list->count() > 1);
}

}  // namespace colorizing::app
