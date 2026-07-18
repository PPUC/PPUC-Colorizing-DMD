#include "MainWindow.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <set>

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
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
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"

namespace colorizing::app {

namespace {

constexpr int kMaxRecentProjects = 8;
constexpr char kRecentProjectsSettingsKey[] = "recentProjects";

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

QToolButton* makeToolButton(QWidget* parent,
                            const QString& text,
                            const QString& tooltip,
                            int toolValue)
{
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setCheckable(true);
    button->setProperty("drawingTool", toolValue);
    return button;
}

std::vector<render::Point> makeLinePixels(render::Point start, render::Point end)
{
    std::vector<render::Point> pixels;
    int x0 = start.x;
    int y0 = start.y;
    const int x1 = end.x;
    const int y1 = end.y;
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        pixels.push_back(render::Point{x0, y0});
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y0 += sy;
        }
    }
    return pixels;
}

std::vector<render::Point> makeRectanglePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    std::set<std::pair<int, int>> uniquePixels;

    for (int x = left; x <= right; ++x) {
        uniquePixels.insert({x, top});
        uniquePixels.insert({x, bottom});
    }
    for (int y = top; y <= bottom; ++y) {
        uniquePixels.insert({left, y});
        uniquePixels.insert({right, y});
    }

    std::vector<render::Point> pixels;
    pixels.reserve(uniquePixels.size());
    for (const auto& [x, y] : uniquePixels) {
        pixels.push_back(render::Point{x, y});
    }
    return pixels;
}

std::vector<render::Point> makeFilledRectanglePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    std::vector<render::Point> pixels;
    pixels.reserve(static_cast<std::size_t>(right - left + 1) *
                   static_cast<std::size_t>(bottom - top + 1));

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            pixels.push_back(render::Point{x, y});
        }
    }
    return pixels;
}

std::vector<render::Point> makeEllipsePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    const double centerX = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
    const double centerY = (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
    const double radiusX = std::max(0.5, (static_cast<double>(right - left) + 1.0) / 2.0);
    const double radiusY = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) / 2.0);
    auto insideEllipse = [&](int x, int y) {
        const double normalizedX = (static_cast<double>(x) - centerX) / radiusX;
        const double normalizedY = (static_cast<double>(y) - centerY) / radiusY;
        return normalizedX * normalizedX + normalizedY * normalizedY <= 1.0;
    };
    std::set<std::pair<int, int>> uniquePixels;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (insideEllipse(x, y) &&
                (!insideEllipse(x - 1, y) || !insideEllipse(x + 1, y) ||
                 !insideEllipse(x, y - 1) || !insideEllipse(x, y + 1))) {
                uniquePixels.insert({x, y});
            }
        }
    }

    std::vector<render::Point> pixels;
    pixels.reserve(uniquePixels.size());
    for (const auto& [x, y] : uniquePixels) {
        pixels.push_back(render::Point{x, y});
    }
    return pixels;
}

std::vector<render::Point> makeFilledEllipsePixels(render::Point start, render::Point end)
{
    const int left = std::min(start.x, end.x);
    const int right = std::max(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int bottom = std::max(start.y, end.y);
    const double centerX = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
    const double centerY = (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
    const double radiusX = std::max(0.5, (static_cast<double>(right - left) + 1.0) / 2.0);
    const double radiusY = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) / 2.0);
    std::vector<render::Point> pixels;

    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const double normalizedX = (static_cast<double>(x) - centerX) / radiusX;
            const double normalizedY = (static_cast<double>(y) - centerY) / radiusY;
            if (normalizedX * normalizedX + normalizedY * normalizedY <= 1.0) {
                pixels.push_back(render::Point{x, y});
            }
        }
    }
    return pixels;
}

render::Point squareConstrainedEnd(render::Point start, render::Point end)
{
    const int dx = end.x - start.x;
    const int dy = end.y - start.y;
    const int side = std::min(std::abs(dx), std::abs(dy));
    const int xSign = dx < 0 ? -1 : 1;
    const int ySign = dy < 0 ? -1 : 1;
    return render::Point{start.x + xSign * side, start.y + ySign * side};
}

std::vector<render::Point> makeCirclePixels(render::Point start, render::Point end)
{
    return makeEllipsePixels(start, squareConstrainedEnd(start, end));
}

std::vector<render::Point> makeFilledCirclePixels(render::Point start, render::Point end)
{
    return makeFilledEllipsePixels(start, squareConstrainedEnd(start, end));
}

