#include "MainWindow.h"

#include <algorithm>
#include <functional>
#include <set>
#include <tuple>

#include <QAbstractItemView>
#include <QAction>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "edit/DrawingGeometry.h"
#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "SpriteEditorWidget.h"
#include "panels/BackgroundPanel.h"
#include "panels/ColorRotationPanel.h"
#include "panels/DynamicPanel.h"
#include "panels/FramePanel.h"
#include "panels/MaskPanel.h"
#include "panels/PalettePanel.h"
#include "panels/SpritePanel.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"

namespace colorizing::app {

namespace {

constexpr int kMaxRecentProjects = 8;
constexpr char kRecentProjectsSettingsKey[] = "recentProjects";
constexpr int kExportDmdPixelScale = 8;
constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";

QString favoriteColorButtonStyle(uint16_t rgb565)
{
    const QColor color = rgb565ToColor(rgb565);
    return QStringLiteral("QToolButton { background: %1; border: 1px solid #666; }"
                          "QToolButton:checked { border: 3px solid #ffffff; }")
        .arg(color.name());
}

class FavoriteColorButton final : public QToolButton {
public:
    explicit FavoriteColorButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setAcceptDrops(true);
        setCheckable(true);
        setFixedSize(28, 24);
    }

    void setDropHandler(std::function<void(uint16_t)> handler)
    {
        m_dropHandler = std::move(handler);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(kRgb565MimeType)) {
            event->acceptProposedAction();
            return;
        }
        QToolButton::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        bool ok = false;
        const int value =
            QString::fromLatin1(event->mimeData()->data(kRgb565MimeType)).toInt(&ok);
        if (ok && value >= 0 && value <= 0xffff && m_dropHandler) {
            m_dropHandler(static_cast<uint16_t>(value));
            event->acceptProposedAction();
            return;
        }
        QToolButton::dropEvent(event);
    }

private:
    std::function<void(uint16_t)> m_dropHandler;
};

QToolButton* makeColorButton(QWidget* parent, uint16_t rgb565)
{
    auto* button = new FavoriteColorButton(parent);
    button->setProperty("rgb565", static_cast<int>(rgb565));
    button->setToolTip(QStringLiteral("Draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
    button->setStyleSheet(favoriteColorButtonStyle(rgb565));
    return button;
}

QString colorButtonStyle(uint16_t rgb565, bool enabled = true)
{
    const QColor color = rgb565ToColor(rgb565);
    const QString border = enabled ? QStringLiteral("#666") : QStringLiteral("#333");
    return QStringLiteral("QToolButton { background: %1; border: 1px solid %2; }"
                          "QToolButton:disabled { color: #777; }")
        .arg(color.name(), border);
}

uint16_t colorToRgb565(const QColor& color)
{
    const uint16_t r = static_cast<uint16_t>((color.red() * 31) / 255);
    const uint16_t g = static_cast<uint16_t>((color.green() * 63) / 255);
    const uint16_t b = static_cast<uint16_t>((color.blue() * 31) / 255);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

document::Rgb565Frame rgb565FrameFromImage(const QImage& source, render::Size targetSize)
{
    QImage scaled = source.convertToFormat(QImage::Format_RGB32)
                        .scaled(targetSize.width,
                                targetSize.height,
                                Qt::IgnoreAspectRatio,
                                Qt::FastTransformation);
    document::Rgb565Frame frame = document::makeRgb565Frame(targetSize);
    for (int y = 0; y < targetSize.height; ++y) {
        for (int x = 0; x < targetSize.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(targetSize.width) +
                static_cast<std::size_t>(x);
            frame.pixels[index] = colorToRgb565(scaled.pixelColor(x, y));
        }
    }
    return frame;
}

QImage renderMaskPreviewImage(const document::IndexedFrame& mask)
{
    QImage image(mask.size.width, mask.size.height, QImage::Format_ARGB32);
    image.fill(Qt::black);
    for (int y = 0; y < mask.size.height; ++y) {
        for (int x = 0; x < mask.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.size.width) +
                static_cast<std::size_t>(x);
            const uint8_t value = mask.pixels[index];
            image.setPixelColor(x, y, value == 0 ? QColor(32, 32, 32)
                                                  : QColor(0, 220, 220));
        }
    }
    return image;
}

QPixmap previewPixmap(const QImage& image, QSize targetSize);

QIcon previewIcon(const QImage& image, QSize targetSize)
{
    return QIcon(previewPixmap(image, targetSize));
}

QPixmap previewPixmap(const QImage& image, QSize targetSize)
{
    return QPixmap::fromImage(image.scaled(targetSize,
                                           Qt::KeepAspectRatio,
                                           Qt::FastTransformation));
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

QString editKindStatusSuffix(edit::EditKind kind)
{
    switch (kind) {
        case edit::EditKind::FramePixels:
            break;
        case edit::EditKind::ComparisonMask:
            return QStringLiteral(" mask edit");
        case edit::EditKind::DynamicMask:
            return QStringLiteral(" dynamic mask edit");
        case edit::EditKind::DynamicColor:
            return QStringLiteral(" dynamic color edit");
        case edit::EditKind::DynamicShadow:
            return QStringLiteral(" dynamic shadow edit");
        case edit::EditKind::BackgroundMask:
            return QStringLiteral(" background mask edit");
        case edit::EditKind::BackgroundImage:
            return QStringLiteral(" background edit");
        case edit::EditKind::SpriteImage:
            return QStringLiteral(" sprite edit");
        case edit::EditKind::Palette:
            return QStringLiteral(" palette edit");
    }
    return QString();
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

    m_saveAction = fileMenu->addAction(QStringLiteral("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, [this]() {
        saveProject();
    });

    QAction* saveAsAction = fileMenu->addAction(QStringLiteral("Save &As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        saveProjectAs();
    });

    QAction* exportFrameAction = fileMenu->addAction(QStringLiteral("Export Current &Frame PNG..."));
    connect(exportFrameAction, &QAction::triggered, this, [this]() {
        exportCurrentFramePng();
    });

    QAction* importFrameImageAction =
        fileMenu->addAction(QStringLiteral("Import Image Into Selected &Frames..."));
    connect(importFrameImageAction, &QAction::triggered, this, [this]() {
        importImageIntoSelectedFrames();
    });

    m_recentProjectsMenu = fileMenu->addMenu(QStringLiteral("Open &Recent"));
    refreshRecentProjectsMenu();

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = editMenu->addAction(QStringLiteral("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        undo();
    });

    m_redoAction = editMenu->addAction(QStringLiteral("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);
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
    m_frameFilterComboBox = new QComboBox(central);
    m_frameFilterComboBox->addItem(QStringLiteral("All frames"),
                                   static_cast<int>(FrameFilter::All));
    m_frameFilterComboBox->addItem(QStringLiteral("HD frames"),
                                   static_cast<int>(FrameFilter::Hd));
    m_frameFilterComboBox->addItem(QStringLiteral("With background"),
                                   static_cast<int>(FrameFilter::Background));
    m_frameFilterComboBox->addItem(QStringLiteral("With dynamic"),
                                   static_cast<int>(FrameFilter::Dynamic));
    m_frameFilterComboBox->addItem(QStringLiteral("With sprites"),
                                   static_cast<int>(FrameFilter::Sprites));
    m_frameFilterComboBox->setEnabled(false);
    m_frameList = new QListWidget(central);
    m_frameList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frameList->setIconSize(QSize(160, 84));
    m_frameList->setResizeMode(QListView::Adjust);
    m_frameList->setUniformItemSizes(false);
    m_frameList->setMinimumWidth(230);
    m_maskList = new QListWidget(central);
    m_maskList->setIconSize(QSize(160, 42));
    m_maskList->setResizeMode(QListView::Adjust);
    m_maskList->setUniformItemSizes(false);
    m_maskList->setMinimumWidth(230);
    m_backgroundList = new QListWidget(central);
    m_backgroundList->setIconSize(QSize(160, 42));
    m_backgroundList->setResizeMode(QListView::Adjust);
    m_backgroundList->setUniformItemSizes(false);
    m_backgroundList->setMinimumWidth(230);
    m_spriteList = new QListWidget(central);
    m_spriteList->setIconSize(QSize(160, 42));
    m_spriteList->setResizeMode(QListView::Adjust);
    m_spriteList->setUniformItemSizes(false);
    m_spriteList->setMinimumWidth(230);
    m_preview = new FramePreviewWidget(central);
    m_spriteEditor = new SpriteEditorWidget(central);
    m_canvasStack = new QStackedWidget(central);
    m_canvasStack->addWidget(m_preview);
    m_canvasStack->addWidget(m_spriteEditor);

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
    m_backgroundMaskOverlayButton = new QToolButton(central);
    m_backgroundMaskOverlayButton->setText(QStringLiteral("BG"));
    m_backgroundMaskOverlayButton->setToolTip(QStringLiteral("Show and edit background mask"));
    m_backgroundMaskOverlayButton->setCheckable(true);
    m_backgroundMaskOverlayButton->setEnabled(false);
    m_maskPanel = new MaskPanel(central);
    m_maskPanel->onMaskIdEdited = [this](int maskId) {
        setCurrentFrameComparisonMaskId(maskId);
    };
    m_maskPanel->onShapeModeEdited = [this](int mode) {
        setCurrentFrameShapeComparisonMode(mode);
    };
    m_maskPanel->onAssignRequested = [this]() {
        assignSelectedMaskToFrames();
    };
    m_maskPanel->onEditRequested = [this]() {
        editAssignedMask();
    };
    m_colorRotationPanel = new ColorRotationPanel(central);
    m_colorRotationPanel->onSlotSelectionChanged = [this]() {
        updateColorRotationControls();
    };
    m_colorRotationPanel->onLengthEdited = [this](std::size_t slotIndex, int length) {
        setCurrentColorRotationLength(slotIndex, length);
    };
    m_colorRotationPanel->onDelayEdited = [this](std::size_t slotIndex, int delayMs) {
        setCurrentColorRotationDelayMs(slotIndex, delayMs);
    };
    m_colorRotationPanel->onColorClicked = [this](std::size_t slotIndex, std::size_t colorIndex) {
        editCurrentColorRotationColor(slotIndex, colorIndex);
    };
    m_colorRotationPanel->onClearSlotRequested = [this](std::size_t slotIndex) {
        clearCurrentColorRotationSlot(slotIndex);
    };
    m_palettePanel = new PalettePanel(central);
    m_palettePanel->onPaletteSelectionChanged = [this]() {
        cancelFreePaletteGradientSelection();
        updatePaletteControls();
    };
    m_palettePanel->onGradientSettingsChanged = [this]() {
        cancelFreePaletteGradientSelection();
    };
    m_palettePanel->onNameEdited = [this](const QString& name) {
        setCurrentPaletteName(name);
    };
    m_palettePanel->onColorClicked = [this](std::size_t colorIndex) {
        pickCurrentPaletteColor(colorIndex);
    };
    m_palettePanel->onColorDoubleClicked = [this](std::size_t colorIndex) {
        editCurrentPaletteColor(colorIndex);
    };
    m_palettePanel->onApplyGradientRequested = [this]() {
        applyCurrentPaletteGradient();
    };
    m_palettePanel->onFillSelectionRequested = [this]() {
        fillCanvasSelectionWithPaletteGradient();
    };
    m_palettePanel->onSelectionDirectionClicked = [this]() {
        toggleCanvasSelectionDirectionMode();
    };
    m_palettePanel->onClearSelectionRequested = [this]() {
        clearCanvasSelection();
    };
    m_framePanel = new FramePanel(central);
    m_framePanel->onPaintTargetChanged = [this]() {
        paintTargetChanged();
    };
    m_framePanel->onBackgroundIdEdited = [this](int backgroundId) {
        setCurrentFrameBackgroundId(backgroundId);
    };
    m_framePanel->onTriggerIdEdited = [this](int triggerId) {
        setCurrentFrameTriggerId(triggerId);
    };
    m_framePanel->onDurationEdited = [this](int durationMs) {
        setCurrentFrameDurationMs(durationMs);
    };
    m_framePanel->onSwitchToMonochromeToggled = [this](bool checked) {
        setCurrentFrameSwitchToMonochrome(checked);
    };
    m_framePanel->onDmdRenderOptionsChanged = [this]() {
        updateDmdRenderOptions();
    };
    m_framePanel->onBackgroundLayerToggled = [this](bool checked) {
        if (m_preview) {
            m_preview->setBackgroundLayerEnabled(checked);
        }
    };
    m_framePanel->onImportImageRequested = [this]() {
        importImageIntoSelectedFrames();
    };
    m_framePanel->onCreateHdRequested = [this]() {
        if (isSpriteCanvasActive()) {
            createCurrentSpriteHd();
        } else {
            createCurrentFrameHd();
        }
    };
    m_framePanel->onRemoveHdRequested = [this]() {
        if (isSpriteCanvasActive()) {
            removeCurrentSpriteHd();
        } else {
            removeCurrentFrameHd();
        }
    };
    m_backgroundComboBox = new QComboBox(central);
    m_backgroundComboBox->setToolTip(QStringLiteral("Loaded background"));
    m_backgroundComboBox->setEnabled(false);
    m_backgroundComboBox->setVisible(false);
    m_backgroundPanel = new BackgroundPanel(central);
    m_backgroundPanel->onAssignRequested = [this]() {
        assignSelectedBackgroundToFrames();
    };
    m_backgroundPanel->onEditRequested = [this]() {
        editAssignedBackground();
    };
    m_backgroundPanel->onCopyFrameToBackgroundRequested = [this]() {
        copyCurrentFrameToSelectedBackground();
    };
    m_backgroundPanel->onCopyBackgroundToFramesRequested = [this]() {
        copySelectedBackgroundToFrames();
    };
    m_backgroundPanel->onImportImageRequested = [this]() {
        importImageIntoSelectedBackground();
    };
    m_backgroundPanel->onExportRequested = [this]() {
        exportSelectedBackgroundPng();
    };
    m_spritePanel = new SpritePanel(central);
    m_spritePanel->onNameEdited = [this](const QString& name) {
        setCurrentSpriteName(name);
    };
    m_spritePanel->onSlotSelectionChanged = [this]() {
        updateSpriteControls();
    };
    m_spritePanel->onAssignRequested = [this]() {
        assignSelectedSpriteToCurrentSlot();
    };
    m_spritePanel->onCaptureRequested = [this]() {
        captureSelectedSpriteFromCurrentFrame();
    };
    m_spritePanel->onRemoveRequested = [this]() {
        removeCurrentSpriteSlot();
    };
    m_spritePanel->onCaptureSelectionToggled = [this](bool) {
        updateSpriteCaptureSelectionOverlay();
        updateSpriteControls();
    };
    m_spritePanel->onClearCaptureSelectionRequested = [this]() {
        clearSpriteCaptureSelection();
    };
    m_spritePanel->onBoundsOverlayToggled = [this](bool checked) {
        if (m_preview) {
            m_preview->setSelectedSpriteBoundsOverlayEnabled(checked);
        }
    };
    m_spritePanel->onDetectionAreasToggled = [this](bool) {
        updateSpriteControls();
    };
    m_spritePanel->onSpriteCanvasToggled = [this](bool checked) {
        if (!checked) {
            applyPendingSpriteDependentViews();
        }
        if (m_canvasStack) {
            m_canvasStack->setCurrentWidget(checked && m_spriteEditor
                                                ? static_cast<QWidget*>(m_spriteEditor)
                                                : static_cast<QWidget*>(m_preview));
        }
        updateFrameMetadataControls();
        updateSpriteControls();
        updateUndoRedoActions();
    };
    m_spritePanel->onApplySpriteEditsRequested = [this]() {
        applyPendingSpriteDependentViews();
    };
    m_spritePanel->onSlotBoundsEdited = [this]() {
        setCurrentSpriteSlotBounds();
    };
    m_spritePanel->onDetectionAreaSelectionChanged = [this]() {
        updateSpriteControls();
    };
    m_spritePanel->onDetectionAreaEdited = [this]() {
        setCurrentSpriteDetectionArea();
    };
    m_spritePanel->onClearDetectionAreaRequested = [this]() {
        clearCurrentSpriteDetectionArea();
    };
    toolbarLayout->addWidget(m_sdButton);
    toolbarLayout->addWidget(m_hdButton);
    toolbarLayout->addWidget(m_comparisonMaskOverlayButton);
    toolbarLayout->addWidget(m_dynamicMaskOverlayButton);
    toolbarLayout->addWidget(m_backgroundMaskOverlayButton);
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
        {DrawingTool::PaletteGradient, QStringLiteral("Grad")},
        {DrawingTool::Selection, QStringLiteral("Sel")},
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

    m_favoriteColors = {0xf800, 0x07e0, 0x001f, 0x0000};
    for (std::size_t colorIndex = 0; colorIndex < m_favoriteColors.size(); ++colorIndex) {
        QToolButton* button = makeColorButton(central, m_favoriteColors[colorIndex]);
        toolbarLayout->addWidget(button);
        m_colorButtons.push_back(button);
        if (auto* favoriteButton = dynamic_cast<FavoriteColorButton*>(button)) {
            favoriteButton->setDropHandler([this, colorIndex](uint16_t rgb565) {
                setFavoriteDrawColor(colorIndex, rgb565);
            });
        }
        connect(button, &QToolButton::clicked, this, [this, colorIndex]() {
            if (colorIndex < m_favoriteColors.size()) {
                setActiveDrawColor(m_favoriteColors[colorIndex]);
            }
        });
    }
    m_activeColorButton = new QToolButton(central);
    m_activeColorButton->setFixedSize(34, 24);
    m_activeColorButton->setEnabled(false);
    m_activeColorButton->setToolTip(QStringLiteral("Active draw color"));
    toolbarLayout->addWidget(m_activeColorButton);
    toolbarLayout->addSpacing(12);
    m_playbackButton = new QToolButton(central);
    m_playbackButton->setText(QStringLiteral("Play"));
    m_playbackButton->setToolTip(QStringLiteral("Play visible frames"));
    m_playbackButton->setCheckable(true);
    m_playbackButton->setEnabled(false);
    m_loopPlaybackButton = new QToolButton(central);
    m_loopPlaybackButton->setText(QStringLiteral("Loop"));
    m_loopPlaybackButton->setToolTip(QStringLiteral("Loop playback at the end of the visible frame list"));
    m_loopPlaybackButton->setCheckable(true);
    m_loopPlaybackButton->setChecked(true);
    m_loopPlaybackButton->setEnabled(false);
    m_useProjectTimingCheckBox = new QCheckBox(QStringLiteral("cRP"), central);
    m_useProjectTimingCheckBox->setToolTip(
        QStringLiteral("Use per-frame durations loaded from the .cRP file"));
    m_useProjectTimingCheckBox->setChecked(true);
    m_useProjectTimingCheckBox->setEnabled(false);
    m_playbackIntervalSpinBox = new QSpinBox(central);
    m_playbackIntervalSpinBox->setRange(20, 5000);
    m_playbackIntervalSpinBox->setSingleStep(10);
    m_playbackIntervalSpinBox->setSuffix(QStringLiteral(" ms"));
    m_playbackIntervalSpinBox->setValue(100);
    m_playbackIntervalSpinBox->setToolTip(QStringLiteral("Playback frame interval"));
    m_playbackIntervalSpinBox->setEnabled(false);
    toolbarLayout->addWidget(m_playbackButton);
    toolbarLayout->addWidget(m_loopPlaybackButton);
    toolbarLayout->addWidget(m_useProjectTimingCheckBox);
    toolbarLayout->addWidget(m_playbackIntervalSpinBox);
    toolbarLayout->addSpacing(12);
    toolbarLayout->addWidget(m_frameStatusLabel);
    toolbarLayout->addStretch(1);
    layout->addLayout(toolbarLayout);

    auto* contentLayout = new QHBoxLayout();
    m_leftBrowserTabs = new QTabWidget(central);
    m_leftBrowserTabs->setDocumentMode(true);
    m_leftBrowserTabs->setMinimumWidth(250);
    auto* frameListPanel = new QWidget(m_leftBrowserTabs);
    auto* frameListLayout = new QVBoxLayout(frameListPanel);
    frameListLayout->setContentsMargins(0, 0, 0, 0);
    frameListLayout->addWidget(m_frameFilterComboBox);
    frameListLayout->addWidget(m_frameList, 1);
    m_leftBrowserTabs->addTab(frameListPanel, QStringLiteral("Frames"));
    m_leftBrowserTabs->addTab(m_maskList, QStringLiteral("Masks"));
    m_leftBrowserTabs->addTab(m_backgroundList, QStringLiteral("Backgrounds"));
    m_leftBrowserTabs->addTab(m_spriteList, QStringLiteral("Sprites"));
    auto* inspector = new QWidget(central);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    auto* inspectorTitle = new QLabel(QStringLiteral("Inspector"), inspector);
    inspectorTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    auto* inspectorTabs = new QTabWidget(inspector);
    inspectorTabs->setDocumentMode(true);

    inspectorTabs->addTab(m_framePanel, QStringLiteral("Frame"));

    inspectorTabs->addTab(m_maskPanel, QStringLiteral("Mask"));

    inspectorTabs->addTab(m_backgroundPanel, QStringLiteral("BG"));

    m_dynamicPanel = new DynamicPanel(central);
    m_dynamicPanel->onDynamicSetChanged = [this](int setIndex) {
        dynamicSetChanged(setIndex);
    };
    m_dynamicPanel->onColorClicked = [this](std::size_t originalColorIndex) {
        editDynamicColor(originalColorIndex);
    };
    m_dynamicPanel->onShadowDirectionToggled = [this](uint8_t bit, bool enabled) {
        setDynamicShadowDirectionBit(bit, enabled);
    };
    m_dynamicPanel->onShadowColorClicked = [this]() {
        editDynamicShadowColor();
    };
    inspectorTabs->addTab(m_dynamicPanel, QStringLiteral("Dyn"));

    inspectorTabs->addTab(m_palettePanel, QStringLiteral("Palette"));
    inspectorTabs->addTab(m_colorRotationPanel, QStringLiteral("Rot"));

    inspectorLayout->addWidget(inspectorTitle);
    inspectorLayout->addWidget(inspectorTabs, 1);
    auto* spriteScrollArea = new QScrollArea(inspectorTabs);
    spriteScrollArea->setWidgetResizable(true);
    spriteScrollArea->setFrameShape(QFrame::NoFrame);
    spriteScrollArea->setWidget(m_spritePanel);
    inspectorTabs->insertTab(3, spriteScrollArea, QStringLiteral("Sprite"));
    inspector->setMinimumWidth(210);
    inspector->setMaximumWidth(280);
    contentLayout->addWidget(m_leftBrowserTabs, 0);
    contentLayout->addWidget(m_canvasStack, 1);
    contentLayout->addWidget(inspector, 0);
    layout->addLayout(contentLayout, 1);

    connect(m_frameList, &QListWidget::currentRowChanged, this, [this](int row) {
        selectFrame(row);
    });
    connect(m_frameList, &QListWidget::itemSelectionChanged, this, [this]() {
        selectFrame(m_frameList ? m_frameList->currentRow() : -1);
    });
    connect(m_maskList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item || !m_maskPanel) {
            return;
        }
        const int maskIndex = item->data(Qt::UserRole).toInt();
        if (maskIndex >= 0) {
            const int previousMaskId = m_maskPanel->maskId();
            m_maskPanel->setMaskId(maskIndex);
            if (m_maskPanel->maskId() != previousMaskId) {
                setCurrentFrameComparisonMaskId(m_maskPanel->maskId());
            }
        }
        updateMaskControls();
    });
    connect(m_backgroundList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item || !m_backgroundComboBox) {
            return;
        }
        const int backgroundIndex = item->data(Qt::UserRole).toInt();
        const int comboIndex = m_backgroundComboBox->findData(backgroundIndex);
        if (comboIndex >= 0) {
            m_backgroundComboBox->setCurrentIndex(comboIndex);
        }
    });
    connect(m_spriteList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!item || !m_document) {
            return;
        }
        const int spriteIndex = item->data(Qt::UserRole).toInt();
        if (spriteIndex < 0 ||
            static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount() ||
            spriteIndex == m_selectedSpriteIndex) {
            return;
        }
        m_selectedSpriteIndex = spriteIndex;
        updateSpriteControls();
        updateUndoRedoActions();
    });
    connect(m_frameFilterComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                refreshFrameList(currentFrameIndex());
            });
    connect(m_sdButton, &QToolButton::clicked, this, [this]() {
        cancelFreePaletteGradientSelection();
        clearCanvasSelection();
        m_activeResolution = render::Resolution::Sd;
        updateResolutionMode();
    });
    connect(m_hdButton, &QToolButton::clicked, this, [this]() {
        cancelFreePaletteGradientSelection();
        clearCanvasSelection();
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
        if (checked && m_backgroundMaskOverlayButton) {
            QSignalBlocker blocker(m_backgroundMaskOverlayButton);
            m_backgroundMaskOverlayButton->setChecked(false);
            if (m_preview) {
                m_preview->setBackgroundMaskOverlayEnabled(false);
            }
        }
        if (m_preview) {
            m_preview->clearPreviewPixels();
            m_preview->setDynamicMaskOverlayEnabled(checked);
        }
        updateUndoRedoActions();
    });
    connect(m_backgroundMaskOverlayButton, &QToolButton::toggled, this, [this](bool checked) {
        if (checked && m_dynamicMaskOverlayButton) {
            QSignalBlocker blocker(m_dynamicMaskOverlayButton);
            m_dynamicMaskOverlayButton->setChecked(false);
            if (m_preview) {
                m_preview->setDynamicMaskOverlayEnabled(false);
            }
        }
        if (m_preview) {
            m_preview->clearPreviewPixels();
            m_preview->setBackgroundMaskOverlayEnabled(checked);
        }
        updateUndoRedoActions();
    });
    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::advancePlaybackFrame);
    m_colorRotationPreviewTimer = new QTimer(this);
    m_colorRotationPreviewTimer->setInterval(30);
    connect(m_colorRotationPreviewTimer, &QTimer::timeout, this, [this]() {
        if (m_preview && m_playbackElapsedTimer.isValid()) {
            m_preview->setColorRotationPreviewElapsedMs(
                static_cast<int>(m_playbackElapsedTimer.elapsed()));
        }
    });
    connect(m_playbackButton, &QToolButton::toggled, this, [this](bool checked) {
        setPlaybackRunning(checked);
    });
    connect(m_playbackIntervalSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int intervalMs) {
                if (m_playbackTimer && m_playbackTimer->isActive() &&
                    (!m_useProjectTimingCheckBox || !m_useProjectTimingCheckBox->isChecked())) {
                    m_playbackTimer->setInterval(intervalMs);
                }
            });
    connect(m_useProjectTimingCheckBox, &QCheckBox::toggled, this, [this](bool) {
        if (m_playbackTimer && m_playbackTimer->isActive()) {
            m_playbackTimer->setInterval(playbackIntervalForCurrentFrame());
        }
    });
    connect(m_backgroundComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                cancelFreePaletteGradientSelection();
                clearCanvasSelection();
                updateBackgroundControls();
            });
    m_preview->setPixelClickedHandler([this](render::CanvasHit hit, bool erase) {
        paintFramePixel(hit, erase);
    });
    m_preview->setOriginalClickedHandler([this](render::CanvasHit hit, bool erase) {
        if (isSpriteCaptureSelectionModeActive()) {
            paintSpriteCaptureSelectionPixel(hit, erase);
            return;
        }
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
    m_spriteEditor->setDetectionAreaChangedHandler(
        [this](std::size_t areaIndex, document::SpriteDetectionArea area) {
            if (!m_spritePanel) {
                return;
            }
            m_spritePanel->setSelectedDetectionAreaIndex(areaIndex);
            const int spriteIndex = m_selectedSpriteIndex;
            if (!m_document || spriteIndex < 0 ||
                static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
                updateSpriteControls();
                return;
            }
            try {
                m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                                   areaIndex,
                                                   area);
                if (m_spriteEditor) {
                    m_spriteEditor->update();
                }
                setSpriteDependentViewsDirty();
                updateSpriteControls();
                updateProjectHeaderStatus();
                statusBar()->showMessage(
                    QStringLiteral("Updated sprite %1 detection area %2")
                        .arg(spriteIndex)
                        .arg(static_cast<int>(areaIndex)),
                    1500);
            } catch (const std::exception& ex) {
                updateSpriteControls();
                statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
            }
        });
    m_spriteEditor->setColorizedPixelEditedHandler(
        [this](std::size_t spriteIndex,
               render::Resolution resolution,
               int x,
               int y,
               bool erase) {
            paintSpriteColorizedPixel(spriteIndex, resolution, x, y, erase);
        });
    m_spriteEditor->setColorizedPixelsEditedHandler(
        [this](std::size_t spriteIndex,
               render::Resolution resolution,
               const std::vector<render::Point>& pixels,
               bool erase) {
            paintSpriteColorizedPixels(spriteIndex, resolution, pixels, erase);
        });
    m_spriteEditor->setColorizedPixelPickedHandler(
        [this](std::size_t spriteIndex, render::Resolution resolution, int x, int y) {
            pickSpriteColorizedPixel(spriteIndex, resolution, x, y);
        });
    m_spriteEditor->setColorizedFloodFillHandler(
        [this](std::size_t spriteIndex, render::Resolution resolution, int x, int y) {
            floodFillSpriteColorized(spriteIndex, resolution, x, y);
        });
    setDrawingTool(m_drawingTool);
    setActiveDrawColor(m_activeDrawColor);
    updateDmdRenderOptions();

    setCentralWidget(central);
    restoreInspectorTabOrder();
}

