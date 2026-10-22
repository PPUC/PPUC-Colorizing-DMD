#include "panels/SameFramesPanel.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace colorizing::app {

namespace {

constexpr int kListHeight = 84;

QToolButton* makeButton(QWidget* parent, const QString& text, const QString& toolTip)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setEnabled(false);
    return button;
}

QString frameNumber(std::size_t frameIndex)
{
    return QString::number(static_cast<qulonglong>(frameIndex));
}

}  // namespace

SameFramesPanel::SameFramesPanel(QWidget* parent)
    : QWidget(parent)
{
    m_sameCountLabel = new QLabel(tr("Same frames: -"), this);
    m_sameCountLabel->setToolTip(tr(
        "Frames the runtime cannot tell from the current one under its mask and shape mode"));
    m_sameFramesComboBox = new QComboBox(this);
    m_sameFramesComboBox->setToolTip(tr("Go to a frame that is the same"));
    m_sameFramesComboBox->setEnabled(false);
    m_sameFramesComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sameFramesComboBox->setMinimumContentsLength(6);

    m_deleteSameAsCurrentButton =
        makeButton(this, tr("Delete Same As Current..."),
                   tr("Delete the frames that are the same as the current frame"));
    m_deleteSameAsSelectedButton =
        makeButton(this, tr("Delete Same As Selected..."),
                   tr("Delete the frames that are the same as a selected frame"));
    m_deleteAllDuplicatesButton =
        makeButton(this, tr("Delete All Duplicates..."),
                   tr("Delete every frame that is the same as an earlier frame"));
    m_keepSelectedCheckBox = new QCheckBox(tr("Keep selected frames"), this);
    m_keepSelectedCheckBox->setToolTip(
        tr("Deleting same frames never deletes a selected frame"));

    m_updateCountsButton =
        makeButton(this, tr("Update Same List"),
                   tr("Count the same frames of the current frame for every mask, "
                      "with and without shape mode"));
    m_maskCountsList = new QListWidget(this);
    m_maskCountsList->setToolTip(
        tr("Same frames of the current frame per mask: full comparison / shape mode"));
    m_maskCountsList->setFixedHeight(kListHeight);
    m_maskCountsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_maskCountsList->setFocusPolicy(Qt::NoFocus);

    m_spotCommonButton =
        makeButton(this, tr("Spot Common"),
                   tr("Mark the pixels that differ among the selected frames on "
                      "the original frame"));
    m_spotCommonButton->setCheckable(true);
    m_spotMarksComboBox = new QComboBox(this);
    m_spotMarksComboBox->addItem(tr("Differing Pixels"), static_cast<int>(SpotMarks::Differing));
    m_spotMarksComboBox->addItem(tr("Common Pixels"), static_cast<int>(SpotMarks::Common));
    m_spotMarksComboBox->setToolTip(
        tr("Which pixels Spot Common marks: those in which the selected frames differ, or "
           "those they have in common (as the Win32 editor)"));
    m_commonToMaskButton =
        makeButton(this, tr("Common To Mask"),
                   tr("Make the comparison mask of the current frame cover exactly "
                      "the pixels that differ among the selected frames"));

    m_usingMaskComboBox = new QComboBox(this);
    m_usingMaskComboBox->setToolTip(tr("List the frames that use this mask"));
    m_usingMaskComboBox->setEnabled(false);
    m_usingMaskList = new QListWidget(this);
    m_usingMaskList->setToolTip(tr("Frames using the mask above; click to go there"));
    m_usingMaskList->setFixedHeight(kListHeight);
    m_usingMaskList->setUniformItemSizes(true);
    m_selectUsingMaskButton =
        makeButton(this, tr("Select Them"),
                   tr("Select the frames that use the mask above"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* sameLayout = new QHBoxLayout();
    sameLayout->addWidget(m_sameCountLabel, 1);
    sameLayout->addWidget(m_sameFramesComboBox);
    layout->addLayout(sameLayout);
    layout->addWidget(m_deleteSameAsCurrentButton);
    layout->addWidget(m_deleteSameAsSelectedButton);
    layout->addWidget(m_deleteAllDuplicatesButton);
    layout->addWidget(m_keepSelectedCheckBox);
    layout->addWidget(m_updateCountsButton);
    layout->addWidget(m_maskCountsList);
    auto* commonLayout = new QHBoxLayout();
    commonLayout->addWidget(m_spotCommonButton);
    commonLayout->addWidget(m_commonToMaskButton);
    commonLayout->addStretch(1);
    layout->addLayout(commonLayout);
    auto* marksLayout = new QHBoxLayout();
    marksLayout->addWidget(new QLabel(tr("Spot Common marks"), this));
    marksLayout->addWidget(m_spotMarksComboBox, 1);
    layout->addLayout(marksLayout);
    auto* usingLayout = new QHBoxLayout();
    usingLayout->addWidget(new QLabel(tr("Frames using"), this));
    usingLayout->addWidget(m_usingMaskComboBox, 1);
    layout->addLayout(usingLayout);
    layout->addWidget(m_usingMaskList);
    layout->addWidget(m_selectUsingMaskButton);

    connect(m_sameFramesComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
            this,
            [this](int index) {
                if (index >= 0 && onFrameRequested) {
                    onFrameRequested(m_sameFramesComboBox->itemData(index).toULongLong());
                }
            });
    connect(m_usingMaskList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item && onFrameRequested) {
            onFrameRequested(item->data(Qt::UserRole).toULongLong());
        }
    });
    connect(m_usingMaskComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onUsingMaskChanged) {
                    onUsingMaskChanged();
                }
            });
    connect(m_spotCommonButton, &QToolButton::toggled, this, [this](bool checked) {
        if (onSpotCommonToggled) {
            onSpotCommonToggled(checked);
        }
    });
    connect(m_spotMarksComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                if (onSpotMarksChanged) {
                    onSpotMarksChanged(spotMarks());
                }
            });
    const auto forward = [this](QToolButton* button, std::function<void()> SameFramesPanel::*slot) {
        connect(button, &QToolButton::clicked, this, [this, slot]() {
            if (this->*slot) {
                (this->*slot)();
            }
        });
    };
    forward(m_updateCountsButton, &SameFramesPanel::onUpdateCountsRequested);
    forward(m_deleteSameAsCurrentButton, &SameFramesPanel::onDeleteSameAsCurrentRequested);
    forward(m_deleteSameAsSelectedButton, &SameFramesPanel::onDeleteSameAsSelectedRequested);
    forward(m_deleteAllDuplicatesButton, &SameFramesPanel::onDeleteAllDuplicatesRequested);
    forward(m_commonToMaskButton, &SameFramesPanel::onCommonToMaskRequested);
    forward(m_selectUsingMaskButton, &SameFramesPanel::onSelectUsingMaskRequested);
}

