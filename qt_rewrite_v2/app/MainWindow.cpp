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
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "FramePreviewWidget.h"
#include "FrameThumbnail.h"
#include "legacy/LegacyProjectFileReader.h"

namespace colorizing::app {

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

    QAction* openAction = fileMenu->addAction(QStringLiteral("&Open Project..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() {
        openProject();
    });

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
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

    setStatusText(QStringLiteral("%1: %2 frames, %3 HD")
                      .arg(QString::fromStdString(m_document->romName()))
                      .arg(frameCount)
                      .arg(hdCount));
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

}  // namespace colorizing::app