// The inspector controls used to be created interleaved across the pages. This keeps the tab
// order that resulted from it now that each page creates its own controls in one go.
void MainWindow::restoreInspectorTabOrder()
{
    const auto chain = [](const std::vector<QWidget*>& widgets) {
        for (std::size_t index = 1; index < widgets.size(); ++index) {
            QWidget::setTabOrder(widgets[index - 1], widgets[index]);
        }
    };
    // Mask ID, shape mode, Assign, Edit.
    const std::vector<QWidget*> mask = m_maskPanel->tabOrderWidgets();
    // Dynamic set, then the color and shadow buttons.
    const std::vector<QWidget*> dynamic = m_dynamicPanel->tabOrderWidgets();
    // Background ID, trigger ID, duration, Switch mono, Show BG, Import Image, Create HD,
    // Remove HD, drawing target, DMD shape, DMD size.
    const std::vector<QWidget*> frame = m_framePanel->tabOrderWidgets();
    // The six buttons.
    const std::vector<QWidget*> background = m_backgroundPanel->tabOrderWidgets();
    chain({mask[0], dynamic[0]});
    chain({mask[1], frame[0], frame[1], frame[2]});
    chain({m_backgroundComboBox, mask[2], mask[3]});
    chain({background[5], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9], frame[10]});
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
    applyPendingSpriteDependentViews();
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

void MainWindow::saveProjectAs()
{
    if (!m_document) {
        return;
    }
    if (m_currentCromPath.isEmpty() || m_currentCrpPath.isEmpty()) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project As Failed"),
                              QStringLiteral("The current project paths are incomplete."));
        return;
    }

    const QFileInfo currentCromInfo(m_currentCromPath);
    QString targetCromPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save cROM As"),
        currentCromInfo.dir().absoluteFilePath(currentCromInfo.completeBaseName() +
                                               QStringLiteral("_copy.cROM")),
        QStringLiteral("Serum colorization (*.cROM);;All files (*)"));
    if (targetCromPath.isEmpty()) {
        return;
    }
    if (!targetCromPath.endsWith(QStringLiteral(".cROM"), Qt::CaseInsensitive)) {
        targetCromPath += QStringLiteral(".cROM");
    }

    const QFileInfo targetCromInfo(targetCromPath);
    const QString targetCrpPath =
        targetCromInfo.dir().absoluteFilePath(targetCromInfo.completeBaseName() +
                                              QStringLiteral(".cRP"));
    if (targetCromPath == m_currentCromPath) {
        saveProject();
        return;
    }

    const QString oldCromPath = m_currentCromPath;
    const QString oldCrpPath = m_currentCrpPath;
    m_currentCromPath = targetCromPath;
    m_currentCrpPath = targetCrpPath;
    saveProject();
    if (m_document && !m_document->isDirty()) {
        rememberRecentProject(m_currentCromPath, m_currentCrpPath);
        statusBar()->showMessage(QStringLiteral("Project saved as copy"), 3000);
        return;
    }

    m_currentCromPath = oldCromPath;
    m_currentCrpPath = oldCrpPath;
}

void MainWindow::exportCurrentFramePng()
{
    if (!m_document) {
        return;
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!frameIndex) {
        statusBar()->showMessage(QStringLiteral("No frame selected"), 2500);
        return;
    }

    render::Resolution exportResolution = m_activeResolution;
    if (exportResolution == render::Resolution::Hd && !m_document->hasHdFrame(*frameIndex)) {
        exportResolution = render::Resolution::Sd;
    }

    const QString resolutionName =
        exportResolution == render::Resolution::Hd ? QStringLiteral("hd") : QStringLiteral("sd");
    QFileInfo currentCromInfo(m_currentCromPath);
    QString defaultPath = currentCromInfo.dir().absoluteFilePath(
        QStringLiteral("%1_frame_%2_%3.png")
            .arg(currentCromInfo.completeBaseName())
            .arg(static_cast<int>(*frameIndex), 4, 10, QLatin1Char('0'))
            .arg(resolutionName));
    QString targetPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Current Frame PNG"),
        defaultPath,
        QStringLiteral("PNG image (*.png);;All files (*)"));
    if (targetPath.isEmpty()) {
        return;
    }
    if (!targetPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        targetPath += QStringLiteral(".png");
    }

    const bool includeBackgroundLayer =
        m_framePanel && m_framePanel->isBackgroundLayerChecked();
    const QImage image = renderResolvedFrameImage(*m_document,
                                                  *frameIndex,
                                                  exportResolution,
                                                  kExportDmdPixelScale,
                                                  includeBackgroundLayer,
                                                  currentDmdRenderOptions());
    if (!image.save(targetPath, "PNG")) {
        QMessageBox::critical(this,
                              QStringLiteral("Export Failed"),
                              QStringLiteral("Could not write PNG file."));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported frame PNG"), 3000);
}

void MainWindow::importImageIntoSelectedFrames()
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        statusBar()->showMessage(QStringLiteral("Select at least one frame first"), 2500);
        return;
    }

    const QString imagePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Image Into Selected Frames"),
        m_currentCromPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentCromPath).absolutePath(),
        QStringLiteral("Images (*.png *.bmp *.jpg *.jpeg);;All files (*)"));
    if (imagePath.isEmpty()) {
        return;
    }

    QImage image(imagePath);
    if (image.isNull()) {
        QMessageBox::critical(this,
                              QStringLiteral("Import Failed"),
                              QStringLiteral("Could not read image file."));
        return;
    }

    const render::Resolution targetResolution = m_activeResolution;
    const render::Size targetSize = targetResolution == render::Resolution::Hd
                                        ? render::kHdFrameSize
                                        : render::kSdFrameSize;
    const document::Rgb565Frame imported = rgb565FrameFromImage(image, targetSize);

    int updatedFrames = 0;
    int skippedFrames = 0;
    try {
        for (std::size_t frameIndex : frameIndices) {
            if (targetResolution == render::Resolution::Hd &&
                !m_document->hasHdFrame(frameIndex)) {
                ++skippedFrames;
                continue;
            }

            std::vector<edit::PixelChange> changes;
            const document::Rgb565Frame& target =
                m_document->colorizedFrame(frameIndex, targetResolution);
            for (int y = 0; y < imported.size.height; ++y) {
                for (int x = 0; x < imported.size.width; ++x) {
                    const std::size_t pixelIndex =
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(imported.size.width) +
                        static_cast<std::size_t>(x);
                    const uint16_t oldColor = target.pixels[pixelIndex];
                    const uint16_t newColor = imported.pixels[pixelIndex];
                    if (oldColor == newColor) {
                        continue;
                    }
                    changes.push_back(edit::PixelChange{x, y, oldColor, newColor});
                }
            }

            if (changes.empty()) {
                continue;
            }
            commitEdit(std::make_unique<edit::PixelEditCommand>(edit::EditKind::FramePixels,
                                                                frameIndex,
                                                                targetResolution,
                                                                std::move(changes)));
            ++updatedFrames;
        }
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    if (updatedFrames == 0) {
        statusBar()->showMessage(
            skippedFrames > 0
                ? QStringLiteral("No selected HD frames were available for import")
                : QStringLiteral("Selected frames already match the imported image"),
            3000);
        updateUndoRedoActions();
        return;
    }

    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(
        QStringLiteral("Imported image into %1 frame(s)%2")
            .arg(updatedFrames)
            .arg(skippedFrames > 0
                     ? QStringLiteral(" (%1 skipped without HD)").arg(skippedFrames)
                     : QString()),
        3000);
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

    // The canvas, mask tools and sprite capture still assume 128x32 original frames. The core
    // loads and saves 256x64 originals already; opening is held back until the editor follows.
    if (result.snapshot.originalIs256x64) {
        QMessageBox::warning(this,
                             QStringLiteral("Open Project"),
                             QStringLiteral("This project has 256x64 original frames, which the "
                                            "editor cannot display yet."));
        setStatusText(QStringLiteral("Open failed"));
        return;
    }

    try {
        m_document = document::ProjectDocument::fromSnapshot(result.snapshot);
        m_currentCromPath = cromPath;
        m_currentCrpPath = crpPath;
        rememberRecentProject(cromPath, crpPath);
        m_undoHistories.clear();
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
    m_spriteDependentViewsDirty = false;
    if (m_spritePanel) {
        m_spritePanel->setApplySpriteEditsEnabled(false);
    }
    m_preview->setDocument(m_document ? &*m_document : nullptr);
    m_preview->setBackgroundLayerEnabled(m_framePanel &&
                                         m_framePanel->isBackgroundLayerChecked());
    m_preview->setSelectedSpriteBoundsOverlayEnabled(
        m_spritePanel && m_spritePanel->isBoundsOverlayChecked());
    if (m_spriteEditor) {
        m_spriteEditor->setDocument(m_document ? &*m_document : nullptr);
    }
    m_activeResolution = render::Resolution::Sd;
    updateResolutionMode();
    if (!m_document) {
        setPlaybackRunning(false);
        m_frameList->clear();
        refreshComponentBrowsers();
        if (m_frameFilterComboBox) {
            m_frameFilterComboBox->setEnabled(false);
        }
        setStatusText(QStringLiteral("No project loaded"));
        setFrameStatusText(QString());
        if (m_saveAction) {
            m_saveAction->setEnabled(false);
        }
        updateSpriteControls();
        return;
    }

    setPlaybackRunning(false);
    if (m_frameFilterComboBox) {
        m_frameFilterComboBox->setEnabled(true);
    }
    refreshFrameList(0);
    refreshComponentBrowsers();
    updateSpriteControls();
    updatePlaybackControls();
    updateProjectHeaderStatus();
}

void MainWindow::refreshFrameList(std::optional<std::size_t> selectedFrameIndex)
{
    if (!m_frameList) {
        return;
    }
    m_frameList->clear();
    if (!m_document) {
        return;
    }

    int selectedRow = -1;
    for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
        if (!frameMatchesFilter(frameIndex)) {
            continue;
        }
        auto* item = new QListWidgetItem(
            makeFrameThumbnail(*m_document, frameIndex, QSize(160, 84)),
            frameListItemText(frameIndex));
        item->setData(Qt::UserRole, static_cast<qulonglong>(frameIndex));
        item->setSizeHint(QSize(210, 96));
        m_frameList->addItem(item);
        if (selectedFrameIndex && *selectedFrameIndex == frameIndex) {
            selectedRow = m_frameList->count() - 1;
        }
    }

    if (selectedRow >= 0) {
        m_frameList->setCurrentRow(selectedRow);
    } else if (m_frameList->count() > 0) {
        m_frameList->setCurrentRow(0);
    } else {
        selectFrame(-1);
    }
    updatePlaybackControls();
}

QString MainWindow::frameListItemText(std::size_t frameIndex) const
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return QString();
    }

    QStringList details;
    details << QStringLiteral("%1 ms").arg(m_document->frameDurationMs(frameIndex));
    if (m_document->hasHdFrame(frameIndex)) {
        details << QStringLiteral("HD");
    }
    if (m_document->dynamicData(frameIndex)) {
        details << QStringLiteral("Dyn");
    }
    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(frameIndex);
    if (backgroundData && backgroundData->backgroundId < m_document->backgroundCount()) {
        details << QStringLiteral("BG %1")
                       .arg(static_cast<int>(backgroundData->backgroundId));
    }
    const std::vector<document::FrameSpriteReference>& spriteReferences =
        m_document->frameSpriteReferences(frameIndex);
    const int spriteCount = static_cast<int>(
        std::count_if(spriteReferences.begin(),
                      spriteReferences.end(),
                      [](const document::FrameSpriteReference& reference) {
                          return reference.spriteId != 255;
                      }));
    if (spriteCount > 0) {
        details << QStringLiteral("%1 sprites").arg(spriteCount);
    }
    return QStringLiteral("%1\n%2")
        .arg(static_cast<int>(frameIndex))
        .arg(details.join(QStringLiteral("\n")));
}

