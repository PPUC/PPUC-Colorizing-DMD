#include "MainWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "legacy/LegacyProjectFileReader.h"

namespace colorizing::app {

namespace {

QToolButton* makeColorButton(QWidget* parent, uint16_t rgb565)
{
    auto* button = new QToolButton(parent);
    button->setCheckable(true);
    button->setFixedSize(28, 24);
    button->setProperty("rgb565", static_cast<int>(rgb565));
    button->setToolTip(QStringLiteral("Draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
    const QColor color = rgb565ToColor(rgb565);
    button->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #666; }"
                                         "QToolButton:checked { border: 3px solid #ffffff; }")
                              .arg(color.name()));
    return button;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("ColorizingDMD Qt Rewrite v2"));
    resize(1000, 700);
    createActions();
    createCentralView();
    setStatusText(QStringLiteral("No project loaded"));
}

void MainWindow::createActions()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));

    QAction* openAction = fileMenu->addAction(QStringLiteral("&Open Project..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() {
        openProject();
    });

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = editMenu->addAction(QStringLiteral("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        undo();
    });

    m_redoAction = editMenu->addAction(QStringLiteral("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        redo();
    });
}

void MainWindow::createCentralView()
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    m_statusLabel = new QLabel(central);
    m_frameStatusLabel = new QLabel(central);
    m_frameList = new QListWidget(central);
    m_frameList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_frameList->setIconSize(QSize(160, 84));
    m_frameList->setResizeMode(QListView::Adjust);
    m_frameList->setUniformItemSizes(false);
    m_frameList->setMinimumWidth(230);
    m_preview = new FramePreviewWidget(central);

    layout->addWidget(m_statusLabel);

    auto* toolbarLayout = new QHBoxLayout();
    m_sdButton = new QToolButton(central);
    m_sdButton->setText(QStringLiteral("SD"));
    m_sdButton->setCheckable(true);
    m_sdButton->setChecked(true);
    m_sdButton->setEnabled(false);
    m_hdButton = new QToolButton(central);
    m_hdButton->setText(QStringLiteral("HD"));
    m_hdButton->setCheckable(true);
    m_hdButton->setEnabled(false);
    toolbarLayout->addWidget(m_sdButton);
    toolbarLayout->addWidget(m_hdButton);
    toolbarLayout->addSpacing(12);
    const std::vector<uint16_t> colors{0xffff, 0xf800, 0x07e0, 0x001f, 0x0000};
    for (uint16_t color : colors) {
        QToolButton* button = makeColorButton(central, color);
        toolbarLayout->addWidget(button);
        m_colorButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this, color]() {
            setActiveDrawColor(color);
        });
    }
    toolbarLayout->addSpacing(12);
    toolbarLayout->addWidget(m_frameStatusLabel);
    toolbarLayout->addStretch(1);
    layout->addLayout(toolbarLayout);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->addWidget(m_frameList, 0);
    contentLayout->addWidget(m_preview, 1);
    layout->addLayout(contentLayout, 1);

    connect(m_frameList, &QListWidget::currentRowChanged, this, [this](int row) {
        selectFrame(row);
    });
    connect(m_sdButton, &QToolButton::clicked, this, [this]() {
        m_activeResolution = render::Resolution::Sd;
        updateResolutionMode();
    });
    connect(m_hdButton, &QToolButton::clicked, this, [this]() {
        m_activeResolution = render::Resolution::Hd;
        updateResolutionMode();
    });
    m_preview->setPixelClickedHandler([this](render::CanvasHit hit) {
        paintFramePixel(hit);
    });
    setActiveDrawColor(m_activeDrawColor);

    setCentralWidget(central);
}

void MainWindow::openProject()
{
    const QString cromPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open cROM"),
        QString(),
        QStringLiteral("Serum colorization (*.cROM *.cRom *.crom);;All files (*)"));
    if (cromPath.isEmpty()) {
        return;
    }

    QFileInfo cromInfo(cromPath);
    const QString defaultCrpPath =
        cromInfo.dir().absoluteFilePath(cromInfo.completeBaseName() + QStringLiteral(".cRP"));

    QString crpPath = defaultCrpPath;
    if (!QFileInfo::exists(crpPath)) {
        crpPath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Open matching cRP"),
            cromInfo.dir().absolutePath(),
            QStringLiteral("Serum editor project (*.cRP *.crp);;All files (*)"));
        if (crpPath.isEmpty()) {
            return;
        }
    }

    loadProject(cromPath, crpPath);
}

void MainWindow::loadProject(const QString& cromPath, const QString& crpPath)
{
    const legacy::LegacyProjectFileReader reader;
    const legacy::LegacyLoadResult result = reader.load(legacy::LegacyProjectPaths{
        cromPath.toStdString(),
        crpPath.toStdString(),
    });

    if (!result.ok) {
        QMessageBox::critical(this,
                              QStringLiteral("Open Project Failed"),
                              QString::fromStdString(result.error.message));
        setStatusText(QStringLiteral("Open failed"));
        return;
    }

    try {
        m_document = document::ProjectDocument::fromSnapshot(result.snapshot);
        m_undoStack.clear();
        m_redoStack.clear();
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        QMessageBox::critical(this,
                              QStringLiteral("Open Project Failed"),
                              QString::fromUtf8(ex.what()));
        setStatusText(QStringLiteral("Open failed"));
        return;
    }

    refreshProjectSummary();
}

void MainWindow::setStatusText(const QString& text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
    statusBar()->showMessage(text);
}

void MainWindow::setFrameStatusText(const QString& text)
{
    if (m_frameStatusLabel) {
        m_frameStatusLabel->setText(text);
    }
}