void SameFramesPanel::refresh(const SameFramesPanelState& state)
{
    QSignalBlocker blocker(m_sameFramesComboBox);
    m_sameFramesComboBox->clear();
    if (!state.hasFrame) {
        m_sameCountLabel->setText(tr("Same frames: -"));
    } else {
        const std::size_t listed = std::min(state.sameFrames.size(), edit::kMaxListedSameFrames);
        // As the Win32 editor says it once its list is full.
        m_sameCountLabel->setText(
            state.sameFrames.size() >= edit::kMaxListedSameFrames
                ? tr("Same frames: >=%1")
                      .arg(static_cast<qulonglong>(edit::kMaxListedSameFrames))
                : tr("Same frames: %1")
                      .arg(static_cast<qulonglong>(state.sameFrames.size())));
        for (std::size_t position = 0; position < listed; ++position) {
            m_sameFramesComboBox->addItem(frameNumber(state.sameFrames[position]),
                                          static_cast<qulonglong>(state.sameFrames[position]));
        }
        m_sameFramesComboBox->setCurrentIndex(-1);
    }
    m_sameFramesComboBox->setEnabled(m_sameFramesComboBox->count() > 0);
    const bool canDelete = state.hasFrame && state.frameCount > 1;
    m_deleteSameAsCurrentButton->setEnabled(canDelete && !state.sameFrames.empty());
    m_deleteSameAsSelectedButton->setEnabled(canDelete && state.selectedFrameCount > 0);
    m_deleteAllDuplicatesButton->setEnabled(canDelete);
    m_updateCountsButton->setEnabled(state.hasFrame);
    m_spotCommonButton->setEnabled(state.hasFrame);
    m_commonToMaskButton->setEnabled(state.hasFrame && state.hasAssignedMask &&
                                     state.selectedFrameCount > 1);
}