QString makeRecentProjectEntry(const QString& cromPath, const QString& crpPath)
{
    return cromPath + QChar('\t') + crpPath;
}

QString recentProjectCromPath(const QString& entry)
{
    return entry.section(QChar('\t'), 0, 0);
}

QString recentProjectCrpPath(const QString& entry)
{
    return entry.section(QChar('\t'), 1);
}

}  // namespace

bool MainWindow::EditHistoryKey::operator<(const EditHistoryKey& other) const
{
    if (frameIndex != other.frameIndex) {
        return frameIndex < other.frameIndex;
    }
    return static_cast<int>(resolution) < static_cast<int>(other.resolution);
}

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

    m_saveAction = fileMenu->addAction(QStringLiteral("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, [this]() {
        saveProject();
    });

    m_recentProjectsMenu = fileMenu->addMenu(QStringLiteral("Open &Recent"));
    refreshRecentProjectsMenu();

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
    m_comparisonMaskOverlayButton = new QToolButton(central);
    m_comparisonMaskOverlayButton->setText(QStringLiteral("Mask"));
    m_comparisonMaskOverlayButton->setToolTip(QStringLiteral("Show and edit comparison mask"));
    m_comparisonMaskOverlayButton->setCheckable(true);
    m_comparisonMaskOverlayButton->setEnabled(false);
    m_dynamicMaskOverlayButton = new QToolButton(central);
    m_dynamicMaskOverlayButton->setText(QStringLiteral("Dynamic"));
    m_dynamicMaskOverlayButton->setToolTip(QStringLiteral("Show dynamic mask overlay"));
    m_dynamicMaskOverlayButton->setCheckable(true);
    m_dynamicMaskOverlayButton->setEnabled(false);
    m_comparisonMaskIdSpinBox = new QSpinBox(central);
    m_comparisonMaskIdSpinBox->setToolTip(QStringLiteral("Assigned comparison mask ID"));
    m_comparisonMaskIdSpinBox->setMinimum(-1);
    m_comparisonMaskIdSpinBox->setMaximum(-1);
    m_comparisonMaskIdSpinBox->setSpecialValueText(QStringLiteral("None"));
    m_comparisonMaskIdSpinBox->setEnabled(false);
    m_shapeComparisonModeComboBox = new QComboBox(central);
    m_shapeComparisonModeComboBox->setToolTip(QStringLiteral("Shape comparison mode"));
    m_shapeComparisonModeComboBox->setEnabled(false);
    toolbarLayout->addWidget(m_sdButton);
    toolbarLayout->addWidget(m_hdButton);
    toolbarLayout->addWidget(m_comparisonMaskOverlayButton);
    toolbarLayout->addWidget(m_dynamicMaskOverlayButton);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Mask ID"), central));
    toolbarLayout->addWidget(m_comparisonMaskIdSpinBox);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Shape"), central));
    toolbarLayout->addWidget(m_shapeComparisonModeComboBox);
    toolbarLayout->addSpacing(12);

    const std::vector<std::pair<DrawingTool, QString>> tools{
        {DrawingTool::Point, QStringLiteral("Point")},
        {DrawingTool::Line, QStringLiteral("Line")},
        {DrawingTool::Rectangle, QStringLiteral("Rect")},
        {DrawingTool::FilledRectangle, QStringLiteral("Fill Rect")},
        {DrawingTool::Circle, QStringLiteral("Circle")},
        {DrawingTool::FilledCircle, QStringLiteral("Fill Circle")},
        {DrawingTool::Ellipse, QStringLiteral("Ellipse")},
        {DrawingTool::FilledEllipse, QStringLiteral("Fill Ellipse")},
        {DrawingTool::ColorPicker, QStringLiteral("Pick")},
        {DrawingTool::FloodFill, QStringLiteral("Flood")},
    };
    for (const auto& toolOption : tools) {
        const DrawingTool tool = toolOption.first;
        const QString label = toolOption.second;
        QToolButton* button = makeToolButton(central,
                                             label,
                                             QStringLiteral("%1 tool").arg(label),
                                             static_cast<int>(tool));
        toolbarLayout->addWidget(button);
        m_toolButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this, tool]() {
            setDrawingTool(tool);
        });
    }
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
    connect(m_comparisonMaskOverlayButton, &QToolButton::toggled, this, [this](bool checked) {
        if (checked && m_dynamicMaskOverlayButton) {
            QSignalBlocker blocker(m_dynamicMaskOverlayButton);
            m_dynamicMaskOverlayButton->setChecked(false);
            if (m_preview) {
                m_preview->setDynamicMaskOverlayEnabled(false);
            }
        }
        if (m_preview) {
            m_preview->clearPreviewPixels();
            m_preview->setComparisonMaskOverlayEnabled(checked);
        }
        updateUndoRedoActions();
    });
    connect(m_dynamicMaskOverlayButton, &QToolButton::toggled, this, [this](bool checked) {
        if (checked && m_comparisonMaskOverlayButton) {
            QSignalBlocker blocker(m_comparisonMaskOverlayButton);
            m_comparisonMaskOverlayButton->setChecked(false);
            if (m_preview) {
                m_preview->setComparisonMaskOverlayEnabled(false);
            }
        }
        if (m_preview) {
            m_preview->clearPreviewPixels();
            m_preview->setDynamicMaskOverlayEnabled(checked);
        }
        updateUndoRedoActions();
    });
    connect(m_comparisonMaskIdSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int maskId) {
                setCurrentFrameComparisonMaskId(maskId);
            });
    connect(m_shapeComparisonModeComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                setCurrentFrameShapeComparisonMode(
                    m_shapeComparisonModeComboBox->currentData().toInt());
            });
    m_preview->setPixelClickedHandler([this](render::CanvasHit hit) {
        paintFramePixel(hit);
    });
    m_preview->setOriginalClickedHandler([this](render::CanvasHit hit, bool erase) {
        paintComparisonMaskPixel(hit, erase);
    });
    m_preview->setStrokePreviewHandler(
        [this](render::CanvasHit start, render::CanvasHit end, bool erase) {
            previewFrameStroke(start, end, erase);
    });
    m_preview->setStrokeFinishedHandler(
        [this](render::CanvasHit start, render::CanvasHit end, bool erase) {
            paintFrameStroke(start, end, erase);
    });
    setDrawingTool(m_drawingTool);
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

