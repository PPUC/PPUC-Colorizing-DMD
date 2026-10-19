#include "panels/BackgroundPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "PlaneLabels.h"

namespace colorizing::app {

BackgroundPanel::BackgroundPanel(QWidget* parent)
    : QWidget(parent)
{
    m_assignButton = new QToolButton(this);
    m_assignButton->setText(tr("Assign BG"));
    m_assignButton->setToolTip(tr("Assign selected background to selected frames"));
    m_assignButton->setEnabled(false);
    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Remove BG"));
    m_removeButton->setToolTip(tr("Remove the background from the selected frames"));
    m_removeButton->setEnabled(false);
    m_editButton = new QToolButton(this);
    m_editButton->setText(tr("Edit BG"));
    m_editButton->setToolTip(tr("Edit the selected background on the canvas; click again to "
                                "go back to the frame"));
    m_editButton->setCheckable(true);
    m_editButton->setEnabled(false);
    m_copyFrameToBackgroundButton = new QToolButton(this);
    m_copyFrameToBackgroundButton->setText(tr("Frame -> BG"));
    m_copyFrameToBackgroundButton->setToolTip(
        tr("Copy the current frame image into the selected background"));
    m_copyFrameToBackgroundButton->setEnabled(false);
    m_copyBackgroundToFramesButton = new QToolButton(this);
    m_copyBackgroundToFramesButton->setText(tr("BG -> Frame"));
    m_copyBackgroundToFramesButton->setToolTip(
        tr("Copy the selected background image into selected frames"));
    m_copyBackgroundToFramesButton->setEnabled(false);
    m_importImageButton = new QToolButton(this);
    m_importImageButton->setText(tr("Import Image"));
    m_importImageButton->setToolTip(
        tr("Import an image into the selected background"));
    m_importImageButton->setEnabled(false);
    m_exportButton = new QToolButton(this);
    m_exportButton->setText(tr("Export BG"));
    m_exportButton->setToolTip(tr("Export the selected background as PNG"));
    m_exportButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedBackgroundLabel = new QLabel(tr("Selected: None"), this);
    m_selectedBackgroundLabel->setWordWrap(true);
    pageLayout->addWidget(m_selectedBackgroundLabel);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_assignButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addWidget(m_editButton);
    pageLayout->addLayout(buttonLayout);
    auto* copyLayout = new QHBoxLayout();
    copyLayout->addWidget(m_copyFrameToBackgroundButton);
    copyLayout->addWidget(m_copyBackgroundToFramesButton);
    pageLayout->addLayout(copyLayout);
    auto* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(m_importImageButton);
    fileLayout->addWidget(m_exportButton);
    pageLayout->addLayout(fileLayout);
    pageLayout->addStretch(1);

    connect(m_assignButton, &QToolButton::clicked, this, [this]() {
        if (onAssignRequested) {
            onAssignRequested();
        }
    });
    connect(m_removeButton, &QToolButton::clicked, this, [this]() {
        if (onRemoveRequested) {
            onRemoveRequested();
        }
    });
    connect(m_editButton, &QToolButton::clicked, this, [this](bool checked) {
        if (onEditRequested) {
            onEditRequested(checked);
        }
    });
    connect(m_copyFrameToBackgroundButton, &QToolButton::clicked, this, [this]() {
        if (onCopyFrameToBackgroundRequested) {
            onCopyFrameToBackgroundRequested();
        }
    });
    connect(m_copyBackgroundToFramesButton, &QToolButton::clicked, this, [this]() {
        if (onCopyBackgroundToFramesRequested) {
            onCopyBackgroundToFramesRequested();
        }
    });
    connect(m_importImageButton, &QToolButton::clicked, this, [this]() {
        if (onImportImageRequested) {
            onImportImageRequested();
        }
    });
    connect(m_exportButton, &QToolButton::clicked, this, [this]() {
        if (onExportRequested) {
            onExportRequested();
        }
    });
}

void BackgroundPanel::refresh(const BackgroundPanelState& state)
{
    const document::ProjectDocument* document = state.document;
    const bool hasBackground = document && state.selectedBackgroundIndex &&
                               *state.selectedBackgroundIndex < document->backgroundCount();
    m_assignButton->setEnabled(hasBackground && state.hasSelectedFrames);
    m_removeButton->setEnabled(document && state.selectedFramesHaveBackground);
    m_copyFrameToBackgroundButton->setEnabled(hasBackground && state.hasCurrentFrame);
    m_copyBackgroundToFramesButton->setEnabled(hasBackground && state.hasSelectedFrames);
    m_importImageButton->setEnabled(hasBackground);
    m_exportButton->setEnabled(hasBackground);
    m_editButton->setEnabled(hasBackground || state.editing);
    {
        const QSignalBlocker blocker(m_editButton);
        m_editButton->setChecked(state.editing);
    }

    if (!hasBackground) {
        m_selectedBackgroundLabel->setText(tr("Selected: None"));
        return;
    }

    const std::size_t backgroundIndex = *state.selectedBackgroundIndex;
    const document::BackgroundRecord& background = document->background(backgroundIndex);
    m_selectedBackgroundLabel->setText(tr("Selected: %1%2")
                                           .arg(static_cast<int>(backgroundIndex))
                                           .arg(extraDataSuffix(
                                               *document,
                                               background.colorizedHd.has_value())));
}

}  // namespace colorizing::app
