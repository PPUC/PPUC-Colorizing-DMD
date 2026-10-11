#include "panels/BackgroundPanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>

#include "FrameImageUtils.h"

namespace colorizing::app {

namespace {

QPixmap previewPixmap(const QImage& image, QSize targetSize)
{
    return QPixmap::fromImage(image.scaled(targetSize,
                                           Qt::KeepAspectRatio,
                                           Qt::FastTransformation));
}

}  // namespace

BackgroundPanel::BackgroundPanel(QWidget* parent)
    : QWidget(parent)
{
    m_assignButton = new QToolButton(this);
    m_assignButton->setText(QStringLiteral("Assign BG"));
    m_assignButton->setToolTip(QStringLiteral("Assign selected background to selected frames"));
    m_assignButton->setEnabled(false);
    m_editButton = new QToolButton(this);
    m_editButton->setText(QStringLiteral("Edit BG"));
    m_editButton->setToolTip(QStringLiteral("Switch drawing target to the selected background"));
    m_editButton->setEnabled(false);
    m_copyFrameToBackgroundButton = new QToolButton(this);
    m_copyFrameToBackgroundButton->setText(QStringLiteral("Frame -> BG"));
    m_copyFrameToBackgroundButton->setToolTip(
        QStringLiteral("Copy the current frame image into the selected background"));
    m_copyFrameToBackgroundButton->setEnabled(false);
    m_copyBackgroundToFramesButton = new QToolButton(this);
    m_copyBackgroundToFramesButton->setText(QStringLiteral("BG -> Frame"));
    m_copyBackgroundToFramesButton->setToolTip(
        QStringLiteral("Copy the selected background image into selected frames"));
    m_copyBackgroundToFramesButton->setEnabled(false);
    m_importImageButton = new QToolButton(this);
    m_importImageButton->setText(QStringLiteral("Import Image"));
    m_importImageButton->setToolTip(
        QStringLiteral("Import an image into the selected background"));
    m_importImageButton->setEnabled(false);
    m_exportButton = new QToolButton(this);
    m_exportButton->setText(QStringLiteral("Export BG"));
    m_exportButton->setToolTip(QStringLiteral("Export the selected background as PNG"));
    m_exportButton->setEnabled(false);

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedBackgroundLabel = new QLabel(QStringLiteral("Selected: None"), this);
    m_selectedBackgroundLabel->setWordWrap(true);
    pageLayout->addWidget(m_selectedBackgroundLabel);
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_assignButton);
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
    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(190, 48);
    m_previewLabel->setFrameShape(QFrame::Box);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setToolTip(QStringLiteral("Selected background preview"));
    m_previewLabel->setVisible(false);
    pageLayout->addStretch(1);

    connect(m_assignButton, &QToolButton::clicked, this, [this]() {
        if (onAssignRequested) {
            onAssignRequested();
        }
    });
    connect(m_editButton, &QToolButton::clicked, this, [this]() {
        if (onEditRequested) {
            onEditRequested();
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
    m_copyFrameToBackgroundButton->setEnabled(hasBackground && state.hasCurrentFrame);
    m_copyBackgroundToFramesButton->setEnabled(hasBackground && state.hasSelectedFrames);
    m_importImageButton->setEnabled(hasBackground);
    m_exportButton->setEnabled(hasBackground);
    m_editButton->setEnabled(hasBackground);

    if (!hasBackground) {
        m_selectedBackgroundLabel->setText(QStringLiteral("Selected: None"));
        m_previewLabel->clear();
        m_previewLabel->setText(QStringLiteral("Background"));
        return;
    }

    const std::size_t backgroundIndex = *state.selectedBackgroundIndex;
    const document::BackgroundRecord& background = document->background(backgroundIndex);
    m_selectedBackgroundLabel->setText(QStringLiteral("Selected: %1%2")
                                           .arg(static_cast<int>(backgroundIndex))
                                           .arg(background.colorizedHd ? QStringLiteral(" (HD)")
                                                                       : QString()));
    m_previewLabel->setPixmap(
        previewPixmap(renderBackgroundPreviewImage(background, state.resolution),
                      m_previewLabel->size()));
}

std::vector<QWidget*> BackgroundPanel::tabOrderWidgets() const
{
    return {m_assignButton,
            m_editButton,
            m_copyFrameToBackgroundButton,
            m_copyBackgroundToFramesButton,
            m_importImageButton,
            m_exportButton};
}

}  // namespace colorizing::app