void SameFramesPanel::setMaskCounts(const std::vector<edit::SameFrameCounts>* counts)
{
    m_maskCountsList->clear();
    if (!counts) {
        return;
    }
    for (const edit::SameFrameCounts& line : *counts) {
        const QString mask = line.maskId == document::kNoComparisonMaskId
                                 ? tr("No mask")
                                 : tr("Mask %1").arg(static_cast<int>(line.maskId));
        m_maskCountsList->addItem(tr("%1: %2 / shape %3")
                                      .arg(mask)
                                      .arg(static_cast<qulonglong>(line.fullComparison))
                                      .arg(static_cast<qulonglong>(line.shapeMode)));
    }
}

void SameFramesPanel::setMaskChoices(std::size_t framesWithoutMask,
                                     const std::vector<std::size_t>& useCounts)
{
    const int previous = usingMaskId();
    QSignalBlocker blocker(m_usingMaskComboBox);
    m_usingMaskComboBox->clear();
    m_usingMaskComboBox->addItem(
        tr("no mask (%1)").arg(static_cast<qulonglong>(framesWithoutMask)), -1);
    for (std::size_t mask = 0; mask < useCounts.size(); ++mask) {
        m_usingMaskComboBox->addItem(tr("mask %1 (%2)")
                                         .arg(static_cast<qulonglong>(mask))
                                         .arg(static_cast<qulonglong>(useCounts[mask])),
                                     static_cast<int>(mask));
    }
    const int index = m_usingMaskComboBox->findData(previous);
    m_usingMaskComboBox->setCurrentIndex(index >= 0 ? index : 0);
    m_usingMaskComboBox->setEnabled(true);
}

void SameFramesPanel::setFramesUsingMask(const std::vector<std::size_t>& frames)
{
    m_usingMaskList->clear();
    for (std::size_t frameIndex : frames) {
        auto* item = new QListWidgetItem(frameNumber(frameIndex), m_usingMaskList);
        item->setData(Qt::UserRole, static_cast<qulonglong>(frameIndex));
    }
    m_selectUsingMaskButton->setEnabled(!frames.empty());
}

void SameFramesPanel::setSpotCommonChecked(bool checked)
{
    QSignalBlocker blocker(m_spotCommonButton);
    m_spotCommonButton->setChecked(checked);
}

void SameFramesPanel::setSpotMarks(SpotMarks marks)
{
    QSignalBlocker blocker(m_spotMarksComboBox);
    m_spotMarksComboBox->setCurrentIndex(
        m_spotMarksComboBox->findData(static_cast<int>(marks)));
}

SpotMarks SameFramesPanel::spotMarks() const
{
    return m_spotMarksComboBox->currentData().toInt() == static_cast<int>(SpotMarks::Common)
               ? SpotMarks::Common
               : SpotMarks::Differing;
}

int SameFramesPanel::usingMaskId() const
{
    return m_usingMaskComboBox->count() > 0 ? m_usingMaskComboBox->currentData().toInt() : -1;
}

bool SameFramesPanel::keepsSelectedFrames() const
{
    return m_keepSelectedCheckBox->isChecked();
}

bool SameFramesPanel::isSpotCommonChecked() const
{
    return m_spotCommonButton->isChecked();
}

}  // namespace colorizing::app