void MainWindow::refreshComponentBrowsers()
{
    constexpr QSize iconSize{160, 42};
    if (m_maskList) {
        QSignalBlocker blocker(m_maskList);
        m_maskList->clear();
        if (m_document) {
            for (std::size_t maskIndex = 0; maskIndex < m_document->comparisonMaskCount();
                 ++maskIndex) {
                auto* item = new QListWidgetItem(
                    previewIcon(renderMaskPreviewImage(m_document->comparisonMask(maskIndex)),
                                iconSize),
                    QString::number(static_cast<int>(maskIndex)));
                item->setData(Qt::UserRole, static_cast<int>(maskIndex));
                item->setSizeHint(QSize(210, 54));
                m_maskList->addItem(item);
            }
        }
    }

    if (m_backgroundList) {
        QSignalBlocker blocker(m_backgroundList);
        m_backgroundList->clear();
        if (m_document) {
            for (std::size_t backgroundIndex = 0;
                 backgroundIndex < m_document->backgroundCount();
                 ++backgroundIndex) {
                const document::BackgroundRecord& background =
                    m_document->background(backgroundIndex);
                auto* item = new QListWidgetItem(
                    previewIcon(renderBackgroundPreviewImage(background, m_activeResolution),
                                iconSize),
                    QStringLiteral("%1%2")
                        .arg(static_cast<int>(backgroundIndex))
                        .arg(background.colorizedHd ? QStringLiteral(" (HD)") : QString()));
                item->setData(Qt::UserRole, static_cast<int>(backgroundIndex));
                item->setSizeHint(QSize(210, 54));
                m_backgroundList->addItem(item);
            }
        }
    }

    if (m_spriteList) {
        QSignalBlocker blocker(m_spriteList);
        m_spriteList->clear();
        if (m_document) {
            for (std::size_t spriteIndex = 0; spriteIndex < m_document->spriteCount();
                 ++spriteIndex) {
                const document::SpriteRecord& sprite = m_document->sprite(spriteIndex);
                const QString spriteLabel =
                    sprite.name.empty()
                        ? QString::number(static_cast<int>(spriteIndex))
                        : QStringLiteral("%1 - %2")
                              .arg(static_cast<int>(spriteIndex))
                              .arg(QString::fromStdString(sprite.name));
                auto* item = new QListWidgetItem(
                    previewIcon(renderSpriteColorizedPreviewImage(sprite, m_activeResolution),
                                iconSize),
                    QStringLiteral("%1%2")
                        .arg(spriteLabel)
                        .arg(sprite.hasHd ? QStringLiteral(" (HD)") : QString()));
                item->setData(Qt::UserRole, static_cast<int>(spriteIndex));
                item->setSizeHint(QSize(210, 54));
                m_spriteList->addItem(item);
            }
        }
    }

    if (m_palettePanel) {
        m_palettePanel->setPalettes(m_document ? &*m_document : nullptr);
    }

    updateMaskBrowserSelection();
    updateBackgroundBrowserSelection();
    updateSpriteBrowserSelection();
    updatePaletteBrowserSelection();
    updateMaskControls();
    updatePaletteControls();
}

void MainWindow::updateMaskBrowserSelection()
{
    if (!m_maskList || !m_document) {
        return;
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    QSignalBlocker blocker(m_maskList);
    if (!frameIndex) {
        m_maskList->setCurrentItem(nullptr);
        return;
    }
    const uint8_t maskId = m_document->comparisonMaskId(*frameIndex);
    for (int row = 0; row < m_maskList->count(); ++row) {
        QListWidgetItem* item = m_maskList->item(row);
        if (item && item->data(Qt::UserRole).toInt() == static_cast<int>(maskId)) {
            m_maskList->setCurrentItem(item);
            return;
        }
    }
    m_maskList->setCurrentItem(nullptr);
}

void MainWindow::updateBackgroundBrowserSelection()
{
    if (!m_backgroundList || !m_document) {
        return;
    }
    std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        backgroundIndex = currentBackgroundIndex();
    }
    QSignalBlocker blocker(m_backgroundList);
    if (!backgroundIndex) {
        m_backgroundList->setCurrentItem(nullptr);
        return;
    }
    for (int row = 0; row < m_backgroundList->count(); ++row) {
        QListWidgetItem* item = m_backgroundList->item(row);
        if (item && item->data(Qt::UserRole).toInt() == static_cast<int>(*backgroundIndex)) {
            m_backgroundList->setCurrentItem(item);
            return;
        }
    }
    m_backgroundList->setCurrentItem(nullptr);
}

void MainWindow::updateSpriteBrowserSelection()
{
    if (!m_spriteList || !m_document) {
        return;
    }
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    QSignalBlocker blocker(m_spriteList);
    if (!spriteIndex) {
        m_spriteList->setCurrentItem(nullptr);
        return;
    }
    for (int row = 0; row < m_spriteList->count(); ++row) {
        QListWidgetItem* item = m_spriteList->item(row);
        if (item && item->data(Qt::UserRole).toInt() == static_cast<int>(*spriteIndex)) {
            m_spriteList->setCurrentItem(item);
            return;
        }
    }
    m_spriteList->setCurrentItem(nullptr);
}

void MainWindow::updatePaletteBrowserSelection()
{
    if (m_palettePanel) {
        m_palettePanel->ensurePaletteSelected(m_document ? &*m_document : nullptr);
    }
}

bool MainWindow::frameMatchesFilter(std::size_t frameIndex) const
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return false;
    }
    const FrameFilter filter =
        m_frameFilterComboBox
            ? static_cast<FrameFilter>(m_frameFilterComboBox->currentData().toInt())
            : FrameFilter::All;
    if (filter == FrameFilter::Hd) {
        return m_document->hasHdFrame(frameIndex);
    }
    if (filter == FrameFilter::Background) {
        const std::optional<document::FrameBackgroundData>& backgroundData =
            m_document->backgroundData(frameIndex);
        return backgroundData && backgroundData->backgroundId < m_document->backgroundCount();
    }
    if (filter == FrameFilter::Dynamic) {
        return m_document->dynamicData(frameIndex).has_value();
    }
    if (filter == FrameFilter::Sprites) {
        const std::vector<document::FrameSpriteReference>& references =
            m_document->frameSpriteReferences(frameIndex);
        return std::any_of(references.begin(), references.end(),
                           [](const document::FrameSpriteReference& reference) {
                               return reference.spriteId != 255;
                           });
    }
    return true;
}

std::optional<std::size_t> MainWindow::currentFrameIndex() const
{
    if (!m_document || !m_frameList || m_frameList->currentRow() < 0) {
        return std::nullopt;
    }
    QListWidgetItem* item = m_frameList->currentItem();
    if (!item) {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong frameIndex = item->data(Qt::UserRole).toULongLong(&ok);
    if (!ok || frameIndex >= m_document->frameCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(frameIndex);
}

std::vector<std::size_t> MainWindow::selectedFrameIndices() const
{
    std::vector<std::size_t> frameIndices;
    if (!m_document || !m_frameList) {
        return frameIndices;
    }

    for (QListWidgetItem* item : m_frameList->selectedItems()) {
        if (!item) {
            continue;
        }
        bool ok = false;
        const qulonglong frameIndex = item->data(Qt::UserRole).toULongLong(&ok);
        if (ok && frameIndex < m_document->frameCount()) {
            frameIndices.push_back(static_cast<std::size_t>(frameIndex));
        }
    }
    if (frameIndices.empty()) {
        const std::optional<std::size_t> frameIndex = currentFrameIndex();
        if (frameIndex) {
            frameIndices.push_back(*frameIndex);
        }
    }
    std::sort(frameIndices.begin(), frameIndices.end());
    frameIndices.erase(std::unique(frameIndices.begin(), frameIndices.end()), frameIndices.end());
    return frameIndices;
}

void MainWindow::selectFrame(int)
{
    cancelFreePaletteGradientSelection();
    clearCanvasSelection();
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        setFrameStatusText(QString());
        clearSpriteCaptureSelection();
        updateFrameMetadataControls();
        updateSpriteControls();
        updatePlaybackControls();
        return;
    }
    if (m_spriteCaptureSelectionFrameIndex &&
        *m_spriteCaptureSelectionFrameIndex != *frameIndex) {
        clearSpriteCaptureSelection();
    }
    m_preview->setFrameIndex(*frameIndex);
    const bool hasHd = m_document->hasHdFrame(*frameIndex);
    const int selectedCount = static_cast<int>(selectedFrameIndices().size());
    const uint32_t durationMs = m_document->frameDurationMs(*frameIndex);
    setFrameStatusText(QStringLiteral("Frame %1 | SD 128x32 | %2 | %3 ms%4")
                           .arg(static_cast<int>(*frameIndex))
                           .arg(hasHd ? QStringLiteral("HD 256x64")
                                      : QStringLiteral("HD unavailable"))
                           .arg(durationMs)
                           .arg(selectedCount > 1
                                    ? QStringLiteral(" | %1 selected").arg(selectedCount)
                                    : QString()));
    updateFrameMetadataControls();
    updateColorRotationControls();
    updateSpriteControls();
    updatePaintTargetMode();
    updateUndoRedoActions();
    updateSpriteCaptureSelectionOverlay();
    updatePlaybackControls();
}

void MainWindow::updatePlaybackControls()
{
    const bool hasFrames = m_document && m_frameList && m_frameList->count() > 0;
    const bool running = m_playbackTimer && m_playbackTimer->isActive();
    if (m_playbackButton) {
        QSignalBlocker blocker(m_playbackButton);
        m_playbackButton->setEnabled(hasFrames);
        m_playbackButton->setChecked(running);
        m_playbackButton->setText(running ? QStringLiteral("Pause") : QStringLiteral("Play"));
    }
    if (m_loopPlaybackButton) {
        m_loopPlaybackButton->setEnabled(hasFrames);
    }
    if (m_playbackIntervalSpinBox) {
        m_playbackIntervalSpinBox->setEnabled(hasFrames);
    }
    if (m_useProjectTimingCheckBox) {
        m_useProjectTimingCheckBox->setEnabled(hasFrames);
    }
    if (!hasFrames && running) {
        setPlaybackRunning(false);
    }
}

void MainWindow::setPlaybackRunning(bool running)
{
    if (!m_playbackTimer || !m_playbackButton) {
        return;
    }
    const bool canPlay = m_document && m_frameList && m_frameList->count() > 0;
    if (!running || !canPlay) {
        m_playbackTimer->stop();
        if (m_colorRotationPreviewTimer) {
            m_colorRotationPreviewTimer->stop();
        }
        if (m_preview) {
            m_preview->setColorRotationPreviewElapsedMs(std::nullopt);
        }
        updatePlaybackControls();
        return;
    }

    applyPendingSpriteDependentViews();
    if (m_spritePanel) {
        m_spritePanel->setSpriteCanvasChecked(false);
    }
    if (m_canvasStack && m_preview) {
        m_canvasStack->setCurrentWidget(m_preview);
    }
    m_playbackElapsedTimer.restart();
    if (m_preview) {
        m_preview->setColorRotationPreviewElapsedMs(0);
    }
    m_playbackTimer->start(playbackIntervalForCurrentFrame());
    if (m_colorRotationPreviewTimer) {
        m_colorRotationPreviewTimer->start();
    }
    updatePlaybackControls();
}

void MainWindow::advancePlaybackFrame()
{
    if (!m_document || !m_frameList || m_frameList->count() == 0) {
        setPlaybackRunning(false);
        return;
    }
    int nextRow = m_frameList->currentRow() + 1;
    if (nextRow >= m_frameList->count()) {
        if (m_loopPlaybackButton && m_loopPlaybackButton->isChecked()) {
            nextRow = 0;
        } else {
            setPlaybackRunning(false);
            return;
        }
    }
    m_frameList->setCurrentRow(nextRow);
    if (m_playbackTimer && m_playbackTimer->isActive()) {
        m_playbackTimer->setInterval(playbackIntervalForCurrentFrame());
    }
}

int MainWindow::playbackIntervalForCurrentFrame() const
{
    const int fallbackMs = m_playbackIntervalSpinBox ? m_playbackIntervalSpinBox->value() : 100;
    if (!m_document || !m_useProjectTimingCheckBox || !m_useProjectTimingCheckBox->isChecked()) {
        return fallbackMs;
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!frameIndex) {
        return fallbackMs;
    }
    const uint32_t durationMs = m_document->frameDurationMs(*frameIndex);
    if (durationMs == 0 || durationMs > 5000) {
        return fallbackMs;
    }
    return static_cast<int>(durationMs);
}

void MainWindow::updateResolutionMode()
{
    const bool hasDocument = m_document.has_value();
    m_sdButton->setEnabled(hasDocument);
    m_hdButton->setEnabled(hasDocument);
    m_comparisonMaskOverlayButton->setEnabled(hasDocument);
    m_dynamicMaskOverlayButton->setEnabled(hasDocument);
    m_backgroundMaskOverlayButton->setEnabled(hasDocument);
    if (m_framePanel) {
        m_framePanel->setImportImageEnabled(hasDocument && !selectedFrameIndices().empty());
    }
    if (m_dynamicPanel) {
        m_dynamicPanel->setDynamicSetEnabled(hasDocument);
    }
    if (m_framePanel) {
        m_framePanel->setPaintTargetEnabled(hasDocument && selectedBackgroundIndex().has_value());
    }
    m_sdButton->setChecked(m_activeResolution == render::Resolution::Sd);
    m_hdButton->setChecked(m_activeResolution == render::Resolution::Hd);
    if (m_preview) {
        m_preview->setSelectedBackgroundIndex(selectedBackgroundIndex());
        m_preview->clearPreviewPixels();
        m_preview->setActiveResolution(m_activeResolution);
    }
    if (m_spriteEditor) {
        m_spriteEditor->setActiveResolution(m_activeResolution);
    }
    updateDynamicColorControls();
    updateColorRotationControls();
    updateSpriteControls();
    updateBackgroundControls();
    refreshComponentBrowsers();
    updateUndoRedoActions();
    updatePaintTargetMode();
}

void MainWindow::updateFrameMetadataControls()
{
    if (!m_maskPanel || !m_framePanel) {
        return;
    }

    const std::optional<std::size_t> currentIndex = currentFrameIndex();
    const bool hasFrame = m_document && currentIndex.has_value();
    m_maskPanel->refreshFrame(m_document ? &*m_document : nullptr, currentIndex);
    FramePanelState state;
    state.document = m_document ? &*m_document : nullptr;
    state.frameIndex = currentIndex;
    state.hasSelectedFrames = !selectedFrameIndices().empty();
    m_framePanel->refresh(state);

    if (!hasFrame) {
        setPaintTarget(PaintTarget::Frame);
        updateDynamicColorControls();
        updateMaskControls();
        updateBackgroundControls();
        return;
    }

    if (m_document->backgroundCount() == 0) {
        setPaintTarget(PaintTarget::Frame);
    }
    updateDynamicColorControls();
    updatePaintTargetMode();
    updateBackgroundControls();
    updateMaskBrowserSelection();
    updateMaskControls();
}

void MainWindow::updateMaskControls()
{
    if (!m_maskPanel) {
        return;
    }

    const QListWidgetItem* selectedItem = m_maskList ? m_maskList->currentItem() : nullptr;
    const int selectedMask = selectedItem ? selectedItem->data(Qt::UserRole).toInt() : -1;
    m_maskPanel->refreshSelection(m_document ? &*m_document : nullptr,
                                  currentFrameIndex(),
                                  selectedMask >= 0
                                      ? std::optional<std::size_t>(
                                            static_cast<std::size_t>(selectedMask))
                                      : std::nullopt,
                                  !selectedFrameIndices().empty());
}

void MainWindow::updateBackgroundControls()
{
    if (!m_backgroundComboBox || !m_backgroundPanel) {
        return;
    }

    const QVariant previousBackgroundData = m_backgroundComboBox->currentData();
    {
        QSignalBlocker blocker(m_backgroundComboBox);
        m_backgroundComboBox->clear();
        if (m_document) {
            for (std::size_t backgroundIndex = 0;
                 backgroundIndex < m_document->backgroundCount();
                 ++backgroundIndex) {
                const document::BackgroundRecord& background =
                    m_document->background(backgroundIndex);
                m_backgroundComboBox->addItem(
                    QStringLiteral("Background %1%2")
                        .arg(static_cast<int>(backgroundIndex))
                        .arg(background.colorizedHd ? QStringLiteral(" (HD)") : QString()),
                    static_cast<int>(backgroundIndex));
            }
        }

        if (m_backgroundComboBox->count() == 0) {
            m_backgroundComboBox->addItem(QStringLiteral("No backgrounds"), -1);
            m_backgroundComboBox->setCurrentIndex(0);
        } else if (previousBackgroundData.isValid()) {
            const int previousIndex = m_backgroundComboBox->findData(previousBackgroundData);
            m_backgroundComboBox->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        } else if (const std::optional<std::size_t> backgroundIndex = currentBackgroundIndex()) {
            const int assignedIndex =
                m_backgroundComboBox->findData(static_cast<int>(*backgroundIndex));
            m_backgroundComboBox->setCurrentIndex(assignedIndex >= 0 ? assignedIndex : 0);
        } else {
            m_backgroundComboBox->setCurrentIndex(0);
        }
    }

    const std::optional<std::size_t> selectedBackground = selectedBackgroundIndex();
    m_backgroundComboBox->setEnabled(m_document && m_document->backgroundCount() > 0);
    BackgroundPanelState state;
    state.document = m_document ? &*m_document : nullptr;
    state.selectedBackgroundIndex = selectedBackground;
    state.resolution = m_activeResolution;
    state.hasCurrentFrame = currentFrameIndex().has_value();
    state.hasSelectedFrames = !selectedFrameIndices().empty();
    m_backgroundPanel->refresh(state);

    if (m_preview) {
        m_preview->setSelectedBackgroundIndex(selectedBackground);
    }
    if (selectedBackground) {
        updateBackgroundBrowserSelection();
    }
}

void MainWindow::setCurrentFrameComparisonMaskId(int maskId)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }

    const uint8_t storedMaskId = maskId < 0 ? uint8_t{255} : static_cast<uint8_t>(maskId);
    m_document->setComparisonMaskId(*frameIndex, storedMaskId);
    if (m_preview) {
        m_preview->update();
    }
    updateFrameMetadataControls();
    updateMaskControls();
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(maskId < 0 ? QStringLiteral("Cleared comparison mask assignment")
                                        : QStringLiteral("Assigned comparison mask %1").arg(maskId),
                             2500);
}

void MainWindow::assignSelectedMaskToFrames()
{
    const QListWidgetItem* selectedItem = m_maskList ? m_maskList->currentItem() : nullptr;
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || !selectedItem || frameIndices.empty()) {
        statusBar()->showMessage(QStringLiteral("No comparison mask selected"), 2500);
        updateMaskControls();
        return;
    }

    const int maskIndex = selectedItem->data(Qt::UserRole).toInt();
    if (maskIndex < 0 ||
        static_cast<std::size_t>(maskIndex) >= m_document->comparisonMaskCount()) {
        statusBar()->showMessage(QStringLiteral("No comparison mask selected"), 2500);
        updateMaskControls();
        return;
    }

    const uint8_t storedMaskId = static_cast<uint8_t>(maskIndex);
    for (std::size_t frameIndex : frameIndices) {
        m_document->setComparisonMaskId(frameIndex, storedMaskId);
        updateFrameListItem(frameIndex);
    }
    if (m_preview) {
        m_preview->update();
    }
    updateFrameMetadataControls();
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(
        QStringLiteral("Assigned comparison mask %1 on %2 frame(s)")
            .arg(maskIndex)
            .arg(static_cast<int>(frameIndices.size())),
        2500);
}

void MainWindow::editAssignedMask()
{
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        statusBar()->showMessage(QStringLiteral("Assign a comparison mask before editing"), 2500);
        updateMaskControls();
        return;
    }
    if (m_comparisonMaskOverlayButton) {
        m_comparisonMaskOverlayButton->setChecked(true);
    }
    if (m_canvasStack && m_preview) {
        m_canvasStack->setCurrentWidget(m_preview);
    }
    updateUndoRedoActions();
    statusBar()->showMessage(
        QStringLiteral("Editing comparison mask %1").arg(static_cast<int>(*maskIndex)),
        1500);
}

