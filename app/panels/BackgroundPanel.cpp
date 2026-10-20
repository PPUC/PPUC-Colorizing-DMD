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

    m_backToFrameButton = new QToolButton(this);
    m_backToFrameButton->setText(tr("Back to Frame"));
    m_backToFrameButton->setToolTip(tr("End editing the background and show the frame again"));

    // The frame canvas' page: the background of the frames.
    m_framePage = new QWidget(this);
    auto* frameLayout = new QVBoxLayout(m_framePage);
    frameLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedBackgroundLabel = new QLabel(tr("Selected: None"), m_framePage);
    m_selectedBackgroundLabel->setWordWrap(true);
    frameLayout->addWidget(m_selectedBackgroundLabel);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_assignButton);
    buttonLayout->addWidget(m_removeButton);
    frameLayout->addLayout(buttonLayout);
    auto* copyLayout = new QHBoxLayout();
    copyLayout->addWidget(m_copyFrameToBackgroundButton);
    copyLayout->addWidget(m_copyBackgroundToFramesButton);
    frameLayout->addLayout(copyLayout);
    frameLayout->addWidget(m_editButton);
    frameLayout->addStretch(1);

    // The page while the background is edited: the background itself.
    m_recordPage = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(m_recordPage);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_editedBackgroundLabel = new QLabel(tr("Selected: None"), m_recordPage);
    m_editedBackgroundLabel->setWordWrap(true);
    pageLayout->addWidget(m_editedBackgroundLabel);
    auto* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(m_importImageButton);
    fileLayout->addWidget(m_exportButton);
    pageLayout->addLayout(fileLayout);
    m_createHdButton = new QToolButton(this);
    m_createHdButton->setText(tr("Create HD"));
    m_createHdButton->setToolTip(tr("Create the HD image of the background from its SD image"));
    m_removeHdButton = new QToolButton(this);
    m_removeHdButton->setText(tr("Remove HD"));
    m_removeHdButton->setToolTip(tr("Remove the background's HD image"));
    connect(m_createHdButton, &QToolButton::clicked, this, [this]() {
        if (onCreateHdRequested) {
            onCreateHdRequested();
        }
    });
    connect(m_removeHdButton, &QToolButton::clicked, this, [this]() {
        if (onRemoveHdRequested) {
            onRemoveHdRequested();
        }
    });
    auto* hdLayout = new QHBoxLayout();
    hdLayout->addWidget(m_createHdButton);
    hdLayout->addWidget(m_removeHdButton);
    pageLayout->addLayout(hdLayout);
    pageLayout->addWidget(m_backToFrameButton);
    pageLayout->addStretch(1);

    // Both pages, for a panel that is not split into sections (MainWindow moves them).
    auto* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->addWidget(m_framePage);
    panelLayout->addWidget(m_recordPage);
    connect(m_backToFrameButton, &QToolButton::clicked, this, [this]() {
        if (onEditRequested) {
            onEditRequested(false);
        }
    });

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
        const render::PlaneLayout layout =
            document ? document->geometry().layout() : render::PlaneLayout::Ordinary;
        const bool backgroundHasHd =
            hasBackground &&
            document->background(*state.selectedBackgroundIndex).colorizedHd.has_value();
        if (layout == render::PlaneLayout::NativeExtra) {
            const QString size = planeSizeText(render::ProjectGeometry(layout).romFrameSize());
            m_createHdButton->setText(tr("Create %1 Background").arg(size));
            m_createHdButton->setToolTip(tr("Give the background a black %1 image").arg(size));
        }
        m_createHdButton->setVisible(layout != render::PlaneLayout::NativeOriginal);
        m_removeHdButton->setVisible(layout == render::PlaneLayout::Ordinary);
        m_createHdButton->setEnabled(hasBackground && !backgroundHasHd);
        m_removeHdButton->setEnabled(hasBackground && backgroundHasHd);
    }
    {
        const QSignalBlocker blocker(m_editButton);
        m_editButton->setChecked(state.editing);
    }

    if (!hasBackground) {
        m_selectedBackgroundLabel->setText(tr("Selected: None"));
        m_editedBackgroundLabel->setText(tr("Selected: None"));
        return;
    }

    const std::size_t backgroundIndex = *state.selectedBackgroundIndex;
    const document::BackgroundRecord& background = document->background(backgroundIndex);
    m_selectedBackgroundLabel->setText(tr("Selected: %1%2")
                                           .arg(static_cast<int>(backgroundIndex))
                                           .arg(extraDataSuffix(
                                               *document,
                                               background.colorizedHd.has_value())));
    m_editedBackgroundLabel->setText(m_selectedBackgroundLabel->text());
}

}  // namespace colorizing::app