void MainWindow::openRecentProject(const QString& entry)
{
    const QString cromPath = recentProjectCromPath(entry);
    const QString crpPath = recentProjectCrpPath(entry);
    if (cromPath.isEmpty() || crpPath.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Recent project entry is incomplete"), 4000);
        return;
    }
    loadProject(cromPath, crpPath);
}

void MainWindow::saveProject()
{
    if (!m_document) {
        return;
    }
    if (m_currentCromPath.isEmpty() || m_currentCrpPath.isEmpty()) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project Failed"),
                              QStringLiteral("The current project paths are incomplete."));
        return;
    }

    const legacy::LegacyProjectFileWriter writer;
    const legacy::LegacySaveResult result = writer.save(legacy::LegacyProjectPaths{
        m_currentCromPath.toStdString(),
        m_currentCrpPath.toStdString(),
    }, m_document->toSnapshot());

    if (!result.ok) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project Failed"),
                              QString::fromStdString(result.error.message));
        statusBar()->showMessage(QStringLiteral("Save failed"), 4000);
        return;
    }

    m_document->markClean();
    updateProjectHeaderStatus();
    statusBar()->showMessage(QStringLiteral("Project saved"), 3000);
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
        m_currentCromPath = cromPath;
        m_currentCrpPath = crpPath;
        rememberRecentProject(cromPath, crpPath);
        m_editHistories.clear();
        m_maskEditHistories.clear();
        updateUndoRedoActions();
        if (m_saveAction) {
            m_saveAction->setEnabled(true);
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this,
                              QStringLiteral("Open Project Failed"),
                              QString::fromUtf8(ex.what()));
        setStatusText(QStringLiteral("Open failed"));
        return;
    }

    refreshProjectSummary();
}

void MainWindow::rememberRecentProject(const QString& cromPath, const QString& crpPath)
{
    QSettings settings;
    QStringList entries = settings.value(QString::fromLatin1(kRecentProjectsSettingsKey))
                              .toStringList();
    const QString entry = makeRecentProjectEntry(cromPath, crpPath);
    entries.removeAll(entry);
    entries.prepend(entry);
    while (entries.size() > kMaxRecentProjects) {
        entries.removeLast();
    }
    settings.setValue(QString::fromLatin1(kRecentProjectsSettingsKey), entries);
    refreshRecentProjectsMenu();
}