void MainWindow::setCurrentFrameShapeComparisonMode(int mode)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }

    try {
        m_document->setShapeComparisonMode(*frameIndex, static_cast<uint8_t>(mode));
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

void MainWindow::setCurrentFrameBackgroundId(int backgroundId)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const uint16_t storedBackgroundId =
        backgroundId < 0 ? uint16_t{0xffff} : static_cast<uint16_t>(backgroundId);
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->setBackgroundId(frameIndex, storedBackgroundId);
            updateFrameListItem(frameIndex);
        }
        if (m_preview) {
            m_preview->update();
        }
        updateFrameMetadataControls();
        updateBackgroundControls();
        updatePaintTargetMode();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            backgroundId < 0
                ? QStringLiteral("Cleared background assignment on %1 frame(s)")
                      .arg(static_cast<int>(frameIndices.size()))
                : QStringLiteral("Assigned background %1 on %2 frame(s)")
                      .arg(backgroundId)
                      .arg(static_cast<int>(frameIndices.size())),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameTriggerId(int triggerId)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }
    if (m_framePanel && m_framePanel->isSwitchToMonochromeChecked()) {
        return;
    }

    const uint32_t storedTriggerId =
        static_cast<uint32_t>(std::clamp(triggerId, 0, kMaxManualTriggerId));
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->setTriggerId(frameIndex, storedTriggerId);
        }
        updateFrameMetadataControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Set trigger ID %1 on %2 frame(s)")
                .arg(storedTriggerId)
                .arg(static_cast<int>(frameIndices.size())),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameDurationMs(int durationMs)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const bool wasDirty = m_document->isDirty();
    const uint32_t storedDurationMs =
        static_cast<uint32_t>(std::clamp(durationMs, 0, kMaxFrameDurationMs));
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->setFrameDurationMs(frameIndex, storedDurationMs);
            updateFrameListItemText(frameIndex);
        }
        updateFrameMetadataControls();
        if (const std::optional<std::size_t> frameIndex = currentFrameIndex()) {
            const bool hasHd = m_document->hasHdFrame(*frameIndex);
            const int selectedCount = static_cast<int>(selectedFrameIndices().size());
            setFrameStatusText(QStringLiteral("Frame %1 | SD 128x32 | %2 | %3 ms%4")
                                   .arg(static_cast<int>(*frameIndex))
                                   .arg(hasHd ? QStringLiteral("HD 256x64")
                                              : QStringLiteral("HD unavailable"))
                                   .arg(m_document->frameDurationMs(*frameIndex))
                                   .arg(selectedCount > 1
                                            ? QStringLiteral(" | %1 selected").arg(selectedCount)
                                            : QString()));
        }
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        if (m_playbackTimer && m_playbackTimer->isActive()) {
            m_playbackTimer->setInterval(playbackIntervalForCurrentFrame());
        }
        statusBar()->showMessage(
            QStringLiteral("Set duration %1 ms on %2 frame(s)")
                .arg(storedDurationMs)
                .arg(static_cast<int>(frameIndices.size())),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameSwitchToMonochrome(bool enabled)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const uint32_t storedTriggerId =
        enabled ? kSwitchToMonochromeTriggerId
                : static_cast<uint32_t>(m_framePanel
                                            ? std::clamp(m_framePanel->triggerId(),
                                                         0,
                                                         kMaxManualTriggerId)
                                            : 0);
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->setTriggerId(frameIndex, storedTriggerId);
        }
        updateFrameMetadataControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            enabled
                ? QStringLiteral("Enabled switch-to-monochrome trigger on %1 frame(s)")
                      .arg(static_cast<int>(frameIndices.size()))
                : QStringLiteral("Disabled switch-to-monochrome trigger on %1 frame(s)")
                      .arg(static_cast<int>(frameIndices.size())),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::assignSelectedBackgroundToFrames()
{
    if (!m_backgroundComboBox) {
        return;
    }
    const int backgroundIndex = m_backgroundComboBox->currentData().toInt();
    if (backgroundIndex < 0) {
        statusBar()->showMessage(QStringLiteral("No background selected"), 2500);
        updateBackgroundControls();
        return;
    }
    setCurrentFrameBackgroundId(backgroundIndex);
}

void MainWindow::editAssignedBackground()
{
    if (!m_framePanel) {
        return;
    }
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        statusBar()->showMessage(QStringLiteral("Select a background before editing"), 2500);
        updateBackgroundControls();
        return;
    }
    if (m_activeResolution == render::Resolution::Hd &&
        !m_document->background(*backgroundIndex).colorizedHd) {
        m_activeResolution = render::Resolution::Sd;
        updateResolutionMode();
    }
    setPaintTarget(PaintTarget::Background);
    if (m_canvasStack && m_preview) {
        m_canvasStack->setCurrentWidget(m_preview);
    }
    updatePaintTargetMode();
    statusBar()->showMessage(
        QStringLiteral("Editing background %1").arg(static_cast<int>(*backgroundIndex)),
        1500);
}

void MainWindow::copyCurrentFrameToSelectedBackground()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !frameIndex || !backgroundIndex) {
        statusBar()->showMessage(QStringLiteral("Select a frame and background first"), 2500);
        updateBackgroundControls();
        return;
    }

    render::Resolution targetResolution = m_activeResolution;
    if (targetResolution == render::Resolution::Hd && !m_document->hasHdFrame(*frameIndex)) {
        targetResolution = render::Resolution::Sd;
    }
    const document::BackgroundRecord& background = m_document->background(*backgroundIndex);
    if (targetResolution == render::Resolution::Hd && !background.colorizedHd) {
        targetResolution = render::Resolution::Sd;
    }

    try {
        const document::Rgb565Frame source =
            composeResolvedFrame(*m_document, *frameIndex, targetResolution, true);
        const document::Rgb565Frame& oldBackground =
            m_document->backgroundFrame(*backgroundIndex, targetResolution);
        std::vector<edit::PixelChange> changes;
        for (int y = 0; y < source.size.height; ++y) {
            for (int x = 0; x < source.size.width; ++x) {
                const std::size_t pixelIndex =
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(source.size.width) +
                    static_cast<std::size_t>(x);
                const uint16_t oldColor = oldBackground.pixels[pixelIndex];
                const uint16_t newColor = source.pixels[pixelIndex];
                if (oldColor == newColor) {
                    continue;
                }
                changes.push_back(edit::PixelChange{x, y, oldColor, newColor});
            }
        }
        if (changes.empty()) {
            statusBar()->showMessage(QStringLiteral("Background already matches frame"), 2500);
            return;
        }
        commitEdit(std::make_unique<edit::PixelEditCommand>(edit::EditKind::BackgroundImage,
                                                            *backgroundIndex,
                                                            targetResolution,
                                                            std::move(changes)));
        refreshComponentBrowsers();
        updateUndoRedoActions();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Copied frame %1 to background %2 (%3)")
                .arg(static_cast<int>(*frameIndex))
                .arg(static_cast<int>(*backgroundIndex))
                .arg(targetResolution == render::Resolution::Hd ? QStringLiteral("HD")
                                                                 : QStringLiteral("SD")),
            2500);
    } catch (const std::exception& ex) {
        updateBackgroundControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::copySelectedBackgroundToFrames()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || !backgroundIndex || frameIndices.empty()) {
        statusBar()->showMessage(QStringLiteral("Select a background and frames first"), 2500);
        updateBackgroundControls();
        return;
    }

    const document::BackgroundRecord& background = m_document->background(*backgroundIndex);
    render::Resolution sourceResolution = m_activeResolution;
    if (sourceResolution == render::Resolution::Hd && !background.colorizedHd) {
        sourceResolution = render::Resolution::Sd;
    }
    const document::Rgb565Frame& source =
        m_document->backgroundFrame(*backgroundIndex, sourceResolution);

    int updatedFrames = 0;
    try {
        for (std::size_t frameIndex : frameIndices) {
            if (sourceResolution == render::Resolution::Hd &&
                !m_document->hasHdFrame(frameIndex)) {
                continue;
            }
            std::vector<edit::PixelChange> changes;
            for (int y = 0; y < source.size.height; ++y) {
                for (int x = 0; x < source.size.width; ++x) {
                    const std::size_t pixelIndex =
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(source.size.width) +
                        static_cast<std::size_t>(x);
                    const uint16_t oldColor =
                        m_document->colorizedPixel(frameIndex, sourceResolution, x, y);
                    const uint16_t newColor = source.pixels[pixelIndex];
                    if (oldColor == newColor) {
                        continue;
                    }
                    changes.push_back(edit::PixelChange{x, y, oldColor, newColor});
                }
            }
            if (changes.empty()) {
                continue;
            }
            commitEdit(std::make_unique<edit::PixelEditCommand>(edit::EditKind::FramePixels,
                                                                frameIndex,
                                                                sourceResolution,
                                                                std::move(changes)));
            ++updatedFrames;
        }
        if (updatedFrames == 0) {
            statusBar()->showMessage(QStringLiteral("No selected frame changed"), 2500);
            return;
        }
        updateUndoRedoActions();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Copied background %1 to %2 frame(s) (%3)")
                .arg(static_cast<int>(*backgroundIndex))
                .arg(updatedFrames)
                .arg(sourceResolution == render::Resolution::Hd ? QStringLiteral("HD")
                                                                 : QStringLiteral("SD")),
            2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::importImageIntoSelectedBackground()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !backgroundIndex) {
        statusBar()->showMessage(QStringLiteral("Select a background first"), 2500);
        updateBackgroundControls();
        return;
    }

    const QString imagePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import Background Image"),
        m_currentCromPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentCromPath).absolutePath(),
        QStringLiteral("Images (*.png *.bmp *.jpg *.jpeg);;All files (*)"));
    if (imagePath.isEmpty()) {
        return;
    }

    QImage image(imagePath);
    if (image.isNull()) {
        QMessageBox::critical(this,
                              QStringLiteral("Import Failed"),
                              QStringLiteral("Could not read image file."));
        return;
    }

    const render::Resolution targetResolution = m_activeResolution;
    const render::Size targetSize = targetResolution == render::Resolution::Hd
                                        ? render::kHdFrameSize
                                        : render::kSdFrameSize;
    try {
        const document::Rgb565Frame imported = rgb565FrameFromImage(image, targetSize);
        const document::BackgroundRecord& background = m_document->background(*backgroundIndex);
        if (targetResolution == render::Resolution::Hd && !background.colorizedHd) {
            commitEdit(
                std::make_unique<edit::BackgroundHdCreateCommand>(*backgroundIndex, imported));
        } else {
            const document::Rgb565Frame& oldBackground =
                m_document->backgroundFrame(*backgroundIndex, targetResolution);
            std::vector<edit::PixelChange> changes;
            for (int y = 0; y < imported.size.height; ++y) {
                for (int x = 0; x < imported.size.width; ++x) {
                    const std::size_t pixelIndex =
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(imported.size.width) +
                        static_cast<std::size_t>(x);
                    const uint16_t oldColor = oldBackground.pixels[pixelIndex];
                    const uint16_t newColor = imported.pixels[pixelIndex];
                    if (oldColor == newColor) {
                        continue;
                    }
                    changes.push_back(edit::PixelChange{x, y, oldColor, newColor});
                }
            }
            if (changes.empty()) {
                statusBar()->showMessage(QStringLiteral("Background already matches image"), 2500);
                return;
            }
            commitEdit(std::make_unique<edit::PixelEditCommand>(edit::EditKind::BackgroundImage,
                                                                *backgroundIndex,
                                                                targetResolution,
                                                                std::move(changes)));
        }
        if (m_preview) {
            m_preview->update();
        }
        refreshComponentBrowsers();
        updateBackgroundControls();
        updateUndoRedoActions();
        for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
            const std::optional<document::FrameBackgroundData>& backgroundData =
                m_document->backgroundData(frameIndex);
            if (backgroundData && backgroundData->backgroundId == *backgroundIndex) {
                updateFrameThumbnail(frameIndex);
            }
        }
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Imported image into background %1 (%2)")
                .arg(static_cast<int>(*backgroundIndex))
                .arg(targetResolution == render::Resolution::Hd ? QStringLiteral("HD")
                                                                 : QStringLiteral("SD")),
            3000);
    } catch (const std::exception& ex) {
        updateBackgroundControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::exportSelectedBackgroundPng()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !backgroundIndex) {
        statusBar()->showMessage(QStringLiteral("Select a background first"), 2500);
        return;
    }

    const document::BackgroundRecord& background = m_document->background(*backgroundIndex);
    render::Resolution exportResolution = m_activeResolution;
    if (exportResolution == render::Resolution::Hd && !background.colorizedHd) {
        exportResolution = render::Resolution::Sd;
    }

    const QString resolutionName =
        exportResolution == render::Resolution::Hd ? QStringLiteral("hd") : QStringLiteral("sd");
    QFileInfo currentCromInfo(m_currentCromPath);
    QString defaultPath = currentCromInfo.dir().absoluteFilePath(
        QStringLiteral("%1_background_%2_%3.png")
            .arg(currentCromInfo.completeBaseName())
            .arg(static_cast<int>(*backgroundIndex), 3, 10, QLatin1Char('0'))
            .arg(resolutionName));
    QString targetPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Background PNG"),
        defaultPath,
        QStringLiteral("PNG image (*.png);;All files (*)"));
    if (targetPath.isEmpty()) {
        return;
    }
    if (!targetPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        targetPath += QStringLiteral(".png");
    }

    const QImage image = renderRgb565FrameImage(
        m_document->backgroundFrame(*backgroundIndex, exportResolution),
        kExportDmdPixelScale,
        currentDmdRenderOptions());
    if (!image.save(targetPath, "PNG")) {
        QMessageBox::critical(this,
                              QStringLiteral("Export Failed"),
                              QStringLiteral("Could not write PNG file."));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported background PNG"), 3000);
}

void MainWindow::assignSelectedSpriteToCurrentSlot()
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || !m_spritePanel) {
        return;
    }

    const int spriteIndex = m_selectedSpriteIndex;
    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    if (spriteIndex < 0 || !slotIndex ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    try {
        for (std::size_t frameIndex : frameIndices) {
            std::vector<document::FrameSpriteReference> references =
                m_document->frameSpriteReferences(frameIndex);
            if (*slotIndex >= references.size()) {
                continue;
            }
            document::FrameSpriteReference reference = references[*slotIndex];
            reference.spriteId = static_cast<uint8_t>(spriteIndex);
            m_document->setFrameSpriteReference(frameIndex, *slotIndex, reference);
            updateFrameListItem(frameIndex);
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Assigned sprite %1 to slot %2 on %3 frame(s)")
                .arg(spriteIndex)
                .arg(static_cast<int>(*slotIndex))
                .arg(static_cast<int>(frameIndices.size())),
            2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::captureSelectedSpriteFromCurrentFrame()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!m_document || !frameIndex || !spriteIndex || !m_spritePanel) {
        statusBar()->showMessage(QStringLiteral("Select a frame and sprite first"), 2500);
        updateSpriteControls();
        return;
    }

    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    if (!slotIndex) {
        statusBar()->showMessage(QStringLiteral("Select a sprite slot first"), 2500);
        updateSpriteControls();
        return;
    }

    const SpriteSlotBounds bounds = m_spritePanel->slotBounds();
    const int minX = std::clamp(bounds.minX, 0, render::kSdFrameSize.width - 1);
    const int minY = std::clamp(bounds.minY, 0, render::kSdFrameSize.height - 1);
    const int maxX = std::clamp(bounds.maxX, minX, render::kSdFrameSize.width - 1);
    const int maxY = std::clamp(bounds.maxY, minY, render::kSdFrameSize.height - 1);
    const std::vector<render::Point> selectedPixels = spriteCaptureSelectionPixels();
    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    if (width <= 0 || height <= 0 ||
        width > static_cast<int>(document::kMaxSpriteWidth) ||
        height > static_cast<int>(document::kMaxSpriteHeight)) {
        statusBar()->showMessage(QStringLiteral("Sprite capture bounds are invalid"), 2500);
        updateSpriteControls();
        return;
    }
    int captureMinX = minX;
    int captureMinY = minY;
    int captureMaxX = maxX;
    int captureMaxY = maxY;
    if (!selectedPixels.empty()) {
        captureMinX = render::kSdFrameSize.width - 1;
        captureMinY = render::kSdFrameSize.height - 1;
        captureMaxX = 0;
        captureMaxY = 0;
        for (const render::Point& pixel : selectedPixels) {
            captureMinX = std::min(captureMinX, pixel.x);
            captureMinY = std::min(captureMinY, pixel.y);
            captureMaxX = std::max(captureMaxX, pixel.x);
            captureMaxY = std::max(captureMaxY, pixel.y);
        }
    }
    const int captureWidth = captureMaxX - captureMinX + 1;
    const int captureHeight = captureMaxY - captureMinY + 1;
    if (captureWidth <= 0 || captureHeight <= 0 ||
        captureWidth > static_cast<int>(document::kMaxSpriteWidth) ||
        captureHeight > static_cast<int>(document::kMaxSpriteHeight)) {
        statusBar()->showMessage(QStringLiteral("Sprite capture selection is invalid"), 2500);
        updateSpriteControls();
        return;
    }

    QMessageBox confirm(this);
    confirm.setWindowTitle(QStringLiteral("Capture Sprite"));
    confirm.setText(
        selectedPixels.empty()
            ? QStringLiteral("Capture frame region [%1,%2-%3,%4]?")
                  .arg(captureMinX)
                  .arg(captureMinY)
                  .arg(captureMaxX)
                  .arg(captureMaxY)
            : QStringLiteral("Capture %1 selected pixel(s) from [%2,%3-%4,%5]?")
                  .arg(static_cast<int>(selectedPixels.size()))
                  .arg(captureMinX)
                  .arg(captureMinY)
                  .arg(captureMaxX)
                  .arg(captureMaxY));
    confirm.setInformativeText(
        QStringLiteral("Replace sprite %1 with the captured pixels. Creating a new sprite "
                       "record is not available yet because it must update all cROM and .cRP "
                       "sprite tables together.")
            .arg(static_cast<int>(*spriteIndex)));
    QPushButton* replaceButton =
        confirm.addButton(QStringLiteral("Replace Current"), QMessageBox::AcceptRole);
    QPushButton* createButton =
        confirm.addButton(QStringLiteral("Create New"), QMessageBox::ActionRole);
    createButton->setEnabled(false);
    createButton->setToolTip(QStringLiteral("Planned for the sprite allocation step"));
    confirm.addButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != replaceButton) {
        return;
    }

    try {
        const bool includeBackgroundLayer =
            m_framePanel && m_framePanel->isBackgroundLayerChecked();
        const document::Rgb565Frame sourceColorized =
            composeResolvedFrame(*m_document,
                                 *frameIndex,
                                 render::Resolution::Sd,
                                 includeBackgroundLayer);
        if (selectedPixels.empty()) {
            m_document->replaceSpriteFromFrame(*spriteIndex,
                                               m_document->originalFrame(*frameIndex),
                                               sourceColorized,
                                               captureMinX,
                                               captureMinY,
                                               captureWidth,
                                               captureHeight);
        } else {
            m_document->replaceSpriteFromFrameSelection(*spriteIndex,
                                                        m_document->originalFrame(*frameIndex),
                                                        sourceColorized,
                                                        selectedPixels);
        }
        m_undoHistories.clear(edit::EditKind::SpriteImage, *spriteIndex);
        updateUndoRedoActions();
        m_document->setSpriteDetectionArea(*spriteIndex,
                                           0,
                                           document::SpriteDetectionArea{
                                               0,
                                               0,
                                               static_cast<uint16_t>(captureWidth),
                                               static_cast<uint16_t>(captureHeight),
                                           });
        m_document->setFrameSpriteReference(*frameIndex,
                                            *slotIndex,
                                            document::FrameSpriteReference{
                                                static_cast<uint8_t>(*spriteIndex),
                                                static_cast<uint16_t>(captureMinX),
                                                static_cast<uint16_t>(captureMinY),
                                                static_cast<uint16_t>(captureMaxX),
                                                static_cast<uint16_t>(captureMaxY),
                                            });
        if (!selectedPixels.empty()) {
            clearSpriteCaptureSelection();
        }
        setSpriteDependentViewsDirty();
        applyPendingSpriteDependentViews();
        refreshComponentBrowsers();
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Captured frame region [%1,%2-%3,%4] into sprite %5")
                .arg(captureMinX)
                .arg(captureMinY)
                .arg(captureMaxX)
                .arg(captureMaxY)
                .arg(static_cast<int>(*spriteIndex)),
            3000);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::clearSpriteCaptureSelection()
{
    m_spriteCaptureSelectionFrameIndex.reset();
    m_spriteCaptureSelectionMask.clear();
    if (m_preview) {
        m_preview->clearSelectionOverlayPixels();
    }
    updateSpriteControls();
}

bool MainWindow::isSpriteCaptureSelectionModeActive() const
{
    return m_spritePanel &&
           m_spritePanel->isCaptureSelectionChecked() &&
           m_document.has_value() &&
           currentFrameIndex().has_value();
}

void MainWindow::paintSpriteCaptureSelectionPixel(const render::CanvasHit& hit, bool erase)
{
    if (m_drawingTool != DrawingTool::Point ||
        hit.region != render::CanvasRegion::OriginalFrame ||
        hit.resolution != render::Resolution::Sd) {
        return;
    }
    commitSpriteCaptureSelectionPixels({render::Point{hit.x, hit.y}}, erase);
}

void MainWindow::commitSpriteCaptureSelectionPixels(const std::vector<render::Point>& pixels,
                                                    bool erase)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || pixels.empty()) {
        return;
    }
    if (!m_spriteCaptureSelectionFrameIndex ||
        *m_spriteCaptureSelectionFrameIndex != *frameIndex ||
        m_spriteCaptureSelectionMask.size() !=
            static_cast<std::size_t>(render::kSdFrameSize.width) *
                static_cast<std::size_t>(render::kSdFrameSize.height)) {
        m_spriteCaptureSelectionFrameIndex = *frameIndex;
        m_spriteCaptureSelectionMask.assign(
            static_cast<std::size_t>(render::kSdFrameSize.width) *
                static_cast<std::size_t>(render::kSdFrameSize.height),
            0);
    }

    bool changed = false;
    for (const render::Point& pixel : pixels) {
        if (pixel.x < 0 || pixel.y < 0 ||
            pixel.x >= render::kSdFrameSize.width ||
            pixel.y >= render::kSdFrameSize.height) {
            continue;
        }
        const std::size_t index =
            static_cast<std::size_t>(pixel.y) *
                static_cast<std::size_t>(render::kSdFrameSize.width) +
            static_cast<std::size_t>(pixel.x);
        const uint8_t newValue = erase ? uint8_t{0} : uint8_t{1};
        if (m_spriteCaptureSelectionMask[index] == newValue) {
            continue;
        }
        m_spriteCaptureSelectionMask[index] = newValue;
        changed = true;
    }
    if (!changed) {
        return;
    }
    updateSpriteCaptureSelectionOverlay();
    updateSpriteControls();
}