void MainWindow::refreshProjectSummary()
{
    m_frameList->clear();
    m_preview->setDocument(m_document ? &*m_document : nullptr);
    m_activeResolution = render::Resolution::Sd;
    updateResolutionMode();
    if (!m_document) {
        setStatusText(QStringLiteral("No project loaded"));
        setFrameStatusText(QString());
        return;
    }

    const auto frameCount = static_cast<int>(m_document->frameCount());
    int hdCount = 0;
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const bool hasHd = m_document->hasHdFrame(static_cast<std::size_t>(frameIndex));
        if (hasHd) {
            ++hdCount;
        }
        auto* item = new QListWidgetItem(
            makeFrameThumbnail(*m_document, static_cast<std::size_t>(frameIndex), QSize(160, 84)),
            QStringLiteral("Frame %1%2")
                .arg(frameIndex)
                .arg(hasHd ? QStringLiteral(" (HD)") : QString()));
        item->setSizeHint(QSize(210, 96));
        m_frameList->addItem(item);
    }

    if (frameCount > 0) {
        m_frameList->setCurrentRow(0);
    }

    (void)hdCount;
    updateProjectHeaderStatus();
}

void MainWindow::selectFrame(int frameIndex)
{
    if (!m_document || frameIndex < 0 || frameIndex >= static_cast<int>(m_document->frameCount())) {
        setFrameStatusText(QString());
        return;
    }
    const auto index = static_cast<std::size_t>(frameIndex);
    m_preview->setFrameIndex(index);
    const bool hasHd = m_document->hasHdFrame(index);
    setFrameStatusText(QStringLiteral("Frame %1 | SD 128x32 | %2")
                           .arg(frameIndex)
                           .arg(hasHd ? QStringLiteral("HD 256x64")
                                      : QStringLiteral("HD unavailable")));
}

void MainWindow::updateResolutionMode()
{
    const bool hasDocument = m_document.has_value();
    m_sdButton->setEnabled(hasDocument);
    m_hdButton->setEnabled(hasDocument);
    m_sdButton->setChecked(m_activeResolution == render::Resolution::Sd);
    m_hdButton->setChecked(m_activeResolution == render::Resolution::Hd);
    if (m_preview) {
        m_preview->setActiveResolution(m_activeResolution);
    }
}

void MainWindow::setActiveDrawColor(uint16_t rgb565)
{
    m_activeDrawColor = rgb565;
    for (QToolButton* button : m_colorButtons) {
        QSignalBlocker blocker(button);
        button->setChecked(button->property("rgb565").toInt() == static_cast<int>(rgb565));
    }
}

void MainWindow::paintFramePixel(const render::CanvasHit& hit)
{
    if (!m_document || m_frameList->currentRow() < 0) {
        return;
    }
    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    try {
        const uint16_t oldColor =
            m_document->colorizedPixel(frameIndex, hit.resolution, hit.x, hit.y);
        if (oldColor == m_activeDrawColor) {
            return;
        }
        const PointEditCommand command{
            frameIndex,
            hit.resolution,
            hit.x,
            hit.y,
            oldColor,
            m_activeDrawColor,
        };
        applyPointEdit(command, true);
        m_undoStack.push_back(command);
        m_redoStack.clear();
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(QStringLiteral("Painted frame %1 at %2,%3")
                                 .arg(static_cast<int>(frameIndex))
                                 .arg(hit.x)
                                 .arg(hit.y),
                             2500);
}

void MainWindow::applyPointEdit(const PointEditCommand& command, bool useNewColor)
{
    if (!m_document) {
        return;
    }
    m_document->setColorizedPixel(command.frameIndex,
                                  command.resolution,
                                  command.x,
                                  command.y,
                                  useNewColor ? command.newColor : command.oldColor);
    m_preview->update();
    updateFrameThumbnail(command.frameIndex);
    updateProjectHeaderStatus();
}

void MainWindow::undo()
{
    if (m_undoStack.empty()) {
        return;
    }
    const PointEditCommand command = m_undoStack.back();
    m_undoStack.pop_back();
    applyPointEdit(command, false);
    m_redoStack.push_back(command);
    updateUndoRedoActions();
    statusBar()->showMessage(QStringLiteral("Undo"), 1500);
}

void MainWindow::redo()
{
    if (m_redoStack.empty()) {
        return;
    }
    const PointEditCommand command = m_redoStack.back();
    m_redoStack.pop_back();
    applyPointEdit(command, true);
    m_undoStack.push_back(command);
    updateUndoRedoActions();
    statusBar()->showMessage(QStringLiteral("Redo"), 1500);
}

void MainWindow::updateUndoRedoActions()
{
    if (m_undoAction) {
        m_undoAction->setEnabled(!m_undoStack.empty());
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(!m_redoStack.empty());
    }
}

void MainWindow::updateProjectHeaderStatus()
{
    if (!m_document) {
        setStatusText(QStringLiteral("No project loaded"));
        return;
    }
    int hdCount = 0;
    for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
        if (m_document->hasHdFrame(frameIndex)) {
            ++hdCount;
        }
    }
    setStatusText(QStringLiteral("%1: %2 frames, %3 HD%4")
                      .arg(QString::fromStdString(m_document->romName()))
                      .arg(static_cast<int>(m_document->frameCount()))
                      .arg(hdCount)
                      .arg(m_document->isDirty() ? QStringLiteral(" (modified)") : QString()));
}

void MainWindow::updateFrameThumbnail(std::size_t frameIndex)
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return;
    }
    QListWidgetItem* item = m_frameList->item(static_cast<int>(frameIndex));
    if (!item) {
        return;
    }
    item->setIcon(makeFrameThumbnail(*m_document, frameIndex, QSize(160, 84)));
}

}  // namespace colorizing::app