void MainWindow::refreshRecentProjectsMenu()
{
    if (!m_recentProjectsMenu) {
        return;
    }

    m_recentProjectsMenu->clear();
    QSettings settings;
    const QStringList entries = settings.value(QString::fromLatin1(kRecentProjectsSettingsKey))
                                    .toStringList();
    for (const QString& entry : entries) {
        const QString cromPath = recentProjectCromPath(entry);
        const QString crpPath = recentProjectCrpPath(entry);
        if (cromPath.isEmpty() || crpPath.isEmpty()) {
            continue;
        }
        QAction* action = m_recentProjectsMenu->addAction(cromPath);
        action->setToolTip(cromPath);
        connect(action, &QAction::triggered, this, [this, entry]() {
            openRecentProject(entry);
        });
    }
    if (m_recentProjectsMenu->isEmpty()) {
        QAction* emptyAction = m_recentProjectsMenu->addAction(QStringLiteral("No Recent Projects"));
        emptyAction->setEnabled(false);
    }
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
        if (m_saveAction) {
            m_saveAction->setEnabled(false);
        }
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
        updateFrameMetadataControls();
        return;
    }
    const auto index = static_cast<std::size_t>(frameIndex);
    m_preview->setFrameIndex(index);
    const bool hasHd = m_document->hasHdFrame(index);
    setFrameStatusText(QStringLiteral("Frame %1 | SD 128x32 | %2")
                           .arg(frameIndex)
                           .arg(hasHd ? QStringLiteral("HD 256x64")
                                      : QStringLiteral("HD unavailable")));
    updateFrameMetadataControls();
    updateUndoRedoActions();
}

void MainWindow::updateResolutionMode()
{
    const bool hasDocument = m_document.has_value();
    m_sdButton->setEnabled(hasDocument);
    m_hdButton->setEnabled(hasDocument);
    m_comparisonMaskOverlayButton->setEnabled(hasDocument);
    m_dynamicMaskOverlayButton->setEnabled(hasDocument);
    m_sdButton->setChecked(m_activeResolution == render::Resolution::Sd);
    m_hdButton->setChecked(m_activeResolution == render::Resolution::Hd);
    if (m_preview) {
        m_preview->clearPreviewPixels();
        m_preview->setActiveResolution(m_activeResolution);
    }
    updateUndoRedoActions();
}

void MainWindow::updateFrameMetadataControls()
{
    if (!m_comparisonMaskIdSpinBox || !m_shapeComparisonModeComboBox) {
        return;
    }

    const bool hasFrame = m_document && m_frameList && m_frameList->currentRow() >= 0 &&
                          static_cast<std::size_t>(m_frameList->currentRow()) <
                              m_document->frameCount();
    QSignalBlocker maskBlocker(m_comparisonMaskIdSpinBox);
    QSignalBlocker shapeBlocker(m_shapeComparisonModeComboBox);

    if (!hasFrame) {
        m_comparisonMaskIdSpinBox->setRange(-1, -1);
        m_comparisonMaskIdSpinBox->setValue(-1);
        m_comparisonMaskIdSpinBox->setEnabled(false);
        m_shapeComparisonModeComboBox->clear();
        m_shapeComparisonModeComboBox->addItem(QStringLiteral("Full comparison"), 0);
        m_shapeComparisonModeComboBox->addItem(QStringLiteral("Shape mode"), 1);
        m_shapeComparisonModeComboBox->setCurrentIndex(0);
        m_shapeComparisonModeComboBox->setEnabled(false);
        return;
    }

    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    const int maxMaskId = m_document->comparisonMaskCount() == 0
                              ? -1
                              : static_cast<int>(m_document->comparisonMaskCount() - 1);
    m_comparisonMaskIdSpinBox->setRange(-1, maxMaskId);
    const uint8_t maskId = m_document->comparisonMaskId(frameIndex);
    m_comparisonMaskIdSpinBox->setValue(maskId <= maxMaskId ? static_cast<int>(maskId) : -1);
    m_comparisonMaskIdSpinBox->setEnabled(maxMaskId >= 0);
    const int shapeMode = static_cast<int>(m_document->shapeComparisonMode(frameIndex));
    m_shapeComparisonModeComboBox->clear();
    m_shapeComparisonModeComboBox->addItem(QStringLiteral("Full comparison"), 0);
    m_shapeComparisonModeComboBox->addItem(QStringLiteral("Shape mode"), 1);
    if (shapeMode > 1) {
        m_shapeComparisonModeComboBox->addItem(
            QStringLiteral("Unknown legacy mode %1").arg(shapeMode),
            shapeMode);
    }
    const int shapeIndex = m_shapeComparisonModeComboBox->findData(shapeMode);
    m_shapeComparisonModeComboBox->setCurrentIndex(shapeIndex >= 0 ? shapeIndex : 0);
    m_shapeComparisonModeComboBox->setEnabled(true);
}