void MainWindow::updateSpriteCaptureSelectionOverlay()
{
    if (!m_preview) {
        return;
    }
    const std::vector<render::Point> pixels = spriteCaptureSelectionPixels();
    if (pixels.empty()) {
        m_preview->clearSelectionOverlayPixels();
        return;
    }
    m_preview->setSelectionOverlayPixels(render::CanvasRegion::OriginalFrame,
                                         render::Resolution::Sd,
                                         pixels,
                                         QColor(255, 0, 255, 110));
}

std::vector<render::Point> MainWindow::spriteCaptureSelectionPixels() const
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!frameIndex ||
        !m_spriteCaptureSelectionFrameIndex ||
        *m_spriteCaptureSelectionFrameIndex != *frameIndex ||
        m_spriteCaptureSelectionMask.size() !=
            static_cast<std::size_t>(render::kSdFrameSize.width) *
                static_cast<std::size_t>(render::kSdFrameSize.height)) {
        return {};
    }

    std::vector<render::Point> pixels;
    for (int y = 0; y < render::kSdFrameSize.height; ++y) {
        for (int x = 0; x < render::kSdFrameSize.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(render::kSdFrameSize.width) +
                static_cast<std::size_t>(x);
            if (m_spriteCaptureSelectionMask[index]) {
                pixels.push_back(render::Point{x, y});
            }
        }
    }
    return pixels;
}