void MainWindow::setCurrentFrameComparisonMaskId(int maskId)
{
    if (!m_document || !m_frameList || m_frameList->currentRow() < 0) {
        return;
    }
    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    if (frameIndex >= m_document->frameCount()) {
        return;
    }

    const uint8_t storedMaskId = maskId < 0 ? uint8_t{255} : static_cast<uint8_t>(maskId);
    m_document->setComparisonMaskId(frameIndex, storedMaskId);
    if (m_preview) {
        m_preview->update();
    }
    updateFrameMetadataControls();
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(maskId < 0 ? QStringLiteral("Cleared comparison mask assignment")
                                        : QStringLiteral("Assigned comparison mask %1").arg(maskId),
                             2500);
}

void MainWindow::setCurrentFrameShapeComparisonMode(int mode)
{
    if (!m_document || !m_frameList || m_frameList->currentRow() < 0) {
        return;
    }
    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    if (frameIndex >= m_document->frameCount()) {
        return;
    }

    try {
        m_document->setShapeComparisonMode(frameIndex, static_cast<uint8_t>(mode));
        updateFrameMetadataControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(mode == 0 ? QStringLiteral("Set full comparison mode")
                                           : QStringLiteral("Set shape mode"),
                                 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
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

void MainWindow::setDrawingTool(DrawingTool tool)
{
    m_drawingTool = tool;
    if (m_preview) {
        m_preview->clearPreviewPixels();
    }
    for (QToolButton* button : m_toolButtons) {
        QSignalBlocker blocker(button);
        button->setChecked(button->property("drawingTool").toInt() == static_cast<int>(tool));
    }
}

void MainWindow::paintFramePixel(const render::CanvasHit& hit)
{
    if (isComparisonMaskModeActive()) {
        return;
    }
    if (m_drawingTool == DrawingTool::ColorPicker) {
        pickFrameColor(hit);
        return;
    }
    if (m_drawingTool == DrawingTool::FloodFill) {
        floodFillFrame(hit);
        return;
    }
    if (m_drawingTool != DrawingTool::Point) {
        return;
    }
    commitPixels(hit.resolution,
                 {render::Point{hit.x, hit.y}},
                 QStringLiteral("Painted pixel at %1,%2").arg(hit.x).arg(hit.y));
}

void MainWindow::pickFrameColor(const render::CanvasHit& hit)
{
    if (!m_document || m_frameList->currentRow() < 0) {
        return;
    }
    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    try {
        const uint16_t color = m_document->colorizedPixel(frameIndex,
                                                          hit.resolution,
                                                          hit.x,
                                                          hit.y);
        setActiveDrawColor(color);
        statusBar()->showMessage(QStringLiteral("Picked color 0x%1")
                                     .arg(color, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::floodFillFrame(const render::CanvasHit& hit)
{
    if (!m_document || m_frameList->currentRow() < 0) {
        return;
    }

    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    try {
        const document::Rgb565Frame& frame =
            m_document->colorizedFrame(frameIndex, hit.resolution);
        const uint16_t targetColor =
            m_document->colorizedPixel(frameIndex, hit.resolution, hit.x, hit.y);
        if (targetColor == m_activeDrawColor) {
            return;
        }

        std::vector<uint8_t> visited(frame.pixels.size(), 0);
        std::deque<render::Point> queue;
        std::vector<render::Point> pixels;
        queue.push_back(render::Point{hit.x, hit.y});

        while (!queue.empty()) {
            const render::Point point = queue.front();
            queue.pop_front();
            if (point.x < 0 || point.y < 0 ||
                point.x >= frame.size.width || point.y >= frame.size.height) {
                continue;
            }

            const std::size_t index =
                static_cast<std::size_t>(point.y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(point.x);
            if (visited[index]) {
                continue;
            }
            visited[index] = 1;
            if (frame.pixels[index] != targetColor) {
                continue;
            }

            pixels.push_back(point);
            queue.push_back(render::Point{point.x - 1, point.y});
            queue.push_back(render::Point{point.x + 1, point.y});
            queue.push_back(render::Point{point.x, point.y - 1});
            queue.push_back(render::Point{point.x, point.y + 1});
        }

        commitPixels(hit.resolution, pixels, QStringLiteral("Flood filled area"));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::paintComparisonMaskPixel(const render::CanvasHit& hit, bool erase)
{
    if (!m_document || !isComparisonMaskModeActive()) {
        return;
    }
    if (m_drawingTool == DrawingTool::FloodFill) {
        floodFillComparisonMask(hit, erase);
        return;
    }
    if (m_drawingTool != DrawingTool::Point) {
        return;
    }
    commitMaskPixels({render::Point{hit.x, hit.y}},
                     erase ? uint8_t{0} : uint8_t{1},
                     erase ? QStringLiteral("Erased comparison mask pixel")
                           : QStringLiteral("Painted comparison mask pixel"));
}

void MainWindow::floodFillComparisonMask(const render::CanvasHit& hit, bool erase)
{
    if (!m_document || !isComparisonMaskModeActive()) {
        return;
    }
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        statusBar()->showMessage(QStringLiteral("No comparison mask assigned"), 2500);
        return;
    }

    try {
        const document::IndexedFrame& mask = m_document->comparisonMask(*maskIndex);
        const uint8_t targetValue = m_document->comparisonMaskPixel(*maskIndex, hit.x, hit.y);
        const uint8_t newValue = erase ? uint8_t{0} : uint8_t{1};
        if (targetValue == newValue) {
            return;
        }

        std::vector<uint8_t> visited(mask.pixels.size(), 0);
        std::deque<render::Point> queue;
        std::vector<render::Point> pixels;
        queue.push_back(render::Point{hit.x, hit.y});

        while (!queue.empty()) {
            const render::Point point = queue.front();
            queue.pop_front();
            if (point.x < 0 || point.y < 0 ||
                point.x >= mask.size.width || point.y >= mask.size.height) {
                continue;
            }

            const std::size_t index =
                static_cast<std::size_t>(point.y) * static_cast<std::size_t>(mask.size.width) +
                static_cast<std::size_t>(point.x);
            if (visited[index]) {
                continue;
            }
            visited[index] = 1;
            if (mask.pixels[index] != targetValue) {
                continue;
            }

            pixels.push_back(point);
            queue.push_back(render::Point{point.x - 1, point.y});
            queue.push_back(render::Point{point.x + 1, point.y});
            queue.push_back(render::Point{point.x, point.y - 1});
            queue.push_back(render::Point{point.x, point.y + 1});
        }

        commitMaskPixels(pixels,
                         newValue,
                         erase ? QStringLiteral("Flood erased comparison mask")
                               : QStringLiteral("Flood filled comparison mask"));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::previewFrameStroke(const render::CanvasHit& start,
                                    const render::CanvasHit& end,
                                    bool erase)
{
    if (!m_preview || m_drawingTool == DrawingTool::Point ||
        m_drawingTool == DrawingTool::FloodFill || start.region != end.region ||
        start.resolution != end.resolution) {
        return;
    }
    if (isComparisonMaskModeActive()) {
        if (start.region != render::CanvasRegion::OriginalFrame) {
            return;
        }
        m_preview->setPreviewPixels(render::CanvasRegion::OriginalFrame,
                                    render::Resolution::Sd,
                                    pixelsForStroke(start, end),
                                    erase ? QColor(0, 0, 0, 150)
                                          : QColor(255, 0, 0, 150));
        return;
    }
    if (start.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    QColor previewColor = rgb565ToColor(m_activeDrawColor);
    previewColor.setAlpha(150);
    m_preview->setPreviewPixels(render::CanvasRegion::ColorizedFrame,
                                start.resolution,
                                pixelsForStroke(start, end),
                                previewColor);
}

void MainWindow::paintFrameStroke(const render::CanvasHit& start,
                                  const render::CanvasHit& end,
                                  bool erase)
{
    if (m_drawingTool == DrawingTool::Point || m_drawingTool == DrawingTool::FloodFill ||
        start.region != end.region || start.resolution != end.resolution) {
        return;
    }

    QString statusText;
    if (m_drawingTool == DrawingTool::Line) {
        statusText = QStringLiteral("Painted line");
    } else if (m_drawingTool == DrawingTool::Rectangle) {
        statusText = QStringLiteral("Painted rectangle");
    } else if (m_drawingTool == DrawingTool::FilledRectangle) {
        statusText = QStringLiteral("Painted filled rectangle");
    } else if (m_drawingTool == DrawingTool::Circle) {
        statusText = QStringLiteral("Painted circle");
    } else if (m_drawingTool == DrawingTool::FilledCircle) {
        statusText = QStringLiteral("Painted filled circle");
    } else if (m_drawingTool == DrawingTool::Ellipse) {
        statusText = QStringLiteral("Painted ellipse");
    } else if (m_drawingTool == DrawingTool::FilledEllipse) {
        statusText = QStringLiteral("Painted filled ellipse");
    }

    if (isComparisonMaskModeActive()) {
        if (start.region != render::CanvasRegion::OriginalFrame) {
            return;
        }
        commitMaskPixels(pixelsForStroke(start, end),
                         erase ? uint8_t{0} : uint8_t{1},
                         statusText.replace(
                             QStringLiteral("Painted"),
                             erase ? QStringLiteral("Erased comparison mask")
                                   : QStringLiteral("Painted comparison mask")));
        return;
    }

    if (start.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    commitPixels(start.resolution, pixelsForStroke(start, end), statusText);
}

std::vector<render::Point> MainWindow::pixelsForStroke(const render::CanvasHit& start,
                                                       const render::CanvasHit& end) const
{
    if (m_drawingTool == DrawingTool::Line) {
        return makeLinePixels(render::Point{start.x, start.y}, render::Point{end.x, end.y});
    }
    if (m_drawingTool == DrawingTool::Rectangle) {
        return makeRectanglePixels(render::Point{start.x, start.y}, render::Point{end.x, end.y});
    }
    if (m_drawingTool == DrawingTool::FilledRectangle) {
        return makeFilledRectanglePixels(render::Point{start.x, start.y},
                                         render::Point{end.x, end.y});
    }
    if (m_drawingTool == DrawingTool::Circle) {
        return makeCirclePixels(render::Point{start.x, start.y}, render::Point{end.x, end.y});
    }
    if (m_drawingTool == DrawingTool::FilledCircle) {
        return makeFilledCirclePixels(render::Point{start.x, start.y}, render::Point{end.x, end.y});
    }
    if (m_drawingTool == DrawingTool::Ellipse) {
        return makeEllipsePixels(render::Point{start.x, start.y}, render::Point{end.x, end.y});
    }
    if (m_drawingTool == DrawingTool::FilledEllipse) {
        return makeFilledEllipsePixels(render::Point{start.x, start.y},
                                       render::Point{end.x, end.y});
    }
    return {};
}

void MainWindow::commitPixels(render::Resolution resolution,
                              const std::vector<render::Point>& pixels,
                              const QString& statusText)
{
    if (!m_document || m_frameList->currentRow() < 0 || pixels.empty()) {
        return;
    }

    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    PixelEditCommand command;
    command.frameIndex = frameIndex;
    command.resolution = resolution;
    std::set<std::pair<int, int>> seen;

    try {
        for (const render::Point& pixel : pixels) {
            if (!seen.insert({pixel.x, pixel.y}).second) {
                continue;
            }
            const uint16_t oldColor =
                m_document->colorizedPixel(frameIndex, resolution, pixel.x, pixel.y);
            if (oldColor == m_activeDrawColor) {
                continue;
            }
            command.changes.push_back(PixelChange{
                pixel.x,
                pixel.y,
                oldColor,
                m_activeDrawColor,
            });
        }
        if (command.changes.empty()) {
            return;
        }
        applyPixelEdit(command, true);
        EditHistory& history = m_editHistories[EditHistoryKey{frameIndex, resolution}];
        history.undoStack.push_back(command);
        history.redoStack.clear();
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(statusText, 2500);
}

void MainWindow::commitMaskPixels(const std::vector<render::Point>& pixels,
                                  uint8_t value,
                                  const QString& statusText)
{
    if (!m_document || pixels.empty()) {
        return;
    }
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        statusBar()->showMessage(QStringLiteral("No comparison mask assigned"), 2500);
        return;
    }

    MaskEditCommand command;
    command.maskIndex = *maskIndex;
    std::set<std::pair<int, int>> seen;

    try {
        for (const render::Point& pixel : pixels) {
            if (!seen.insert({pixel.x, pixel.y}).second) {
                continue;
            }
            const uint8_t oldValue =
                m_document->comparisonMaskPixel(*maskIndex, pixel.x, pixel.y);
            if (oldValue == value) {
                continue;
            }
            command.changes.push_back(MaskPixelChange{
                pixel.x,
                pixel.y,
                oldValue,
                value,
            });
        }
        if (command.changes.empty()) {
            return;
        }
        applyMaskEdit(command, true);
        MaskEditHistory& history = m_maskEditHistories[*maskIndex];
        history.undoStack.push_back(command);
        history.redoStack.clear();
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(statusText, 2500);
}

void MainWindow::applyPixelEdit(const PixelEditCommand& command, bool useNewColor)
{
    if (!m_document) {
        return;
    }
    for (const PixelChange& change : command.changes) {
        m_document->setColorizedPixel(command.frameIndex,
                                      command.resolution,
                                      change.x,
                                      change.y,
                                      useNewColor ? change.newColor : change.oldColor);
    }
    m_preview->update();
    updateFrameThumbnail(command.frameIndex);
    updateProjectHeaderStatus();
}

void MainWindow::applyMaskEdit(const MaskEditCommand& command, bool useNewValue)
{
    if (!m_document) {
        return;
    }
    for (const MaskPixelChange& change : command.changes) {
        m_document->setComparisonMaskPixel(command.maskIndex,
                                           change.x,
                                           change.y,
                                           useNewValue ? change.newValue : change.oldValue);
    }
    m_preview->update();
    updateProjectHeaderStatus();
}

std::optional<MainWindow::EditHistoryKey> MainWindow::currentEditHistoryKey() const
{
    if (!m_document || !m_frameList || m_frameList->currentRow() < 0) {
        return std::nullopt;
    }
    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    if (frameIndex >= m_document->frameCount()) {
        return std::nullopt;
    }
    return EditHistoryKey{frameIndex, m_activeResolution};
}

MainWindow::EditHistory* MainWindow::currentEditHistory()
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_editHistories.find(*key);
    if (history == m_editHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::EditHistory* MainWindow::currentEditHistory() const
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_editHistories.find(*key);
    if (history == m_editHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

std::optional<std::size_t> MainWindow::currentComparisonMaskIndex() const
{
    if (!m_document || !m_frameList || m_frameList->currentRow() < 0) {
        return std::nullopt;
    }
    const auto frameIndex = static_cast<std::size_t>(m_frameList->currentRow());
    if (frameIndex >= m_document->frameCount()) {
        return std::nullopt;
    }
    const uint8_t maskId = m_document->comparisonMaskId(frameIndex);
    if (maskId >= m_document->comparisonMaskCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(maskId);
}

MainWindow::MaskEditHistory* MainWindow::currentMaskEditHistory()
{
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        return nullptr;
    }
    auto history = m_maskEditHistories.find(*maskIndex);
    if (history == m_maskEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::MaskEditHistory* MainWindow::currentMaskEditHistory() const
{
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        return nullptr;
    }
    auto history = m_maskEditHistories.find(*maskIndex);
    if (history == m_maskEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

bool MainWindow::isComparisonMaskModeActive() const
{
    return m_comparisonMaskOverlayButton && m_comparisonMaskOverlayButton->isChecked();
}

void MainWindow::undo()
{
    if (isComparisonMaskModeActive()) {
        MaskEditHistory* history = currentMaskEditHistory();
        if (!history || history->undoStack.empty()) {
            return;
        }
        const MaskEditCommand command = history->undoStack.back();
        history->undoStack.pop_back();
        applyMaskEdit(command, false);
        history->redoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo mask edit"), 1500);
        return;
    }

    EditHistory* history = currentEditHistory();
    if (!history || history->undoStack.empty()) {
        return;
    }
    const PixelEditCommand command = history->undoStack.back();
    history->undoStack.pop_back();
    applyPixelEdit(command, false);
    history->redoStack.push_back(command);
    updateUndoRedoActions();
    statusBar()->showMessage(QStringLiteral("Undo"), 1500);
}

void MainWindow::redo()
{
    if (isComparisonMaskModeActive()) {
        MaskEditHistory* history = currentMaskEditHistory();
        if (!history || history->redoStack.empty()) {
            return;
        }
        const MaskEditCommand command = history->redoStack.back();
        history->redoStack.pop_back();
        applyMaskEdit(command, true);
        history->undoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo mask edit"), 1500);
        return;
    }

    EditHistory* history = currentEditHistory();
    if (!history || history->redoStack.empty()) {
        return;
    }
    const PixelEditCommand command = history->redoStack.back();
    history->redoStack.pop_back();
    applyPixelEdit(command, true);
    history->undoStack.push_back(command);
    updateUndoRedoActions();
    statusBar()->showMessage(QStringLiteral("Redo"), 1500);
}

void MainWindow::updateUndoRedoActions()
{
    if (isComparisonMaskModeActive()) {
        const MaskEditHistory* history = currentMaskEditHistory();
        if (m_undoAction) {
            m_undoAction->setEnabled(history && !history->undoStack.empty());
        }
        if (m_redoAction) {
            m_redoAction->setEnabled(history && !history->redoStack.empty());
        }
        return;
    }

    const EditHistory* history = currentEditHistory();
    if (m_undoAction) {
        m_undoAction->setEnabled(history && !history->undoStack.empty());
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(history && !history->redoStack.empty());
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