void MainWindow::removeCurrentSpriteSlot()
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || !m_spritePanel) {
        return;
    }

    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    if (!slotIndex) {
        updateSpriteControls();
        return;
    }

    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->clearFrameSpriteReference(frameIndex, *slotIndex);
            updateFrameListItem(frameIndex);
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Cleared sprite slot %1 on %2 frame(s)")
                .arg(static_cast<int>(*slotIndex))
                .arg(static_cast<int>(frameIndices.size())),
            2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentSpriteSlotBounds()
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || !m_spritePanel) {
        return;
    }

    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    if (!slotIndex) {
        updateSpriteControls();
        return;
    }

    const SpriteSlotBounds bounds = m_spritePanel->slotBounds();
    const int minX = std::min(bounds.minX, bounds.maxX);
    const int maxX = std::max(bounds.minX, bounds.maxX);
    const int minY = std::min(bounds.minY, bounds.maxY);
    const int maxY = std::max(bounds.minY, bounds.maxY);

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            std::vector<document::FrameSpriteReference> references =
                m_document->frameSpriteReferences(frameIndex);
            if (*slotIndex >= references.size()) {
                continue;
            }
            document::FrameSpriteReference reference = references[*slotIndex];
            const document::FrameSpriteReference updated{
                reference.spriteId,
                static_cast<uint16_t>(minX),
                static_cast<uint16_t>(minY),
                static_cast<uint16_t>(maxX),
                static_cast<uint16_t>(maxY),
            };
            if (updated.spriteId == reference.spriteId &&
                updated.minX == reference.minX &&
                updated.minY == reference.minY &&
                updated.maxX == reference.maxX &&
                updated.maxY == reference.maxY) {
                continue;
            }
            m_document->setFrameSpriteReference(frameIndex, *slotIndex, updated);
            updateFrameListItem(frameIndex);
            changed = true;
        }
        if (!changed) {
            updateSpriteControls();
            return;
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Updated sprite slot %1 bounds on %2 frame(s)")
                .arg(static_cast<int>(*slotIndex))
                .arg(static_cast<int>(frameIndices.size())),
            1500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentSpriteName(const QString& name)
{
    if (!m_document) {
        return;
    }
    const int spriteIndex = m_selectedSpriteIndex;
    if (spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->setSpriteName(static_cast<std::size_t>(spriteIndex),
                                  name.toStdString());
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(QStringLiteral("Renamed sprite %1").arg(spriteIndex), 1500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentSpriteDetectionArea()
{
    if (!m_document || !m_spritePanel) {
        return;
    }

    const int spriteIndex = m_selectedSpriteIndex;
    const std::size_t areaIndex = m_spritePanel->selectedDetectionAreaIndex();
    if (spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    const SpriteDetectionAreaInput input = m_spritePanel->detectionAreaInput();
    const int x = input.x;
    const int y = input.y;
    const int width = std::min(input.width, static_cast<int>(document::kMaxSpriteWidth) - x);
    const int height = std::min(input.height, static_cast<int>(document::kMaxSpriteHeight) - y);
    if (width <= 0 || height <= 0) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                           areaIndex,
                                           document::SpriteDetectionArea{
                                               static_cast<uint16_t>(x),
                                               static_cast<uint16_t>(y),
                                               static_cast<uint16_t>(width),
                                               static_cast<uint16_t>(height),
                                           });
        if (m_spriteEditor) {
            m_spriteEditor->update();
        }
        setSpriteDependentViewsDirty();
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Updated sprite %1 detection area %2")
                .arg(spriteIndex)
                .arg(static_cast<int>(areaIndex)),
            1500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::clearCurrentSpriteDetectionArea()
{
    if (!m_document || !m_spritePanel) {
        return;
    }

    const int spriteIndex = m_selectedSpriteIndex;
    const std::size_t areaIndex = m_spritePanel->selectedDetectionAreaIndex();
    if (spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                           areaIndex,
                                           document::SpriteDetectionArea{
                                               0xffff,
                                               0xffff,
                                               0xffff,
                                               0xffff,
                                           });
        if (m_spriteEditor) {
            m_spriteEditor->update();
        }
        setSpriteDependentViewsDirty();
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Cleared sprite %1 detection area %2")
                .arg(spriteIndex)
                .arg(static_cast<int>(areaIndex)),
            1500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::paintSpriteColorizedPixel(std::size_t spriteIndex,
                                           render::Resolution resolution,
                                           int x,
                                           int y,
                                           bool erase)
{
    paintSpriteColorizedPixels(spriteIndex, resolution, {render::Point{x, y}}, erase);
}

void MainWindow::paintSpriteColorizedPixels(std::size_t spriteIndex,
                                            render::Resolution resolution,
                                            const std::vector<render::Point>& pixels,
                                            bool erase)
{
    if (!m_document || spriteIndex >= m_document->spriteCount()) {
        return;
    }

    const uint16_t color = erase ? uint16_t{0} : m_activeDrawColor;
    try {
        const edit::EditLayer layer{edit::EditKind::SpriteImage, spriteIndex, resolution};
        if (!commitPixelChanges(layer,
                                edit::makeSpritePaintChanges(*m_document,
                                                             spriteIndex,
                                                             resolution,
                                                             pixels,
                                                             color,
                                                             erase))) {
            return;
        }
        setSpriteDependentViewsDirty();
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::pickSpriteColorizedPixel(std::size_t spriteIndex,
                                          render::Resolution resolution,
                                          int x,
                                          int y)
{
    if (!m_document || spriteIndex >= m_document->spriteCount()) {
        return;
    }
    try {
        const uint16_t color = m_document->spriteColorizedPixel(spriteIndex, resolution, x, y);
        setActiveDrawColor(color);
        statusBar()->showMessage(QStringLiteral("Picked sprite color 0x%1")
                                     .arg(color, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::floodFillSpriteColorized(std::size_t spriteIndex,
                                          render::Resolution resolution,
                                          int x,
                                          int y)
{
    if (!m_document || spriteIndex >= m_document->spriteCount()) {
        return;
    }

    try {
        if (m_document->spriteColorizedPixel(spriteIndex, resolution, x, y) == m_activeDrawColor) {
            return;
        }
        paintSpriteColorizedPixels(spriteIndex,
                                   resolution,
                                   edit::spriteFloodFillPixels(*m_document,
                                                               spriteIndex,
                                                               resolution,
                                                               render::Point{x, y}),
                                   false);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setSpriteDependentViewsDirty()
{
    m_spriteDependentViewsDirty = true;
    if (m_spritePanel) {
        m_spritePanel->setApplySpriteEditsEnabled(true);
    }
}

void MainWindow::applyPendingSpriteDependentViews()
{
    if (!m_spriteDependentViewsDirty) {
        return;
    }
    m_spriteDependentViewsDirty = false;
    if (m_spritePanel) {
        m_spritePanel->setApplySpriteEditsEnabled(false);
    }
    if (m_preview) {
        m_preview->update();
    }
    refreshFrameList(currentFrameIndex());
    updateSpriteControls();
    updateProjectHeaderStatus();
    statusBar()->showMessage(QStringLiteral("Applied sprite edits to previews"), 1000);
}

void MainWindow::createCurrentSpriteHd()
{
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!m_document || !spriteIndex) {
        updateSpriteControls();
        return;
    }
    if (m_document->hasHdSprite(*spriteIndex)) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->createHdSprite(*spriteIndex);
        m_undoHistories.clear(
            {edit::EditKind::SpriteImage, *spriteIndex, render::Resolution::Hd});
        m_activeResolution = render::Resolution::Hd;
        if (m_spriteEditor) {
            m_spriteEditor->setActiveResolution(m_activeResolution);
            m_spriteEditor->update();
        }
        setSpriteDependentViewsDirty();
        updateResolutionMode();
        updateSpriteControls();
        updateProjectHeaderStatus();
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Created HD sprite from SD"), 2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::removeCurrentSpriteHd()
{
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!m_document || !spriteIndex || !m_document->hasHdSprite(*spriteIndex)) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->removeHdSprite(*spriteIndex);
        m_undoHistories.clear(
            {edit::EditKind::SpriteImage, *spriteIndex, render::Resolution::Hd});
        if (m_activeResolution == render::Resolution::Hd) {
            m_activeResolution = render::Resolution::Sd;
        }
        if (m_spriteEditor) {
            m_spriteEditor->setActiveResolution(m_activeResolution);
            m_spriteEditor->update();
        }
        setSpriteDependentViewsDirty();
        updateResolutionMode();
        updateSpriteControls();
        updateProjectHeaderStatus();
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Removed HD sprite"), 2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::createCurrentFrameHd()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }
    if (m_document->hasHdFrame(*frameIndex)) {
        updateFrameMetadataControls();
        return;
    }

    const bool hasDynamicData = m_document->dynamicData(*frameIndex).has_value();
    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(*frameIndex);
    const bool hasBackgroundData = backgroundData.has_value();
    const bool hasAssignedBackground =
        backgroundData && backgroundData->backgroundId < m_document->backgroundCount();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Create HD Frame"));
    auto* dialogLayout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(QStringLiteral("Create HD frame from SD. Upscale related data?"),
                                   &dialog);
    description->setWordWrap(true);
    dialogLayout->addWidget(description);
    auto* dynamicMaskCheckBox = new QCheckBox(QStringLiteral("Dynamic mask and colors"), &dialog);
    dynamicMaskCheckBox->setChecked(hasDynamicData);
    dynamicMaskCheckBox->setEnabled(hasDynamicData);
    dynamicMaskCheckBox->setToolTip(QStringLiteral("Scale SD dynamic mask to HD and copy dynamic colors"));
    dialogLayout->addWidget(dynamicMaskCheckBox);
    auto* backgroundMaskCheckBox = new QCheckBox(QStringLiteral("Background mask"), &dialog);
    backgroundMaskCheckBox->setChecked(hasBackgroundData);
    backgroundMaskCheckBox->setEnabled(hasBackgroundData);
    backgroundMaskCheckBox->setToolTip(QStringLiteral("Scale SD background mask to HD"));
    dialogLayout->addWidget(backgroundMaskCheckBox);
    auto* backgroundImageCheckBox = new QCheckBox(QStringLiteral("Assigned background image"), &dialog);
    backgroundImageCheckBox->setChecked(hasAssignedBackground);
    backgroundImageCheckBox->setEnabled(hasAssignedBackground);
    backgroundImageCheckBox->setToolTip(QStringLiteral("Scale assigned SD background image to HD"));
    dialogLayout->addWidget(backgroundImageCheckBox);
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                           &dialog);
    dialogLayout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    try {
        const render::Resolution hd = render::Resolution::Hd;
        m_document->createHdFrame(*frameIndex);
        m_undoHistories.clear({edit::EditKind::FramePixels, *frameIndex, hd});
        if (dynamicMaskCheckBox->isChecked()) {
            m_document->upscaleDynamicMaskToHd(*frameIndex);
            m_undoHistories.clear({edit::EditKind::DynamicMask, *frameIndex, hd});
            m_undoHistories.clear({edit::EditKind::DynamicColor, *frameIndex, hd});
            m_undoHistories.clear({edit::EditKind::DynamicShadow, *frameIndex, hd});
        }
        if (backgroundMaskCheckBox->isChecked()) {
            m_document->upscaleBackgroundMaskToHd(*frameIndex);
            m_undoHistories.clear({edit::EditKind::BackgroundMask, *frameIndex, hd});
        }
        if (backgroundImageCheckBox->isChecked()) {
            const std::size_t backgroundIndex = m_document->backgroundId(*frameIndex);
            m_document->upscaleAssignedBackgroundToHd(*frameIndex);
            m_undoHistories.clear({edit::EditKind::BackgroundImage, backgroundIndex, hd});
        }
        m_activeResolution = hd;
        updateResolutionMode();
        updateFrameMetadataControls();
        updateFrameListItem(*frameIndex);
        updateProjectHeaderStatus();
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Created HD frame from SD"), 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::removeCurrentFrameHd()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || !m_document->hasHdFrame(*frameIndex)) {
        updateFrameMetadataControls();
        return;
    }

    try {
        m_document->removeHdFrame(*frameIndex);
        m_undoHistories.clear(
            {edit::EditKind::FramePixels, *frameIndex, render::Resolution::Hd});
        if (m_activeResolution == render::Resolution::Hd) {
            m_activeResolution = render::Resolution::Sd;
        }
        updateResolutionMode();
        updateFrameMetadataControls();
        updateFrameListItem(*frameIndex);
        updateProjectHeaderStatus();
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Removed HD frame"), 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updateDynamicColorControls()
{
    if (m_dynamicPanel) {
        m_dynamicPanel->refreshColors(m_document ? &*m_document : nullptr,
                                      currentFrameIndex(),
                                      m_activeResolution);
    }
    updateDynamicShadowControls();
}

void MainWindow::updateDynamicShadowControls()
{
    if (m_dynamicPanel) {
        m_dynamicPanel->refreshShadow(m_document ? &*m_document : nullptr,
                                      currentFrameIndex(),
                                      m_activeResolution);
    }
}

void MainWindow::dynamicSetChanged(int setIndex)
{
    statusBar()->showMessage(QStringLiteral("Selected dynamic set %1").arg(setIndex), 1500);
    updateDynamicColorControls();
}

void MainWindow::editDynamicColor(std::size_t originalColorIndex)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }

    try {
        const uint16_t oldRgb565 = m_document->dynamicColor(*frameIndex,
                                                            m_activeResolution,
                                                            activeDynamicSet(),
                                                            originalColorIndex);
        const QColor newColor = QColorDialog::getColor(rgb565ToColor(oldRgb565),
                                                       this,
                                                       QStringLiteral("Dynamic Color"));
        if (!newColor.isValid()) {
            return;
        }

        const uint16_t newRgb565 = colorToRgb565(newColor);
        if (newRgb565 == oldRgb565) {
            return;
        }
        commitEdit(std::make_unique<edit::DynamicColorEditCommand>(*frameIndex,
                                                                   m_activeResolution,
                                                                   activeDynamicSet(),
                                                                   originalColorIndex,
                                                                   oldRgb565,
                                                                   newRgb565));
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Updated dynamic color 0x%1")
                                     .arg(newRgb565, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        updateDynamicColorControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setDynamicShadowDirectionBit(uint8_t bit, bool enabled)
{
    if (!m_document || !currentFrameIndex()) {
        return;
    }

    try {
        bool changed = false;
        for (std::size_t frameIndex : selectedFrameIndices()) {
            if (!m_document->dynamicData(frameIndex).has_value()) {
                continue;
            }
            const uint8_t oldDirection =
                m_document->dynamicShadowDirection(frameIndex,
                                                   m_activeResolution,
                                                   activeDynamicSet());
            const uint8_t newDirection =
                enabled ? static_cast<uint8_t>(oldDirection | bit)
                        : static_cast<uint8_t>(oldDirection & ~bit);
            if (newDirection == oldDirection) {
                continue;
            }
            edit::DynamicShadowChange change;
            change.oldDirection = oldDirection;
            change.newDirection = newDirection;
            commitEdit(std::make_unique<edit::DynamicShadowEditCommand>(frameIndex,
                                                                        m_activeResolution,
                                                                        activeDynamicSet(),
                                                                        true,
                                                                        false,
                                                                        change));
            changed = true;
        }
        if (!changed) {
            updateDynamicShadowControls();
            return;
        }
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Updated dynamic shadow direction"), 2500);
    } catch (const std::exception& ex) {
        updateDynamicShadowControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::editDynamicShadowColor()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }

    try {
        const uint16_t oldRgb565 =
            m_document->dynamicShadowColor(*frameIndex, m_activeResolution, activeDynamicSet());
        const QColor newColor = QColorDialog::getColor(rgb565ToColor(oldRgb565),
                                                       this,
                                                       QStringLiteral("Dynamic Shadow Color"));
        if (!newColor.isValid()) {
            return;
        }

        const uint16_t newRgb565 = colorToRgb565(newColor);
        if (newRgb565 == oldRgb565) {
            return;
        }

        bool changed = false;
        for (std::size_t selectedFrameIndex : selectedFrameIndices()) {
            if (!m_document->dynamicData(selectedFrameIndex).has_value()) {
                continue;
            }
            const uint16_t oldColor =
                m_document->dynamicShadowColor(selectedFrameIndex,
                                               m_activeResolution,
                                               activeDynamicSet());
            if (oldColor == newRgb565) {
                continue;
            }
            edit::DynamicShadowChange change;
            change.oldColor = oldColor;
            change.newColor = newRgb565;
            commitEdit(std::make_unique<edit::DynamicShadowEditCommand>(selectedFrameIndex,
                                                                        m_activeResolution,
                                                                        activeDynamicSet(),
                                                                        false,
                                                                        true,
                                                                        change));
            changed = true;
        }
        if (!changed) {
            return;
        }
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Updated dynamic shadow color 0x%1")
                                     .arg(newRgb565, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        updateDynamicShadowControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updateColorRotationControls()
{
    if (m_colorRotationPanel) {
        m_colorRotationPanel->refresh(m_document ? &*m_document : nullptr,
                                      currentFrameIndex(),
                                      m_activeResolution);
    }
}

void MainWindow::setCurrentColorRotationLength(std::size_t slotIndex, int length)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const uint16_t storedLength = static_cast<uint16_t>(
        std::clamp(length, 0, static_cast<int>(document::kMaxColorRotationColors)));
    const bool wasDirty = m_document->isDirty();
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->setColorRotationLength(frameIndex,
                                               m_activeResolution,
                                               slotIndex,
                                               storedLength);
        }
        updateColorRotationControls();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        statusBar()->showMessage(
            QStringLiteral("Set rotation length %1 on %2 frame(s)")
                .arg(storedLength)
                .arg(static_cast<int>(frameIndices.size())),
            2000);
    } catch (const std::exception& ex) {
        updateColorRotationControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentColorRotationDelayMs(std::size_t slotIndex, int delayMs)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const uint16_t storedDelayMs =
        static_cast<uint16_t>(std::clamp(delayMs, 0, 2048));
    const bool wasDirty = m_document->isDirty();
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->setColorRotationDelayMs(frameIndex,
                                                m_activeResolution,
                                                slotIndex,
                                                storedDelayMs);
        }
        updateColorRotationControls();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        statusBar()->showMessage(
            QStringLiteral("Set rotation delay %1 ms on %2 frame(s)")
                .arg(storedDelayMs)
                .arg(static_cast<int>(frameIndices.size())),
            2000);
    } catch (const std::exception& ex) {
        updateColorRotationControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::editCurrentColorRotationColor(std::size_t slotIndex, std::size_t colorIndex)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || frameIndices.empty()) {
        return;
    }

    const uint16_t length =
        m_document->colorRotationLength(*frameIndex, m_activeResolution, slotIndex);
    if (length == 0 || colorIndex >= length) {
        updateColorRotationControls();
        return;
    }

    const uint16_t oldColor =
        m_document->colorRotationColor(*frameIndex, m_activeResolution, slotIndex, colorIndex);
    const QColor newColor = QColorDialog::getColor(rgb565ToColor(oldColor),
                                                   this,
                                                   QStringLiteral("Color Rotation Color"));
    if (!newColor.isValid()) {
        return;
    }
    const uint16_t newRgb565 = colorToRgb565(newColor);
    if (newRgb565 == oldColor) {
        return;
    }

    const bool wasDirty = m_document->isDirty();
    try {
        for (std::size_t selectedFrameIndex : frameIndices) {
            const uint16_t selectedLength =
                m_document->colorRotationLength(selectedFrameIndex,
                                                m_activeResolution,
                                                slotIndex);
            if (selectedLength > colorIndex) {
                m_document->setColorRotationColor(selectedFrameIndex,
                                                  m_activeResolution,
                                                  slotIndex,
                                                  colorIndex,
                                                  newRgb565);
            }
        }
        updateColorRotationControls();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        statusBar()->showMessage(
            QStringLiteral("Updated rotation color on %1 frame(s)")
                .arg(static_cast<int>(frameIndices.size())),
            2000);
    } catch (const std::exception& ex) {
        updateColorRotationControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::clearCurrentColorRotationSlot(std::size_t slotIndex)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const bool wasDirty = m_document->isDirty();
    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->clearColorRotationSlot(frameIndex, m_activeResolution, slotIndex);
        }
        updateColorRotationControls();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        statusBar()->showMessage(
            QStringLiteral("Cleared rotation slot %1 on %2 frame(s)")
                .arg(static_cast<int>(slotIndex))
                .arg(static_cast<int>(frameIndices.size())),
            2000);
    } catch (const std::exception& ex) {
        updateColorRotationControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updatePaletteControls()
{
    if (!m_palettePanel) {
        return;
    }

    const bool hasPalette = selectedPaletteIndex().has_value();
    if (!hasPalette) {
        m_canvasSelectionDirectionMode = false;
    }
    m_palettePanel->refresh(m_document ? &*m_document : nullptr,
                            m_canvasSelection.has_value(),
                            m_canvasSelectionDirectionMode);
    if (hasPalette) {
        updateUndoRedoActions();
    }
}

void MainWindow::setCurrentPaletteName(const QString& paletteName)
{
    if (!m_document) {
        return;
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (!paletteIndex) {
        updatePaletteControls();
        return;
    }

    const std::string name = paletteName.toStdString();
    const bool wasDirty = m_document->isDirty();
    try {
        m_document->setPaletteName(*paletteIndex, name);
        refreshComponentBrowsers();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
    } catch (const std::exception& ex) {
        updatePaletteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::pickCurrentPaletteColor(std::size_t colorIndex)
{
    if (!m_document) {
        return;
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (!paletteIndex) {
        updatePaletteControls();
        return;
    }

    const uint16_t color = m_document->paletteColor(*paletteIndex, colorIndex);
    setActiveDrawColor(color);
    statusBar()->showMessage(
        QStringLiteral("Picked palette %1 color %2: 0x%3")
            .arg(static_cast<int>(*paletteIndex))
            .arg(static_cast<int>(colorIndex) + 1)
            .arg(color, 4, 16, QLatin1Char('0')),
        2000);
}

void MainWindow::editCurrentPaletteColor(std::size_t colorIndex)
{
    if (!m_document) {
        return;
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (!paletteIndex) {
        updatePaletteControls();
        return;
    }

    const uint16_t oldColor = m_document->paletteColor(*paletteIndex, colorIndex);
    const QColor newColor = QColorDialog::getColor(rgb565ToColor(oldColor),
                                                   this,
                                                   QStringLiteral("Palette Color"));
    if (!newColor.isValid()) {
        return;
    }
    const uint16_t newRgb565 = colorToRgb565(newColor);
    if (newRgb565 == oldColor) {
        return;
    }

    const bool wasDirty = m_document->isDirty();
    try {
        std::vector<edit::PaletteColorChange> changes;
        changes.push_back(edit::PaletteColorChange{
            colorIndex,
            oldColor,
            newRgb565,
        });
        commitEdit(std::make_unique<edit::PaletteEditCommand>(*paletteIndex, std::move(changes)));
        updateUndoRedoActions();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        statusBar()->showMessage(
            QStringLiteral("Updated palette %1 color %2")
                .arg(static_cast<int>(*paletteIndex))
                .arg(static_cast<int>(colorIndex) + 1),
            2000);
    } catch (const std::exception& ex) {
        updatePaletteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::applyCurrentPaletteGradient()
{
    if (!m_document || !m_palettePanel) {
        return;
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (!paletteIndex) {
        updatePaletteControls();
        return;
    }

    const auto [startIndex, endIndex] = m_palettePanel->gradientEndpoints();
    const int first = std::min(startIndex, endIndex);
    const int last = std::max(startIndex, endIndex);
    if (first == last) {
        statusBar()->showMessage(QStringLiteral("Gradient needs at least two colors"), 2500);
        return;
    }

    std::vector<edit::PaletteColorChange> changes =
        edit::makePaletteRampChanges(*m_document, *paletteIndex, startIndex, endIndex);
    if (changes.empty()) {
        return;
    }
    const bool wasDirty = m_document->isDirty();
    try {
        commitEdit(std::make_unique<edit::PaletteEditCommand>(*paletteIndex, std::move(changes)));
        updateUndoRedoActions();
        if (!wasDirty && m_document->isDirty()) {
            updateProjectHeaderStatus();
        }
        statusBar()->showMessage(
            QStringLiteral("Applied palette gradient %1-%2")
                .arg(first + 1)
                .arg(last + 1),
            2500);
    } catch (const std::exception& ex) {
        updatePaletteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updateSpriteControls()
{
    if (!m_spritePanel) {
        return;
    }

    const int spriteCount = m_document ? static_cast<int>(m_document->spriteCount()) : 0;
    if (spriteCount == 0) {
        m_selectedSpriteIndex = -1;
    } else if (m_selectedSpriteIndex < 0 || m_selectedSpriteIndex >= spriteCount) {
        m_selectedSpriteIndex = 0;
    }

    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    SpritePanelState state;
    state.document = m_document ? &*m_document : nullptr;
    state.frameIndex = frameIndex;
    state.spriteIndex = spriteIndex;
    state.resolution = m_activeResolution;
    state.hasCaptureSelection = !spriteCaptureSelectionPixels().empty();
    state.spriteEditsPending = m_spriteDependentViewsDirty;
    m_spritePanel->refresh(state);

    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    std::optional<document::FrameSpriteReference> selectedReference;
    if (m_document && frameIndex && slotIndex) {
        const std::vector<document::FrameSpriteReference>& references =
            m_document->frameSpriteReferences(*frameIndex);
        if (*slotIndex < references.size()) {
            selectedReference = references[*slotIndex];
        }
    }
    if (m_preview) {
        m_preview->setSelectedSpriteReference(selectedReference);
    }

    if (!spriteIndex) {
        if (isSpriteCanvasActive()) {
            m_framePanel->disableHdActions();
        }
        if (m_canvasStack && m_preview) {
            m_canvasStack->setCurrentWidget(m_preview);
        }
        if (m_spriteEditor) {
            m_spriteEditor->setSpriteIndex(std::nullopt);
        }
        return;
    }

    const document::SpriteRecord& sprite = m_document->sprite(*spriteIndex);
    if (isSpriteCanvasActive()) {
        m_framePanel->showSpriteHdActions(sprite.hasHd);
    }
    if (m_spriteEditor) {
        const std::size_t areaIndex = m_spritePanel->selectedDetectionAreaIndex();
        m_spriteEditor->setSpriteIndex(*spriteIndex);
        m_spriteEditor->setDetectionAreasVisible(m_spritePanel->isDetectionAreasChecked());
        m_spriteEditor->setSelectedDetectionAreaIndex(
            areaIndex < sprite.detectionAreas.size() ? std::optional<std::size_t>(areaIndex)
                                                     : std::nullopt);
    }
    if (m_canvasStack && m_preview && m_spriteEditor) {
        m_canvasStack->setCurrentWidget(m_spritePanel->isSpriteCanvasChecked()
                                            ? static_cast<QWidget*>(m_spriteEditor)
                                            : static_cast<QWidget*>(m_preview));
    }
    updateSpriteBrowserSelection();
}

void MainWindow::updatePaintTargetMode()
{
    const bool backgroundTargetActive = isBackgroundImageTargetActive();
    if (m_preview) {
        m_preview->setSelectedBackgroundIndex(selectedBackgroundIndex());
        m_preview->setBackgroundImageEditModeEnabled(backgroundTargetActive);
        m_preview->clearPreviewPixels();
    }
    updateUndoRedoActions();
}

void MainWindow::setPaintTarget(PaintTarget target)
{
    if (!m_framePanel || m_framePanel->paintTarget() == target) {
        return;
    }
    m_framePanel->setPaintTarget(target);
    paintTargetChanged();
}

void MainWindow::paintTargetChanged()
{
    cancelFreePaletteGradientSelection();
    clearCanvasSelection();
    updatePaintTargetMode();
}

PaintTarget MainWindow::activePaintTarget() const
{
    if (!m_framePanel) {
        return PaintTarget::Frame;
    }
    return m_framePanel->paintTarget();
}

uint8_t MainWindow::activeDynamicSet() const
{
    return m_dynamicPanel ? m_dynamicPanel->dynamicSet() : uint8_t{0};
}

void MainWindow::setActiveDrawColor(uint16_t rgb565)
{
    m_activeDrawColor = rgb565;
    if (m_spriteEditor) {
        m_spriteEditor->setActiveDrawColor(rgb565);
    }
    for (std::size_t index = 0; index < m_colorButtons.size(); ++index) {
        QToolButton* button = m_colorButtons[index];
        QSignalBlocker blocker(button);
        const bool matches =
            index < m_favoriteColors.size() && m_favoriteColors[index] == rgb565;
        button->setChecked(matches);
    }
    if (m_activeColorButton) {
        m_activeColorButton->setToolTip(
            QStringLiteral("Active draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
        m_activeColorButton->setStyleSheet(colorButtonStyle(rgb565, true));
    }
}

void MainWindow::setFavoriteDrawColor(std::size_t favoriteIndex, uint16_t rgb565)
{
    if (favoriteIndex >= m_favoriteColors.size() || favoriteIndex >= m_colorButtons.size()) {
        return;
    }
    m_favoriteColors[favoriteIndex] = rgb565;
    QToolButton* button = m_colorButtons[favoriteIndex];
    button->setProperty("rgb565", static_cast<int>(rgb565));
    button->setToolTip(QStringLiteral("Draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
    button->setStyleSheet(favoriteColorButtonStyle(rgb565));
    setActiveDrawColor(rgb565);
    statusBar()->showMessage(
        QStringLiteral("Updated favorite color %1 to 0x%2")
            .arg(static_cast<int>(favoriteIndex + 1))
            .arg(rgb565, 4, 16, QLatin1Char('0')),
        2000);
}

void MainWindow::setDrawingTool(DrawingTool tool)
{
    if (tool != m_drawingTool) {
        cancelFreePaletteGradientSelection();
    }
    m_drawingTool = tool;
    if (m_preview) {
        m_preview->clearPreviewPixels();
    }
    if (m_spriteEditor) {
        SpriteDrawingTool spriteTool = SpriteDrawingTool::Point;
        switch (tool) {
        case DrawingTool::Point:
            spriteTool = SpriteDrawingTool::Point;
            break;
        case DrawingTool::Line:
            spriteTool = SpriteDrawingTool::Line;
            break;
        case DrawingTool::Rectangle:
            spriteTool = SpriteDrawingTool::Rectangle;
            break;
        case DrawingTool::FilledRectangle:
            spriteTool = SpriteDrawingTool::FilledRectangle;
            break;
        case DrawingTool::Circle:
            spriteTool = SpriteDrawingTool::Circle;
            break;
        case DrawingTool::FilledCircle:
            spriteTool = SpriteDrawingTool::FilledCircle;
            break;
        case DrawingTool::Ellipse:
            spriteTool = SpriteDrawingTool::Ellipse;
            break;
        case DrawingTool::FilledEllipse:
            spriteTool = SpriteDrawingTool::FilledEllipse;
            break;
        case DrawingTool::PaletteGradient:
            spriteTool = SpriteDrawingTool::FilledRectangle;
            break;
        case DrawingTool::Selection:
            spriteTool = SpriteDrawingTool::FilledRectangle;
            break;
        case DrawingTool::ColorPicker:
            spriteTool = SpriteDrawingTool::ColorPicker;
            break;
        case DrawingTool::FloodFill:
            spriteTool = SpriteDrawingTool::FloodFill;
            break;
        }
        m_spriteEditor->setDrawingTool(spriteTool);
    }
    for (QToolButton* button : m_toolButtons) {
        QSignalBlocker blocker(button);
        button->setChecked(button->property("drawingTool").toInt() == static_cast<int>(tool));
    }
}

void MainWindow::paintFramePixel(const render::CanvasHit& hit, bool erase)
{
    if (m_drawingTool != DrawingTool::Selection && isCanvasSelectionDirectionModeActive()) {
        updateCanvasSelectionOverlay();
        return;
    }
    if (m_drawingTool == DrawingTool::PaletteGradient) {
        if (m_palettePanel->gradientDirection() == PaletteGradientDirection::Free &&
            isFreePaletteGradientPending()) {
            showPendingFreePaletteGradientSelection();
        }
        return;
    }
    if (isComparisonMaskModeActive()) {
        return;
    }
    if (isBackgroundMaskModeActive()) {
        if (m_drawingTool == DrawingTool::FloodFill) {
            floodFillBackgroundMask(hit, erase);
            return;
        }
        if (m_drawingTool == DrawingTool::Point) {
            paintBackgroundMaskPixel(hit, erase);
        }
        return;
    }
    if (isDynamicMaskModeActive()) {
        if (m_drawingTool == DrawingTool::ColorPicker) {
            pickDynamicSet(hit);
            return;
        }
        if (m_drawingTool == DrawingTool::FloodFill) {
            floodFillDynamicMask(hit, erase);
            return;
        }
        if (m_drawingTool == DrawingTool::Point) {
            paintDynamicMaskPixel(hit, erase);
        }
        return;
    }
    if (isBackgroundImageTargetActive()) {
        if (m_drawingTool == DrawingTool::ColorPicker) {
            pickBackgroundImageColor(hit);
            return;
        }
        if (m_drawingTool == DrawingTool::FloodFill) {
            floodFillBackgroundImage(hit);
            return;
        }
        if (m_drawingTool == DrawingTool::Point) {
            paintBackgroundImagePixel(hit);
        }
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

void MainWindow::pickDynamicSet(const render::CanvasHit& hit)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || !m_dynamicPanel) {
        return;
    }
    try {
        const uint8_t dynamicSet =
            m_document->dynamicMaskPixel(*frameIndex, hit.resolution, hit.x, hit.y);
        if (dynamicSet == 255 || dynamicSet >= document::kMaxDynamicSetsPerFrame) {
            statusBar()->showMessage(QStringLiteral("No dynamic set at %1,%2")
                                         .arg(hit.x)
                                         .arg(hit.y),
                                     2500);
            return;
        }
        if (dynamicSet != m_dynamicPanel->dynamicSet()) {
            m_dynamicPanel->setDynamicSet(static_cast<int>(dynamicSet));
            dynamicSetChanged(static_cast<int>(dynamicSet));
        }
        updateDynamicColorControls();
        statusBar()->showMessage(QStringLiteral("Picked dynamic set %1 at %2,%3")
                                     .arg(static_cast<int>(dynamicSet))
                                     .arg(hit.x)
                                     .arg(hit.y),
                                 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::pickFrameColor(const render::CanvasHit& hit)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }
    try {
        const uint16_t color = m_document->colorizedPixel(*frameIndex,
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
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }

    try {
        const document::Rgb565Frame& frame =
            m_document->colorizedFrame(*frameIndex, hit.resolution);
        const uint16_t targetColor =
            m_document->colorizedPixel(*frameIndex, hit.resolution, hit.x, hit.y);
        if (targetColor == m_activeDrawColor) {
            return;
        }
        commitPixels(hit.resolution,
                     edit::floodFillMatching(frame.pixels,
                                             frame.size,
                                             render::Point{hit.x, hit.y},
                                             targetColor),
                     QStringLiteral("Flood filled area"));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::paintBackgroundImagePixel(const render::CanvasHit& hit)
{
    if (!m_document || !isBackgroundImageTargetActive()) {
        return;
    }
    commitBackgroundImagePixels(hit.resolution,
                                {render::Point{hit.x, hit.y}},
                                QStringLiteral("Painted background pixel at %1,%2")
                                    .arg(hit.x)
                                    .arg(hit.y));
}

void MainWindow::pickBackgroundImageColor(const render::CanvasHit& hit)
{
    if (!m_document || !isBackgroundImageTargetActive()) {
        return;
    }
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        return;
    }
    try {
        const uint16_t color =
            m_document->backgroundPixel(*backgroundIndex, hit.resolution, hit.x, hit.y);
        setActiveDrawColor(color);
        statusBar()->showMessage(QStringLiteral("Picked background color 0x%1")
                                     .arg(color, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::floodFillBackgroundImage(const render::CanvasHit& hit)
{
    if (!m_document || !isBackgroundImageTargetActive()) {
        return;
    }
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        return;
    }

    try {
        const document::Rgb565Frame& frame =
            m_document->backgroundFrame(*backgroundIndex, hit.resolution);
        const uint16_t targetColor =
            m_document->backgroundPixel(*backgroundIndex, hit.resolution, hit.x, hit.y);
        if (targetColor == m_activeDrawColor) {
            return;
        }
        commitBackgroundImagePixels(hit.resolution,
                                    edit::floodFillMatching(frame.pixels,
                                                            frame.size,
                                                            render::Point{hit.x, hit.y},
                                                            targetColor),
                                    QStringLiteral("Flood filled background"));
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
        commitMaskPixels(edit::floodFillMatching(mask.pixels,
                                                 mask.size,
                                                 render::Point{hit.x, hit.y},
                                                 targetValue),
                         newValue,
                         erase ? QStringLiteral("Flood erased comparison mask")
                               : QStringLiteral("Flood filled comparison mask"));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::paintDynamicMaskPixel(const render::CanvasHit& hit, bool erase)
{
    if (!m_document || !isDynamicMaskModeActive()) {
        return;
    }
    commitFrameMaskPixels(edit::EditKind::DynamicMask,
                          hit.resolution,
                          {render::Point{hit.x, hit.y}},
                          erase ? uint8_t{255} : activeDynamicSet(),
                          erase ? QStringLiteral("Erased dynamic mask pixel")
                                : QStringLiteral("Painted dynamic mask pixel"));
}

void MainWindow::floodFillDynamicMask(const render::CanvasHit& hit, bool erase)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !isDynamicMaskModeActive() || !frameIndex) {
        return;
    }
    try {
        const uint8_t targetValue =
            m_document->dynamicMaskPixel(*frameIndex, hit.resolution, hit.x, hit.y);
        const uint8_t newValue = erase ? uint8_t{255} : activeDynamicSet();
        if (targetValue == newValue) {
            return;
        }

        const document::IndexedFrame& mask =
            hit.resolution == render::Resolution::Hd
                ? m_document->dynamicData(*frameIndex)->maskHd
                : m_document->dynamicData(*frameIndex)->maskSd;
        commitFrameMaskPixels(edit::EditKind::DynamicMask,
                              hit.resolution,
                              edit::floodFillMatching(mask.pixels,
                                                      mask.size,
                                                      render::Point{hit.x, hit.y},
                                                      targetValue),
                              newValue,
                              erase ? QStringLiteral("Flood erased dynamic mask")
                                    : QStringLiteral("Flood filled dynamic mask"));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::paintBackgroundMaskPixel(const render::CanvasHit& hit, bool erase)
{
    if (!m_document || !isBackgroundMaskModeActive()) {
        return;
    }
    commitFrameMaskPixels(edit::EditKind::BackgroundMask,
                          hit.resolution,
                          {render::Point{hit.x, hit.y}},
                          erase ? uint8_t{0} : uint8_t{1},
                          erase ? QStringLiteral("Erased background mask pixel")
                                : QStringLiteral("Painted background mask pixel"));
}

void MainWindow::floodFillBackgroundMask(const render::CanvasHit& hit, bool erase)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !isBackgroundMaskModeActive() || !frameIndex) {
        return;
    }
    try {
        const uint8_t targetValue =
            m_document->backgroundMaskPixel(*frameIndex, hit.resolution, hit.x, hit.y);
        const uint8_t newValue = erase ? uint8_t{0} : uint8_t{1};
        if (targetValue == newValue) {
            return;
        }

        const std::optional<document::FrameBackgroundData>& backgroundData =
            m_document->backgroundData(*frameIndex);
        if (!backgroundData || backgroundData->backgroundId >= m_document->backgroundCount()) {
            statusBar()->showMessage(QStringLiteral("No background assigned"), 2500);
            return;
        }
        const document::IndexedFrame& mask =
            hit.resolution == render::Resolution::Hd ? backgroundData->maskHd : backgroundData->maskSd;
        commitFrameMaskPixels(edit::EditKind::BackgroundMask,
                              hit.resolution,
                              edit::floodFillMatching(mask.pixels,
                                                      mask.size,
                                                      render::Point{hit.x, hit.y},
                                                      targetValue),
                              newValue,
                              erase ? QStringLiteral("Flood erased background mask")
                                    : QStringLiteral("Flood filled background mask"));
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
    if (m_drawingTool == DrawingTool::Selection) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        m_preview->setPreviewPixels(render::CanvasRegion::ColorizedFrame,
                                    start.resolution,
                                    pixelsForStroke(start, end),
                                    QColor(0, 220, 180, 130));
        return;
    }
    if (m_drawingTool != DrawingTool::Selection && isCanvasSelectionDirectionModeActive()) {
        previewCanvasSelectionDirection(start, end);
        return;
    }
    if (isSpriteCaptureSelectionModeActive()) {
        if (start.region != render::CanvasRegion::OriginalFrame ||
            start.resolution != render::Resolution::Sd) {
            return;
        }
        m_preview->setPreviewPixels(render::CanvasRegion::OriginalFrame,
                                    render::Resolution::Sd,
                                    pixelsForStroke(start, end),
                                    erase ? QColor(0, 0, 0, 150)
                                          : QColor(255, 0, 255, 150));
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
    if (isDynamicMaskModeActive()) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        m_preview->setPreviewPixels(render::CanvasRegion::ColorizedFrame,
                                    start.resolution,
                                    pixelsForStroke(start, end),
                                    erase ? QColor(0, 0, 0, 150)
                                          : QColor(0, 180, 255, 150));
        return;
    }
    if (isBackgroundMaskModeActive()) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        m_preview->setPreviewPixels(render::CanvasRegion::ColorizedFrame,
                                    start.resolution,
                                    pixelsForStroke(start, end),
                                    erase ? QColor(0, 0, 0, 150)
                                          : QColor(255, 200, 0, 150));
        return;
    }
    if (isBackgroundImageTargetActive()) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        if (m_drawingTool == DrawingTool::PaletteGradient &&
            m_palettePanel->gradientDirection() == PaletteGradientDirection::Free &&
            isFreePaletteGradientPending()) {
            previewFreePaletteGradientDirection(start, end);
            return;
        }
        QColor previewColor = rgb565ToColor(m_activeDrawColor);
        previewColor.setAlpha(150);
        m_preview->setPreviewPixels(render::CanvasRegion::ColorizedFrame,
                                    start.resolution,
                                    pixelsForStroke(start, end),
                                    previewColor);
        return;
    }
    if (start.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    if (m_drawingTool == DrawingTool::PaletteGradient &&
        m_palettePanel->gradientDirection() == PaletteGradientDirection::Free &&
        isFreePaletteGradientPending()) {
        previewFreePaletteGradientDirection(start, end);
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
    } else if (m_drawingTool == DrawingTool::Selection) {
        statusText = QStringLiteral("Selected canvas area");
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
    } else if (m_drawingTool == DrawingTool::PaletteGradient) {
        statusText = QStringLiteral("Filled palette gradient");
    }
    const bool isFreePaletteGradient =
        m_drawingTool == DrawingTool::PaletteGradient &&
        m_palettePanel->gradientDirection() == PaletteGradientDirection::Free;
    if (m_drawingTool != DrawingTool::Selection && isCanvasSelectionDirectionModeActive()) {
        setCanvasSelectionDirection(start, end);
        return;
    }

    if (isSpriteCaptureSelectionModeActive()) {
        if (start.region != render::CanvasRegion::OriginalFrame ||
            start.resolution != render::Resolution::Sd) {
            return;
        }
        commitSpriteCaptureSelectionPixels(pixelsForStroke(start, end), erase);
        return;
    }
    if (isComparisonMaskModeActive()) {
        if (start.region != render::CanvasRegion::OriginalFrame) {
            return;
        }
        if (m_drawingTool == DrawingTool::PaletteGradient) {
            statusBar()->showMessage(QStringLiteral("Palette gradients edit RGB pixels only"), 2500);
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
    if (isDynamicMaskModeActive()) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        if (m_drawingTool == DrawingTool::PaletteGradient) {
            statusBar()->showMessage(QStringLiteral("Palette gradients edit RGB pixels only"), 2500);
            return;
        }
        commitFrameMaskPixels(edit::EditKind::DynamicMask,
                              start.resolution,
                              pixelsForStroke(start, end),
                              erase ? uint8_t{255} : activeDynamicSet(),
                              statusText.replace(
                                  QStringLiteral("Painted"),
                                  erase ? QStringLiteral("Erased dynamic mask")
                                        : QStringLiteral("Painted dynamic mask")));
        return;
    }
    if (isBackgroundMaskModeActive()) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        if (m_drawingTool == DrawingTool::PaletteGradient) {
            statusBar()->showMessage(QStringLiteral("Palette gradients edit RGB pixels only"), 2500);
            return;
        }
        commitFrameMaskPixels(edit::EditKind::BackgroundMask,
                              start.resolution,
                              pixelsForStroke(start, end),
                              erase ? uint8_t{0} : uint8_t{1},
                              statusText.replace(
                                  QStringLiteral("Painted"),
                                  erase ? QStringLiteral("Erased background mask")
                                        : QStringLiteral("Painted background mask")));
        return;
    }
    if (isBackgroundImageTargetActive()) {
        if (start.region != render::CanvasRegion::ColorizedFrame) {
            return;
        }
        if (m_drawingTool == DrawingTool::Selection) {
            setCanvasSelectionFromStroke(start, end);
            return;
        }
        if (m_drawingTool == DrawingTool::PaletteGradient) {
            if (isFreePaletteGradient) {
                if (!isFreePaletteGradientPending()) {
                    beginFreePaletteGradientSelection(start, end);
                    return;
                }
                if (!m_pendingFreePaletteGradient ||
                    m_pendingFreePaletteGradient->target != PaintTarget::Background ||
                    m_pendingFreePaletteGradient->backgroundIndex != selectedBackgroundIndex() ||
                    m_pendingFreePaletteGradient->shapeStart.region != start.region ||
                    m_pendingFreePaletteGradient->shapeStart.resolution != start.resolution) {
                    cancelFreePaletteGradientSelection();
                    statusBar()->showMessage(QStringLiteral("Free gradient selection cancelled"), 2500);
                    return;
                }
                commitBackgroundImageGradientPixels(
                    m_pendingFreePaletteGradient->shapeStart.resolution,
                    m_pendingFreePaletteGradient->shapeStart,
                    m_pendingFreePaletteGradient->shapeEnd,
                    start,
                    end,
                    statusText);
                cancelFreePaletteGradientSelection();
                return;
            }
            commitBackgroundImageGradientPixels(start.resolution,
                                                start,
                                                end,
                                                start,
                                                end,
                                                statusText);
            return;
        }
        commitBackgroundImagePixels(start.resolution,
                                    pixelsForStroke(start, end),
                                    statusText.replace(QStringLiteral("Painted"),
                                                       QStringLiteral("Painted background")));
        return;
    }

    if (start.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    if (m_drawingTool == DrawingTool::Selection) {
        setCanvasSelectionFromStroke(start, end);
        return;
    }
    if (m_drawingTool == DrawingTool::PaletteGradient) {
        if (isFreePaletteGradient) {
            if (!isFreePaletteGradientPending()) {
                beginFreePaletteGradientSelection(start, end);
                return;
            }
            if (!m_pendingFreePaletteGradient ||
                m_pendingFreePaletteGradient->target != PaintTarget::Frame ||
                m_pendingFreePaletteGradient->shapeStart.region != start.region ||
                m_pendingFreePaletteGradient->shapeStart.resolution != start.resolution) {
                cancelFreePaletteGradientSelection();
                statusBar()->showMessage(QStringLiteral("Free gradient selection cancelled"), 2500);
                return;
            }
            commitPaletteGradientPixels(m_pendingFreePaletteGradient->shapeStart.resolution,
                                        m_pendingFreePaletteGradient->shapeStart,
                                        m_pendingFreePaletteGradient->shapeEnd,
                                        start,
                                        end,
                                        statusText);
            cancelFreePaletteGradientSelection();
            return;
        }
        commitPaletteGradientPixels(start.resolution, start, end, start, end, statusText);
        return;
    }
    commitPixels(start.resolution, pixelsForStroke(start, end), statusText);
}

bool MainWindow::isFreePaletteGradientPending() const
{
    return m_pendingFreePaletteGradient.has_value();
}

void MainWindow::beginFreePaletteGradientSelection(const render::CanvasHit& start,
                                                   const render::CanvasHit& end)
{
    if (!m_preview) {
        return;
    }
    m_pendingFreePaletteGradient = PendingFreePaletteGradient{
        start,
        end,
        activePaintTarget(),
        selectedBackgroundIndex(),
    };
    showPendingFreePaletteGradientSelection();
    statusBar()->showMessage(QStringLiteral("Draw gradient direction line"), 2500);
}

void MainWindow::previewFreePaletteGradientDirection(const render::CanvasHit& start,
                                                     const render::CanvasHit& end)
{
    if (!m_preview || !m_pendingFreePaletteGradient) {
        return;
    }
    std::vector<render::Point> previewPixels = pixelsForStroke(
        m_pendingFreePaletteGradient->shapeStart,
        m_pendingFreePaletteGradient->shapeEnd);
    std::vector<render::Point> linePixels =
        edit::makeLinePixels(render::Point{start.x, start.y},
                             render::Point{end.x, end.y});
    previewPixels.insert(previewPixels.end(), linePixels.begin(), linePixels.end());
    QColor previewColor = rgb565ToColor(m_activeDrawColor);
    previewColor.setAlpha(145);
    m_preview->setPreviewPixels(m_pendingFreePaletteGradient->shapeStart.region,
                                m_pendingFreePaletteGradient->shapeStart.resolution,
                                previewPixels,
                                previewColor);
}

void MainWindow::showPendingFreePaletteGradientSelection()
{
    if (!m_preview || !m_pendingFreePaletteGradient) {
        return;
    }
    QColor previewColor = rgb565ToColor(m_activeDrawColor);
    previewColor.setAlpha(120);
    m_preview->setPreviewPixels(
        m_pendingFreePaletteGradient->shapeStart.region,
        m_pendingFreePaletteGradient->shapeStart.resolution,
        pixelsForStroke(m_pendingFreePaletteGradient->shapeStart,
                        m_pendingFreePaletteGradient->shapeEnd),
        previewColor);
}

void MainWindow::cancelFreePaletteGradientSelection()
{
    m_pendingFreePaletteGradient.reset();
    if (m_preview) {
        m_preview->clearPreviewPixels();
    }
}

void MainWindow::setCanvasSelectionFromStroke(const render::CanvasHit& start,
                                              const render::CanvasHit& end)
{
    m_canvasSelectionDirectionMode = false;
    if (m_palettePanel) {
        m_palettePanel->setSelectionDirectionChecked(false);
    }
    if (start.region != render::CanvasRegion::ColorizedFrame || start.resolution != end.resolution) {
        return;
    }
    std::vector<render::Point> pixels = pixelsForStroke(start, end);
    if (pixels.empty()) {
        clearCanvasSelection();
        return;
    }
    std::sort(pixels.begin(), pixels.end(), [](const render::Point& left, const render::Point& right) {
        return std::tie(left.y, left.x) < std::tie(right.y, right.x);
    });
    pixels.erase(std::unique(pixels.begin(),
                             pixels.end(),
                             [](const render::Point& left, const render::Point& right) {
                                 return left.x == right.x && left.y == right.y;
                             }),
                 pixels.end());
    m_canvasSelection = CanvasSelection{
        start,
        end,
        activePaintTarget(),
        selectedBackgroundIndex(),
        std::move(pixels),
        std::nullopt,
        std::nullopt,
    };
    updateCanvasSelectionOverlay();
    updatePaletteControls();
    statusBar()->showMessage(QStringLiteral("Selected canvas area"), 2000);
}

void MainWindow::toggleCanvasSelectionDirectionMode()
{
    if (!m_canvasSelection) {
        m_canvasSelectionDirectionMode = false;
        m_palettePanel->setSelectionDirectionChecked(false);
        updatePaletteControls();
        return;
    }
    m_canvasSelectionDirectionMode = !m_canvasSelectionDirectionMode;
    m_palettePanel->setSelectionDirectionChecked(m_canvasSelectionDirectionMode);
    if (m_canvasSelectionDirectionMode) {
        cancelFreePaletteGradientSelection();
        statusBar()->showMessage(QStringLiteral("Draw selection gradient direction line"), 2500);
    } else {
        updateCanvasSelectionOverlay();
    }
}

void MainWindow::clearCanvasSelection()
{
    m_canvasSelection.reset();
    m_canvasSelectionDirectionMode = false;
    if (m_palettePanel) {
        m_palettePanel->setSelectionDirectionChecked(false);
    }
    if (m_preview && !isSpriteCaptureSelectionModeActive()) {
        m_preview->clearSelectionOverlayPixels();
    }
    updatePaletteControls();
}

void MainWindow::updateCanvasSelectionOverlay()
{
    if (!m_preview || !m_canvasSelection) {
        return;
    }
    std::vector<render::Point> pixels = m_canvasSelection->pixels;
    if (m_canvasSelection->gradientStart && m_canvasSelection->gradientEnd) {
        std::vector<render::Point> linePixels =
            edit::makeLinePixels(render::Point{m_canvasSelection->gradientStart->x,
                                         m_canvasSelection->gradientStart->y},
                           render::Point{m_canvasSelection->gradientEnd->x,
                                         m_canvasSelection->gradientEnd->y});
        pixels.insert(pixels.end(), linePixels.begin(), linePixels.end());
    }
    m_preview->setSelectionOverlayPixels(m_canvasSelection->shapeStart.region,
                                         m_canvasSelection->shapeStart.resolution,
                                         pixels,
                                         QColor(0, 220, 180, 95));
}

bool MainWindow::isCanvasSelectionDirectionModeActive() const
{
    return m_canvasSelectionDirectionMode && m_canvasSelection.has_value();
}

void MainWindow::setCanvasSelectionDirection(const render::CanvasHit& start,
                                             const render::CanvasHit& end)
{
    if (!m_canvasSelection ||
        start.region != m_canvasSelection->shapeStart.region ||
        end.region != m_canvasSelection->shapeStart.region ||
        start.resolution != m_canvasSelection->shapeStart.resolution ||
        end.resolution != m_canvasSelection->shapeStart.resolution) {
        return;
    }
    m_canvasSelection->gradientStart = start;
    m_canvasSelection->gradientEnd = end;
    m_canvasSelectionDirectionMode = false;
    if (m_palettePanel) {
        m_palettePanel->setSelectionDirectionChecked(false);
    }
    updateCanvasSelectionOverlay();
    statusBar()->showMessage(QStringLiteral("Set selection gradient direction"), 2000);
}

void MainWindow::previewCanvasSelectionDirection(const render::CanvasHit& start,
                                                 const render::CanvasHit& end)
{
    if (!m_preview || !m_canvasSelection ||
        start.region != m_canvasSelection->shapeStart.region ||
        start.resolution != m_canvasSelection->shapeStart.resolution ||
        end.region != m_canvasSelection->shapeStart.region ||
        end.resolution != m_canvasSelection->shapeStart.resolution) {
        return;
    }
    std::vector<render::Point> pixels = m_canvasSelection->pixels;
    std::vector<render::Point> linePixels =
        edit::makeLinePixels(render::Point{start.x, start.y},
                             render::Point{end.x, end.y});
    pixels.insert(pixels.end(), linePixels.begin(), linePixels.end());
    m_preview->setSelectionOverlayPixels(m_canvasSelection->shapeStart.region,
                                         m_canvasSelection->shapeStart.resolution,
                                         pixels,
                                         QColor(0, 220, 180, 120));
}

void MainWindow::fillCanvasSelectionWithPaletteGradient()
{
    if (!m_document || !m_canvasSelection || m_canvasSelection->pixels.empty()) {
        statusBar()->showMessage(QStringLiteral("No canvas selection"), 2500);
        return;
    }
    if (m_palettePanel->gradientDirection() == PaletteGradientDirection::Free &&
        (!m_canvasSelection->gradientStart || !m_canvasSelection->gradientEnd)) {
        statusBar()->showMessage(QStringLiteral("Set a selection direction line first"), 3000);
        return;
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (!paletteIndex) {
        statusBar()->showMessage(QStringLiteral("No palette selected"), 2500);
        return;
    }
    if (m_canvasSelection->target == PaintTarget::Background &&
        m_canvasSelection->backgroundIndex != selectedBackgroundIndex()) {
        clearCanvasSelection();
        statusBar()->showMessage(QStringLiteral("Canvas selection cleared"), 2500);
        return;
    }

    const render::CanvasHit shapeStart = m_canvasSelection->shapeStart;
    const render::CanvasHit shapeEnd = m_canvasSelection->shapeEnd;
    const edit::PixelColorSource colorForPoint =
        paletteGradientColorSource(*paletteIndex,
                                   shapeStart,
                                   shapeEnd,
                                   m_canvasSelection->gradientStart.value_or(shapeStart),
                                   m_canvasSelection->gradientEnd.value_or(shapeEnd));

    try {
        if (m_canvasSelection->target == PaintTarget::Background) {
            const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
            if (!backgroundIndex) {
                return;
            }
            const edit::EditLayer layer{edit::EditKind::BackgroundImage,
                                        *backgroundIndex,
                                        shapeStart.resolution};
            if (!commitPixelChanges(layer,
                                    edit::makePixelChanges(*m_document,
                                                           layer,
                                                           m_canvasSelection->pixels,
                                                           colorForPoint))) {
                return;
            }
        } else {
            bool changed = false;
            for (std::size_t frameIndex : selectedFrameIndices()) {
                if (shapeStart.resolution == render::Resolution::Hd &&
                    !m_document->hasHdFrame(frameIndex)) {
                    continue;
                }
                const edit::EditLayer layer{edit::EditKind::FramePixels,
                                            frameIndex,
                                            shapeStart.resolution};
                if (commitPixelChanges(layer,
                                       edit::makePixelChanges(*m_document,
                                                              layer,
                                                              m_canvasSelection->pixels,
                                                              colorForPoint))) {
                    changed = true;
                }
            }
            if (!changed) {
                return;
            }
        }
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Filled selection gradient"), 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

std::vector<render::Point> MainWindow::pixelsForStroke(const render::CanvasHit& start,
                                                       const render::CanvasHit& end) const
{
    const render::Point startPoint{start.x, start.y};
    const render::Point endPoint{end.x, end.y};
    if (m_drawingTool == DrawingTool::Line) {
        return edit::makeLinePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::Rectangle) {
        return edit::makeRectanglePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::FilledRectangle) {
        return edit::makeFilledRectanglePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::Circle) {
        return edit::makeCirclePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::FilledCircle) {
        return edit::makeFilledCirclePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::Ellipse) {
        return edit::makeEllipsePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::FilledEllipse) {
        return edit::makeFilledEllipsePixels(startPoint, endPoint);
    }
    if (m_drawingTool == DrawingTool::PaletteGradient || m_drawingTool == DrawingTool::Selection) {
        switch (m_palettePanel->gradientShape()) {
            case PaletteGradientShape::Circle:
                return edit::makeFilledCirclePixels(startPoint, endPoint);
            case PaletteGradientShape::Ellipse:
                return edit::makeFilledEllipsePixels(startPoint, endPoint);
            case PaletteGradientShape::Rectangle:
                break;
        }
        return edit::makeFilledRectanglePixels(startPoint, endPoint);
    }
    return {};
}

edit::PixelColorSource MainWindow::paletteGradientColorSource(
    std::size_t paletteIndex,
    const render::CanvasHit& shapeStart,
    const render::CanvasHit& shapeEnd,
    const render::CanvasHit& gradientStart,
    const render::CanvasHit& gradientEnd) const
{
    const auto [startIndex, endIndex] = m_palettePanel->gradientEndpoints();
    const edit::PaletteGradient gradient{
        render::Point{shapeStart.x, shapeStart.y},
        render::Point{shapeEnd.x, shapeEnd.y},
        render::Point{gradientStart.x, gradientStart.y},
        render::Point{gradientEnd.x, gradientEnd.y},
        m_palettePanel->gradientDirection(),
        m_palettePanel->gradientPattern(),
        startIndex,
        endIndex,
    };
    return [this, paletteIndex, gradient](render::Point point) {
        return m_document->paletteColor(paletteIndex,
                                        edit::paletteGradientColorIndex(gradient, point));
    };
}

bool MainWindow::commitPixelChanges(const edit::EditLayer& layer,
                                    std::vector<edit::PixelChange> changes)
{
    if (changes.empty()) {
        return false;
    }
    commitEdit(std::make_unique<edit::PixelEditCommand>(layer.kind,
                                                        layer.index,
                                                        layer.resolution,
                                                        std::move(changes)));
    return true;
}

bool MainWindow::commitMaskChanges(const edit::EditLayer& layer,
                                   std::vector<edit::MaskPixelChange> changes)
{
    if (changes.empty()) {
        return false;
    }
    commitEdit(std::make_unique<edit::MaskEditCommand>(layer.kind,
                                                       layer.index,
                                                       layer.resolution,
                                                       std::move(changes)));
    return true;
}

void MainWindow::commitPixels(render::Resolution resolution,
                              const std::vector<render::Point>& pixels,
                              const QString& statusText)
{
    const uint16_t drawColor = m_activeDrawColor;
    commitFramePixelColors(resolution,
                           pixels,
                           [drawColor](render::Point) { return drawColor; },
                           statusText);
}

void MainWindow::commitPaletteGradientPixels(render::Resolution resolution,
                                             const render::CanvasHit& start,
                                             const render::CanvasHit& end,
                                             const render::CanvasHit& gradientStart,
                                             const render::CanvasHit& gradientEnd,
                                             const QString& statusText)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    const std::vector<render::Point> pixels = pixelsForStroke(start, end);
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (!m_document || frameIndices.empty() || pixels.empty() || !paletteIndex ||
        !m_palettePanel) {
        statusBar()->showMessage(QStringLiteral("No palette gradient source selected"), 2500);
        return;
    }
    commitFramePixelColors(
        resolution,
        pixels,
        paletteGradientColorSource(*paletteIndex, start, end, gradientStart, gradientEnd),
        statusText);
}

void MainWindow::commitFramePixelColors(render::Resolution resolution,
                                        const std::vector<render::Point>& pixels,
                                        const edit::PixelColorSource& colorForPoint,
                                        const QString& statusText)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || pixels.empty()) {
        return;
    }

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            if (resolution == render::Resolution::Hd && !m_document->hasHdFrame(frameIndex)) {
                continue;
            }
            const edit::EditLayer layer{edit::EditKind::FramePixels, frameIndex, resolution};
            if (commitPixelChanges(layer,
                                   edit::makePixelChanges(*m_document,
                                                          layer,
                                                          pixels,
                                                          colorForPoint))) {
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(
        frameIndices.size() > 1
            ? QStringLiteral("%1 on %2 frame(s)").arg(statusText).arg(static_cast<int>(frameIndices.size()))
            : statusText,
        2500);
}

void MainWindow::commitMaskPixels(const std::vector<render::Point>& pixels,
                                  uint8_t value,
                                  const QString& statusText)
{
    if (!m_document || pixels.empty()) {
        return;
    }
    std::set<std::size_t> maskIndices;
    for (std::size_t frameIndex : selectedFrameIndices()) {
        const uint8_t maskId = m_document->comparisonMaskId(frameIndex);
        if (maskId < m_document->comparisonMaskCount()) {
            maskIndices.insert(static_cast<std::size_t>(maskId));
        }
    }
    if (maskIndices.empty()) {
        statusBar()->showMessage(QStringLiteral("No comparison mask assigned"), 2500);
        return;
    }

    try {
        bool changed = false;
        for (std::size_t maskIndex : maskIndices) {
            const edit::EditLayer layer{edit::EditKind::ComparisonMask,
                                        maskIndex,
                                        render::Resolution::Sd};
            if (commitMaskChanges(layer,
                                  edit::makeMaskChanges(*m_document, layer, pixels, value))) {
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(
        maskIndices.size() > 1
            ? QStringLiteral("%1 on %2 mask(s)").arg(statusText).arg(static_cast<int>(maskIndices.size()))
            : statusText,
        2500);
}

void MainWindow::commitFrameMaskPixels(edit::EditKind kind,
                                       render::Resolution resolution,
                                       const std::vector<render::Point>& pixels,
                                       uint8_t value,
                                       const QString& statusText)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || pixels.empty()) {
        return;
    }
    const bool isBackgroundMask = kind == edit::EditKind::BackgroundMask;

    try {
        bool changed = false;
        bool hasAssignedBackground = false;
        for (std::size_t frameIndex : frameIndices) {
            if (isBackgroundMask) {
                const std::optional<document::FrameBackgroundData>& backgroundData =
                    m_document->backgroundData(frameIndex);
                if (!backgroundData ||
                    backgroundData->backgroundId >= m_document->backgroundCount()) {
                    continue;
                }
                hasAssignedBackground = true;
            } else if (!m_document->dynamicData(frameIndex)) {
                continue;
            }
            const edit::EditLayer layer{kind, frameIndex, resolution};
            if (commitMaskChanges(layer,
                                  edit::makeMaskChanges(*m_document, layer, pixels, value))) {
                changed = true;
            }
        }
        if (!changed) {
            if (isBackgroundMask && !hasAssignedBackground) {
                statusBar()->showMessage(
                    QStringLiteral("No selected frame has an assigned background"),
                    2500);
            }
            return;
        }
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(
        frameIndices.size() > 1
            ? QStringLiteral("%1 on %2 frame(s)").arg(statusText).arg(static_cast<int>(frameIndices.size()))
            : statusText,
        2500);
}

void MainWindow::commitBackgroundImagePixels(render::Resolution resolution,
                                             const std::vector<render::Point>& pixels,
                                             const QString& statusText)
{
    if (!m_document || pixels.empty()) {
        return;
    }
    const uint16_t drawColor = m_activeDrawColor;
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        statusBar()->showMessage(QStringLiteral("No background selected"), 2500);
        return;
    }
    commitBackgroundImageColors(*backgroundIndex,
                                resolution,
                                pixels,
                                [drawColor](render::Point) { return drawColor; },
                                statusText);
}

void MainWindow::commitBackgroundImageGradientPixels(render::Resolution resolution,
                                                     const render::CanvasHit& start,
                                                     const render::CanvasHit& end,
                                                     const render::CanvasHit& gradientStart,
                                                     const render::CanvasHit& gradientEnd,
                                                     const QString& statusText)
{
    if (!m_document || !m_palettePanel) {
        return;
    }
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    const std::vector<render::Point> pixels = pixelsForStroke(start, end);
    if (!backgroundIndex || !paletteIndex || pixels.empty()) {
        statusBar()->showMessage(QStringLiteral("No background or palette selected"), 2500);
        return;
    }
    commitBackgroundImageColors(
        *backgroundIndex,
        resolution,
        pixels,
        paletteGradientColorSource(*paletteIndex, start, end, gradientStart, gradientEnd),
        statusText);
}

void MainWindow::commitBackgroundImageColors(std::size_t backgroundIndex,
                                             render::Resolution resolution,
                                             const std::vector<render::Point>& pixels,
                                             const edit::PixelColorSource& colorForPoint,
                                             const QString& statusText)
{
    try {
        const edit::EditLayer layer{edit::EditKind::BackgroundImage, backgroundIndex, resolution};
        if (!commitPixelChanges(layer,
                                edit::makePixelChanges(*m_document,
                                                       layer,
                                                       pixels,
                                                       colorForPoint))) {
            return;
        }
        updateUndoRedoActions();
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }

    statusBar()->showMessage(statusText, 2500);
}

void MainWindow::commitEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (!m_document) {
        return;
    }
    command->apply(*m_document);
    const edit::EditTarget target = command->target();
    refreshAfterEdit(target);
    m_undoHistories.push(edit::historyKey(target), std::move(command));
}

void MainWindow::refreshAfterEdit(const edit::EditTarget& target)
{
    if (!m_document) {
        return;
    }
    switch (target.kind) {
        case edit::EditKind::FramePixels:
            m_preview->update();
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameThumbnail(frameIndex);
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::ComparisonMask:
        case edit::EditKind::DynamicMask:
        case edit::EditKind::BackgroundMask:
            m_preview->update();
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::DynamicColor:
            updateDynamicColorControls();
            m_preview->update();
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameThumbnail(frameIndex);
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::DynamicShadow:
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameThumbnail(frameIndex);
            }
            updateDynamicShadowControls();
            m_preview->update();
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::BackgroundImage:
            m_preview->update();
            updateBackgroundControls();
            for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
                const std::optional<document::FrameBackgroundData>& backgroundData =
                    m_document->backgroundData(frameIndex);
                if (backgroundData && backgroundData->backgroundId == target.index) {
                    updateFrameThumbnail(frameIndex);
                }
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::SpriteImage:
            setSpriteDependentViewsDirty();
            if (m_spriteEditor) {
                m_spriteEditor->update();
            }
            break;
        case edit::EditKind::Palette:
            refreshComponentBrowsers();
            updateProjectHeaderStatus();
            break;
    }
}

std::vector<edit::HistoryKey> MainWindow::currentHistoryKeys() const
{
    std::vector<edit::HistoryKey> keys;
    if (isSpriteCanvasActive()) {
        const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
        if (spriteIndex) {
            render::Resolution resolution = m_activeResolution;
            if (m_document) {
                const document::SpriteRecord& sprite = m_document->sprite(*spriteIndex);
                if (resolution == render::Resolution::Hd && !sprite.hasHd) {
                    resolution = render::Resolution::Sd;
                }
            }
            keys.push_back({edit::EditKind::SpriteImage, *spriteIndex, resolution});
        }
        return keys;
    }
    if (isComparisonMaskModeActive()) {
        const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
        if (maskIndex) {
            keys.push_back({edit::EditKind::ComparisonMask, *maskIndex, render::Resolution::Sd});
        }
        return keys;
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (isDynamicMaskModeActive()) {
        if (frameIndex) {
            keys.push_back({edit::EditKind::DynamicMask, *frameIndex, m_activeResolution});
            keys.push_back({edit::EditKind::DynamicColor, *frameIndex, m_activeResolution});
            keys.push_back({edit::EditKind::DynamicShadow, *frameIndex, m_activeResolution});
        }
        return keys;
    }
    if (isBackgroundMaskModeActive()) {
        if (frameIndex) {
            keys.push_back({edit::EditKind::BackgroundMask, *frameIndex, m_activeResolution});
        }
        return keys;
    }
    if (isBackgroundImageTargetActive()) {
        const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
        if (backgroundIndex) {
            keys.push_back(
                {edit::EditKind::BackgroundImage, *backgroundIndex, m_activeResolution});
        }
        return keys;
    }
    if (frameIndex) {
        keys.push_back({edit::EditKind::FramePixels, *frameIndex, m_activeResolution});
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (paletteIndex) {
        keys.push_back({edit::EditKind::Palette, *paletteIndex, render::Resolution::Sd});
    }
    if (frameIndex) {
        keys.push_back({edit::EditKind::DynamicColor, *frameIndex, m_activeResolution});
        keys.push_back({edit::EditKind::DynamicShadow, *frameIndex, m_activeResolution});
    }
    return keys;
}

std::optional<std::size_t> MainWindow::currentComparisonMaskIndex() const
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return std::nullopt;
    }
    const uint8_t maskId = m_document->comparisonMaskId(*frameIndex);
    if (maskId >= m_document->comparisonMaskCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(maskId);
}

bool MainWindow::isComparisonMaskModeActive() const
{
    return m_comparisonMaskOverlayButton && m_comparisonMaskOverlayButton->isChecked();
}

bool MainWindow::isDynamicMaskModeActive() const
{
    return m_dynamicMaskOverlayButton && m_dynamicMaskOverlayButton->isChecked();
}

bool MainWindow::isBackgroundMaskModeActive() const
{
    return m_backgroundMaskOverlayButton && m_backgroundMaskOverlayButton->isChecked();
}

std::optional<std::size_t> MainWindow::selectedPaletteIndex() const
{
    if (!m_palettePanel) {
        return std::nullopt;
    }
    return m_palettePanel->selectedPaletteIndex(m_document ? &*m_document : nullptr);
}

std::optional<std::size_t> MainWindow::selectedBackgroundIndex() const
{
    if (!m_document || !m_backgroundComboBox) {
        return std::nullopt;
    }
    const int backgroundIndex = m_backgroundComboBox->currentData().toInt();
    if (backgroundIndex < 0 ||
        static_cast<std::size_t>(backgroundIndex) >= m_document->backgroundCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(backgroundIndex);
}

std::optional<std::size_t> MainWindow::currentBackgroundIndex() const
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return std::nullopt;
    }
    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(*frameIndex);
    if (!backgroundData || backgroundData->backgroundId >= m_document->backgroundCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(backgroundData->backgroundId);
}

bool MainWindow::isBackgroundImageTargetActive() const
{
    return activePaintTarget() == PaintTarget::Background && selectedBackgroundIndex().has_value();
}

std::optional<std::size_t> MainWindow::currentSpriteIndex() const
{
    if (!m_document) {
        return std::nullopt;
    }
    const int spriteIndex = m_selectedSpriteIndex;
    if (spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(spriteIndex);
}

bool MainWindow::isSpriteCanvasActive() const
{
    return m_spritePanel && m_spritePanel->isSpriteCanvasChecked() &&
           currentSpriteIndex().has_value();
}

void MainWindow::undo()
{
    if (!m_document) {
        return;
    }
    for (const edit::HistoryKey& key : currentHistoryKeys()) {
        const edit::EditCommand* command = nullptr;
        try {
            command = m_undoHistories.undo(key, *m_document);
        } catch (const std::exception& ex) {
            // The edited data no longer exists in that shape, e.g. its HD frame was removed.
            updateUndoRedoActions();
            statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
            return;
        }
        if (!command) {
            continue;
        }
        refreshAfterEdit(command->target());
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo") + editKindStatusSuffix(key.kind), 1500);
        return;
    }
}

void MainWindow::redo()
{
    if (!m_document) {
        return;
    }
    for (const edit::HistoryKey& key : currentHistoryKeys()) {
        const edit::EditCommand* command = nullptr;
        try {
            command = m_undoHistories.redo(key, *m_document);
        } catch (const std::exception& ex) {
            // The edited data no longer exists in that shape, e.g. its HD frame was removed.
            updateUndoRedoActions();
            statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
            return;
        }
        if (!command) {
            continue;
        }
        refreshAfterEdit(command->target());
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo") + editKindStatusSuffix(key.kind), 1500);
        return;
    }
}

void MainWindow::updateUndoRedoActions()
{
    bool canUndo = false;
    bool canRedo = false;
    for (const edit::HistoryKey& key : currentHistoryKeys()) {
        canUndo = canUndo || m_undoHistories.canUndo(key);
        canRedo = canRedo || m_undoHistories.canRedo(key);
    }
    if (m_undoAction) {
        m_undoAction->setEnabled(canUndo);
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(canRedo);
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
    setStatusText(QStringLiteral("%1: %2 frames, %3 HD, %4 backgrounds, %5 sprites%6")
                      .arg(QString::fromStdString(m_document->romName()))
                      .arg(static_cast<int>(m_document->frameCount()))
                      .arg(hdCount)
                      .arg(static_cast<int>(m_document->backgroundCount()))
                      .arg(static_cast<int>(m_document->spriteCount()))
                      .arg(m_document->isDirty() ? QStringLiteral(" (modified)") : QString()));
}

void MainWindow::updateFrameThumbnail(std::size_t frameIndex)
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return;
    }
    for (int row = 0; row < m_frameList->count(); ++row) {
        QListWidgetItem* item = m_frameList->item(row);
        if (item && item->data(Qt::UserRole).toULongLong() == frameIndex) {
            item->setIcon(makeFrameThumbnail(*m_document, frameIndex, QSize(160, 84)));
            return;
        }
    }
}

void MainWindow::updateFrameListItemText(std::size_t frameIndex)
{
    if (!m_document || !m_frameList || frameIndex >= m_document->frameCount()) {
        return;
    }
    if (!frameMatchesFilter(frameIndex)) {
        refreshFrameList(currentFrameIndex());
        return;
    }
    for (int row = 0; row < m_frameList->count(); ++row) {
        QListWidgetItem* item = m_frameList->item(row);
        if (item && item->data(Qt::UserRole).toULongLong() == frameIndex) {
            item->setText(frameListItemText(frameIndex));
            return;
        }
    }
    refreshFrameList(frameIndex);
}

void MainWindow::updateFrameListItem(std::size_t frameIndex)
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return;
    }
    if (!frameMatchesFilter(frameIndex)) {
        refreshFrameList(currentFrameIndex());
        return;
    }
    for (int row = 0; row < m_frameList->count(); ++row) {
        QListWidgetItem* item = m_frameList->item(row);
        if (!item || item->data(Qt::UserRole).toULongLong() != frameIndex) {
            continue;
        }
        item->setText(frameListItemText(frameIndex));
        item->setIcon(makeFrameThumbnail(*m_document, frameIndex, QSize(160, 84)));
        return;
    }
    refreshFrameList(frameIndex);
}

DmdRenderOptions MainWindow::currentDmdRenderOptions() const
{
    return m_framePanel ? m_framePanel->dmdRenderOptions() : DmdRenderOptions{};
}

void MainWindow::updateDmdRenderOptions()
{
    if (m_preview) {
        m_preview->setDmdRenderOptions(currentDmdRenderOptions());
    }
    if (m_spriteEditor) {
        m_spriteEditor->setDmdRenderOptions(currentDmdRenderOptions());
    }
}

}  // namespace colorizing::app
