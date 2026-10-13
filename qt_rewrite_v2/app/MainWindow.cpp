#include "MainWindow.h"

#include <algorithm>
#include <functional>

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
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

#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "LazyListIcons.h"
#include "PlaneLabels.h"
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
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::app {

namespace {

constexpr int kMaxRecentProjects = 8;
constexpr char kRecentProjectsSettingsKey[] = "recentProjects";
constexpr int kExportDmdPixelScale = 8;
constexpr QSize kFrameThumbnailSize{160, 84};
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

// What "inactive" on a frame means, see legacy::isFrameSkippedAtRuntime().
const QString kInactiveFrameToolTip = QStringLiteral(
    "inactive: shorter than 15 ms and not colorized \u2014 the runtime skips it");

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

bool isCrzPath(const QString& path)
{
    return path.endsWith(QStringLiteral(".cRZ"), Qt::CaseInsensitive);
}

// The file beside `path` with the same base name and the given extension, in whatever case it
// is spelled on disk; if there is none, the path with the extension as given.
QString siblingProjectFile(const QString& path, const QString& extension)
{
    const QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    const QFileInfoList files = info.dir().entryInfoList(QDir::Files | QDir::Hidden);
    for (const QFileInfo& file : files) {
        if (file.completeBaseName() == baseName &&
            file.suffix().compare(extension, Qt::CaseInsensitive) == 0) {
            return file.absoluteFilePath();
        }
    }
    return info.dir().absoluteFilePath(baseName + QChar('.') + extension);
}

QString editKindStatusSuffix(edit::EditKind kind, bool showsHdAndSd)
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
        case edit::EditKind::FrameAttributes:
            return QStringLiteral(" frame change");
        case edit::EditKind::SpriteAttributes:
            return showsHdAndSd ? QStringLiteral(" sprite HD change")
                                : QStringLiteral(" sprite change");
    }
    return QString();
}

// "3 frame(s)" or, when frames could not take the edit, "3 frame(s), 1 skipped (reason)".
QString frameEditCountText(int changed, int skipped, const QString& skipReason = QString())
{
    QString text = QStringLiteral("%1 frame(s)").arg(changed);
    if (skipped > 0) {
        text += QStringLiteral(", %1 skipped").arg(skipped);
        if (!skipReason.isEmpty()) {
            text += QStringLiteral(" (%1)").arg(skipReason);
        }
    }
    return text;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_canvasEdit(canvasEditOutputs())
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
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

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
    exportFrameAction->setToolTip(QStringLiteral("Export the current frame as libserum renders it"));
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

    m_runtimeViewAction = viewMenu->addAction(QStringLiteral("&Runtime View"));
    // What the toolbar button of the action shows.
    m_runtimeViewAction->setIconText(QStringLiteral("Runtime"));
    m_runtimeViewAction->setCheckable(true);
    m_runtimeViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_runtimeViewAction->setShortcutContext(Qt::ApplicationShortcut);
    m_runtimeViewAction->setToolTip(
        QStringLiteral("Show the current frame as libserum renders it; editing is off meanwhile"));
    connect(m_runtimeViewAction, &QAction::toggled, this, [this](bool checked) {
        setRuntimeViewEnabled(checked);
    });

    auto* rowsGroup = new QActionGroup(this);
    viewMenu->addSeparator();
    m_runtimeRows32Action = viewMenu->addAction(QStringLiteral("Runtime Plane: &32 Rows"));
    m_runtimeRows32Action->setCheckable(true);
    m_runtimeRows32Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    m_runtimeRows32Action->setShortcutContext(Qt::ApplicationShortcut);
    m_runtimeRows32Action->setToolTip(QStringLiteral("What a display with 32 rows gets"));
    m_runtimeRows64Action = viewMenu->addAction(QStringLiteral("Runtime Plane: &64 Rows"));
    m_runtimeRows64Action->setCheckable(true);
    m_runtimeRows64Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_6));
    m_runtimeRows64Action->setShortcutContext(Qt::ApplicationShortcut);
    m_runtimeRows64Action->setToolTip(QStringLiteral("What a display with 64 rows gets"));
    rowsGroup->addAction(m_runtimeRows32Action);
    rowsGroup->addAction(m_runtimeRows64Action);
    connect(m_runtimeRows32Action, &QAction::triggered, this, [this]() {
        setRuntimeViewRows(runtime::PlaneRequest::Rows32);
    });
    connect(m_runtimeRows64Action, &QAction::triggered, this, [this]() {
        setRuntimeViewRows(runtime::PlaneRequest::Rows64);
    });
    m_runtimeRows32Action->setChecked(true);
    m_runtimeRows32Action->setEnabled(false);
    m_runtimeRows64Action->setEnabled(false);
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
    m_frameFilterComboBox->addItem(QStringLiteral("Inactive"),
                                   static_cast<int>(FrameFilter::Inactive));
    m_frameFilterComboBox->setItemData(m_frameFilterComboBox->count() - 1,
                                       kInactiveFrameToolTip,
                                       Qt::ToolTipRole);
    m_frameFilterComboBox->setEnabled(false);
    m_frameList = new QListWidget(central);
    m_frameList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frameList->setIconSize(kFrameThumbnailSize);
    m_frameList->setResizeMode(QListView::Adjust);
    m_frameList->setUniformItemSizes(false);
    m_frameList->setMinimumWidth(230);
    m_frameThumbnails = new LazyListIcons(
        m_frameList,
        makeFrameThumbnailPlaceholder(kFrameThumbnailSize),
        [this](const QListWidgetItem& item) {
            const std::size_t frameIndex = item.data(Qt::UserRole).toULongLong();
            return m_document && frameIndex < m_document->frameCount()
                       ? makeFrameThumbnail(*m_document, frameIndex, kFrameThumbnailSize)
                       : QIcon();
        });
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
    m_preview->setRuntimeView(&m_runtimeView);
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
        m_canvasEdit.cancelFreeGradient();
        updatePaletteControls();
    };
    m_palettePanel->onGradientSettingsChanged = [this]() {
        m_canvasEdit.cancelFreeGradient();
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
        m_canvasEdit.fillSelectionWithPaletteGradient(canvasEditContext());
    };
    m_palettePanel->onSelectionDirectionClicked = [this]() {
        m_canvasEdit.toggleSelectionDirectionMode();
    };
    m_palettePanel->onClearSelectionRequested = [this]() {
        m_canvasEdit.clearSelection(canvasEditContext());
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
        m_canvasEdit.showCaptureSelection(currentFrameIndex());
        updateSpriteControls();
    };
    m_spritePanel->onClearCaptureSelectionRequested = [this]() {
        m_canvasEdit.clearCaptureSelection();
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
        } else if (m_runtimeViewAction) {
            // The sprite canvas takes the place of the frame canvas the view is shown on.
            m_runtimeViewAction->setChecked(false);
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
    // The same action as View -> Runtime View, where it can be seen.
    m_runtimeViewButton = new QToolButton(central);
    m_runtimeViewButton->setDefaultAction(m_runtimeViewAction);
    toolbarLayout->addWidget(m_runtimeViewButton);
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
            m_selectedMaskIndex = static_cast<std::size_t>(maskIndex);
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
        m_canvasEdit.cancelFreeGradient();
        m_canvasEdit.clearSelection(canvasEditContext());
        m_activeResolution = render::Resolution::Sd;
        updateResolutionMode();
    });
    connect(m_hdButton, &QToolButton::clicked, this, [this]() {
        m_canvasEdit.cancelFreeGradient();
        m_canvasEdit.clearSelection(canvasEditContext());
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
    m_runtimeRotationTimer = new QTimer(this);
    m_runtimeRotationTimer->setSingleShot(true);
    connect(m_runtimeRotationTimer, &QTimer::timeout, this, &MainWindow::stepRuntimeRotations);
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
                m_canvasEdit.cancelFreeGradient();
                m_canvasEdit.clearSelection(canvasEditContext());
                updateBackgroundControls();
                updateUndoRedoActions();
            });
    m_preview->setPixelClickedHandler([this](render::CanvasHit hit, bool erase) {
        m_canvasEdit.colorizedPixelClicked(canvasEditContext(), hit, erase);
    });
    m_preview->setOriginalClickedHandler([this](render::CanvasHit hit, bool erase) {
        m_canvasEdit.originalPixelClicked(canvasEditContext(), hit, erase);
    });
    m_preview->setStrokePreviewHandler(
        [this](render::CanvasHit start, render::CanvasHit end, bool erase) {
            m_canvasEdit.strokePreviewed(canvasEditContext(), start, end, erase);
    });
    m_preview->setStrokeFinishedHandler(
        [this](render::CanvasHit start, render::CanvasHit end, bool erase) {
            m_canvasEdit.strokeFinished(canvasEditContext(), start, end, erase);
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
}

void MainWindow::openProject()
{
    const QString cromPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open cROM or cRZ"),
        QString(),
        QStringLiteral("Serum colorization (*.cROM *.cRom *.crom *.cRZ *.crz *.CRZ);;"
                       "All files (*)"));
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
    QString cromPath = recentProjectCromPath(entry);
    const QString crpPath = recentProjectCrpPath(entry);
    if (cromPath.isEmpty() || crpPath.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Recent project entry is incomplete"), 4000);
        return;
    }
    // A project is passed around as .cRZ and saved as .cROM plus .cRZ, so either may be the one
    // that is still there.
    if (!QFileInfo::exists(cromPath)) {
        const QString other = siblingProjectFile(
            cromPath, isCrzPath(cromPath) ? QStringLiteral("cROM") : QStringLiteral("cRZ"));
        if (QFileInfo::exists(other)) {
            cromPath = other;
        }
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
        siblingProjectFile(m_currentCromPath, QStringLiteral("cRZ")).toStdString(),
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
    // A still of its own; a playing sequence would be replaced by it.
    setPlaybackRunning(false);

    // The picture is libserum's, as a machine shows the frame: the plane the Runtime view
    // shows, or the one the editing mode is on.
    const runtime::PlaneRequest wanted =
        m_runtimeView.isEnabled() ? m_runtimeView.rows()
        : m_activeResolution == render::Resolution::Hd ? runtime::PlaneRequest::Rows64
                                                        : runtime::PlaneRequest::Rows32;
    const runtime::PlaneRequest rows =
        runtime::effectivePlaneRequest(m_document->geometry(), wanted);
    const runtime::RuntimeFrame frame =
        runtime::RuntimePreview::instance().renderFrame(*m_document, *frameIndex, rows);
    const runtime::RuntimePlane* plane = frame.ok() ? frame.plane(rows) : nullptr;
    if (!plane) {
        // Nothing is written from the editor's own renderer instead: the file would show
        // what no machine shows.
        statusBar()->showMessage(
            QStringLiteral("Frame %1 is not exported: %2")
                .arg(static_cast<int>(*frameIndex))
                .arg(QString::fromStdString(runtimeNoPictureReason(frame, rows))),
            8000);
        return;
    }

    const render::Resolution exportPlane =
        m_document->geometry().layout() != render::PlaneLayout::Ordinary
            ? m_document->geometry().nativePlane()
            : rows == runtime::PlaneRequest::Rows64 ? render::Resolution::Hd
                                                    : render::Resolution::Sd;
    QFileInfo currentCromInfo(m_currentCromPath);
    const QString defaultPath = currentCromInfo.dir().absoluteFilePath(
        QStringLiteral("%1_frame_%2_%3.png")
            .arg(currentCromInfo.completeBaseName())
            .arg(static_cast<int>(*frameIndex), 4, 10, QLatin1Char('0'))
            .arg(planeName(exportPlane).toLower()));
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
    const QImage image =
        renderRgb565FrameImage(plane->image, kExportDmdPixelScale, currentDmdRenderOptions());
    if (!image.save(targetPath, "PNG")) {
        QMessageBox::critical(this,
                              QStringLiteral("Export Failed"),
                              QStringLiteral("Could not write PNG file."));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported frame PNG as libserum renders it, %1 rows")
                                 .arg(plane->image.size.height),
                             3000);
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
    const render::Size targetSize = m_document->geometry().planeSize(targetResolution);
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
            skippedFrames == 0
                ? QStringLiteral("Selected frames already match the imported image")
                : showsHdAndSd()
                      ? QStringLiteral("No selected HD frames were available for import")
                      : QStringLiteral("No selected frame has a %1 frame to import into")
                            .arg(nativePlaneText(*m_document)),
            3000);
        updateUndoRedoActions();
        return;
    }

    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(
        QStringLiteral("Imported image into %1 frame(s)%2")
            .arg(updatedFrames)
            .arg(skippedFrames == 0
                     ? QString()
                     : showsHdAndSd()
                           ? QStringLiteral(" (%1 skipped without HD)").arg(skippedFrames)
                           : QStringLiteral(" (%1 skipped without %2 frame)")
                                 .arg(skippedFrames)
                                 .arg(nativePlaneText(*m_document))),
        3000);
}

void MainWindow::loadProject(const QString& cromPath, const QString& crpPath)
{
    const legacy::LegacyProjectFileReader reader;
    const legacy::LegacyLoadResult result = reader.load(legacy::LegacyProjectPaths{
        cromPath.toStdString(),
        crpPath.toStdString(),
        {},
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
        // Saving a project opened from its archive writes the loose .cROM next to it.
        m_currentCromPath = isCrzPath(cromPath)
                                ? siblingProjectFile(cromPath, QStringLiteral("cROM"))
                                : cromPath;
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
        if (text.isEmpty()) {
            m_frameStatusLabel->setToolTip(QString());
        }
    }
}

void MainWindow::refreshProjectSummary()
{
    m_spriteDependentViewsDirty = false;
    m_canvasEdit.resetCaptureSelection();
    if (m_spritePanel) {
        m_spritePanel->setApplySpriteEditsEnabled(false);
        m_spritePanel->setSpriteCanvasChecked(false);
        m_spritePanel->setCaptureSelectionChecked(false);
    }
    if (m_canvasStack && m_preview) {
        m_canvasStack->setCurrentWidget(m_preview);
    }
    m_preview->clearSelectionOverlayPixels();
    m_preview->setDocument(m_document ? &*m_document : nullptr);
    m_preview->setBackgroundLayerEnabled(m_framePanel &&
                                         m_framePanel->isBackgroundLayerChecked());
    m_preview->setSelectedSpriteBoundsOverlayEnabled(
        m_spritePanel && m_spritePanel->isBoundsOverlayChecked());
    if (m_spriteEditor) {
        m_spriteEditor->setDocument(m_document ? &*m_document : nullptr);
    }
    m_activeResolution = m_document ? m_document->geometry().nativePlane()
                                    : render::Resolution::Sd;
    updateFrameFilterChoices();
    updateResolutionMode();
    if (!m_document) {
        setPlaybackRunning(false);
        m_frameList->clear();
        m_selectedMaskIndex.reset();
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
        updateRuntimeViewControls();
        return;
    }

    setPlaybackRunning(false);
    if (m_frameFilterComboBox) {
        m_frameFilterComboBox->setEnabled(true);
    }
    m_selectedMaskIndex.reset();
    refreshFrameList(0);
    refreshComponentBrowsers();
    updateSpriteControls();
    updatePlaybackControls();
    updateProjectHeaderStatus();
    updateRuntimeViewControls();
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
        auto* item = new QListWidgetItem(m_frameThumbnails->placeholder(),
                                         frameListItemText(frameIndex));
        item->setData(Qt::UserRole, static_cast<qulonglong>(frameIndex));
        item->setToolTip(frameListItemToolTip(frameIndex));
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
    m_frameThumbnails->scheduleRowsInView();
    updatePlaybackControls();
}

bool MainWindow::isHdOnlyProject() const
{
    return m_document && m_document->rendersHdPlaneOnly();
}

bool MainWindow::isOriginalPlaneOnlyProject() const
{
    return m_document && m_document->editsOriginalPlaneOnly();
}

bool MainWindow::showsHdAndSd() const
{
    return hasHdAsExtraResolution(m_document ? &*m_document : nullptr);
}

QString MainWindow::planeName(render::Resolution resolution) const
{
    return m_document ? planeLabel(*m_document, resolution)
                      : resolution == render::Resolution::Hd ? QStringLiteral("HD")
                                                             : QStringLiteral("SD");
}

QString MainWindow::frameHdGapsText(std::size_t frameIndex) const
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return QString();
    }
    const document::FrameHdGaps gaps = m_document->frameHdGaps(frameIndex);
    const QString size = nativePlaneText(*m_document);
    QStringList reasons;
    if (gaps.frame) {
        reasons << QStringLiteral("no %1 frame").arg(size);
    }
    if (gaps.background) {
        reasons << QStringLiteral("background %1 has no %2 image")
                       .arg(static_cast<int>(m_document->backgroundId(frameIndex)))
                       .arg(size);
    }
    if (!gaps.sprites.empty()) {
        QStringList ids;
        for (uint8_t spriteId : gaps.sprites) {
            ids << QString::number(static_cast<int>(spriteId));
        }
        reasons << QStringLiteral("%1 %2 without %3 picture")
                       .arg(ids.size() > 1 ? QStringLiteral("sprites") : QStringLiteral("sprite"))
                       .arg(ids.join(QStringLiteral(", ")))
                       .arg(size);
    }
    return reasons.isEmpty() ? QString()
                             : QStringLiteral("Not rendered at runtime: %1")
                                   .arg(reasons.join(QStringLiteral("; ")));
}

QString MainWindow::frameListItemToolTip(std::size_t frameIndex) const
{
    QStringList lines;
    const QString gaps = frameHdGapsText(frameIndex);
    if (!gaps.isEmpty()) {
        lines << gaps;
    }
    if (m_document && frameIndex < m_document->frameCount() &&
        legacy::isFrameSkippedAtRuntime(*m_document, frameIndex)) {
        lines << kInactiveFrameToolTip;
    }
    return lines.join(QChar('\n'));
}

void MainWindow::updateInactiveFrameMarker(std::size_t frameIndex)
{
    // Only a frame shorter than the limit can be inactive, and becomes active by being
    // colorized or getting a dynamic zone.
    if (m_document && frameIndex < m_document->frameCount() &&
        m_document->frameDurationMs(frameIndex) < legacy::kSkipFrameDurationMs) {
        updateFrameListItemText(frameIndex);
        updateFrameStatusText();
    }
}

void MainWindow::updateFrameFilterChoices()
{
    if (!m_frameFilterComboBox) {
        return;
    }
    QSignalBlocker blocker(m_frameFilterComboBox);
    const auto offer = [this](FrameFilter filter, const QString& text, int position, bool wanted) {
        const int index = m_frameFilterComboBox->findData(static_cast<int>(filter));
        if ((index >= 0) == wanted) {
            return;
        }
        if (wanted) {
            m_frameFilterComboBox->insertItem(position, text, static_cast<int>(filter));
            return;
        }
        if (m_frameFilterComboBox->currentIndex() == index) {
            m_frameFilterComboBox->setCurrentIndex(0);
        }
        m_frameFilterComboBox->removeItem(index);
    };
    offer(FrameFilter::Hd, QStringLiteral("HD frames"), 1, showsHdAndSd());
    offer(FrameFilter::HdIncomplete,
          QStringLiteral("Not rendered"),
          m_frameFilterComboBox->count(),
          isHdOnlyProject());
}

QString MainWindow::frameListItemText(std::size_t frameIndex) const
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return QString();
    }

    QStringList details;
    details << QStringLiteral("%1 ms").arg(m_document->frameDurationMs(frameIndex));
    if (showsHdAndSd() && m_document->hasHdFrame(frameIndex)) {
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
    if (legacy::isFrameSkippedAtRuntime(*m_document, frameIndex)) {
        details << QStringLiteral("inactive");
    }
    const document::FrameHdGaps gaps = m_document->frameHdGaps(frameIndex);
    if (gaps.frame) {
        details << kNoPictureText;
    }
    if (gaps.background) {
        details << QStringLiteral("BG: %1").arg(kNoPictureText);
    }
    if (!gaps.sprites.empty()) {
        details << QStringLiteral("Spr: %1").arg(kNoPictureText);
    }
    return QStringLiteral("%1\n%2")
        .arg(static_cast<int>(frameIndex))
        .arg(details.join(QStringLiteral("\n")));
}

void MainWindow::refreshComponentBrowsers()
{
    constexpr QSize iconSize{160, 42};
    const bool hdOnly = isHdOnlyProject();
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
                    previewIcon(renderBackgroundPreviewImage(background,
                                                             m_activeResolution,
                                                             m_document->geometry()),
                                iconSize),
                    QStringLiteral("%1%2")
                        .arg(static_cast<int>(backgroundIndex))
                        .arg(extraDataSuffix(*m_document,
                                             background.colorizedHd.has_value())));
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
                    previewIcon(renderSpriteColorizedPreviewImage(sprite,
                                                                  m_activeResolution,
                                                                  hdOnly),
                                iconSize),
                    QStringLiteral("%1%2")
                        .arg(spriteLabel)
                        .arg(extraDataSuffix(*m_document, sprite.hasHd)));
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
    if (m_selectedMaskIndex && *m_selectedMaskIndex >= m_document->comparisonMaskCount()) {
        m_selectedMaskIndex.reset();
    }
    if (!m_selectedMaskIndex) {
        m_selectedMaskIndex = currentComparisonMaskIndex();
    }
    QSignalBlocker blocker(m_maskList);
    m_maskList->setCurrentRow(m_selectedMaskIndex ? static_cast<int>(*m_selectedMaskIndex) : -1);
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
    if (filter == FrameFilter::HdIncomplete) {
        return m_document->frameHdGaps(frameIndex).any();
    }
    if (filter == FrameFilter::Inactive) {
        return legacy::isFrameSkippedAtRuntime(*m_document, frameIndex);
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
    m_canvasEdit.cancelFreeGradient();
    m_canvasEdit.clearSelection(canvasEditContext());
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        setFrameStatusText(QString());
        m_canvasEdit.clearCaptureSelection();
        updateFrameMetadataControls();
        updateSpriteControls();
        updatePlaybackControls();
        return;
    }
    const std::optional<std::size_t> captureFrameIndex = m_canvasEdit.captureSelectionFrame();
    if (captureFrameIndex && *captureFrameIndex != *frameIndex) {
        m_canvasEdit.clearCaptureSelection();
    }
    m_preview->setFrameIndex(*frameIndex);
    updateFrameStatusText();
    updateFrameMetadataControls();
    updateColorRotationControls();
    updateSpriteControls();
    updatePaintTargetMode();
    updateUndoRedoActions();
    m_canvasEdit.showCaptureSelection(currentFrameIndex());
    updatePlaybackControls();
    startRuntimeRotations();
}

void MainWindow::updateFrameStatusText()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        setFrameStatusText(QString());
        return;
    }
    const bool inactive = legacy::isFrameSkippedAtRuntime(*m_document, *frameIndex);
    if (m_runtimeView.isEnabled()) {
        // Which frame a machine identifies the ROM frame as, where that is another one.
        // Short: the label's width is part of the window's minimum width. The sentence is
        // above the picture and in the tool tip.
        const QString hint = QString::fromStdString(
            m_runtimeView.identificationHint(*m_document, *frameIndex, true));
        setFrameStatusText(
            QStringLiteral("Frame %1 | Runtime view, %2 rows | editing off%3%4")
                .arg(static_cast<int>(*frameIndex))
                .arg(m_runtimeView.rowsFor(m_document->geometry()) ==
                             runtime::PlaneRequest::Rows64
                         ? 64
                         : 32)
                .arg(inactive ? QStringLiteral(" | inactive") : QString())
                .arg(hint.isEmpty() ? QString() : QStringLiteral(" | %1").arg(hint)));
        QStringList toolTip{
            QStringLiteral("The canvas shows libserum's output. Switch the Runtime view off "
                           "in the View menu or the toolbar to edit.")};
        if (inactive) {
            toolTip << kInactiveFrameToolTip;
        }
        if (!hint.isEmpty()) {
            toolTip << QStringLiteral("%1. libserum identifies a ROM frame by order: it "
                                      "starts at the frame it identified last, and several "
                                      "frames can match one ROM frame.")
                           .arg(QString::fromStdString(
                               m_runtimeView.identificationHint(*m_document, *frameIndex)));
        }
        m_frameStatusLabel->setToolTip(toolTip.join(QChar('\n')));
        return;
    }
    const int selectedCount = static_cast<int>(selectedFrameIndices().size());
    // Kept short: the label's width is part of the window's minimum width. The reasons are in
    // the tool tip, in the frame list and on the canvas.
    const QString gapsText = frameHdGapsText(*frameIndex);
    const QString planes =
        showsHdAndSd() ? QStringLiteral("SD 128x32 | %1")
                             .arg(m_document->hasHdFrame(*frameIndex)
                                      ? QStringLiteral("HD 256x64")
                                      : QStringLiteral("HD unavailable"))
                       : nativePlaneText(*m_document);
    // An inactive frame is marked in the frame list and explained in the tool tip here; the
    // line itself stays as short as it was.
    setFrameStatusText(QStringLiteral("Frame %1 | %2 | %3 ms%4%5")
                           .arg(static_cast<int>(*frameIndex))
                           .arg(planes)
                           .arg(m_document->frameDurationMs(*frameIndex))
                           .arg(selectedCount > 1
                                    ? QStringLiteral(" | %1 selected").arg(selectedCount)
                                    : QString())
                           .arg(gapsText.isEmpty() ? QString()
                                                   : QStringLiteral(" | not rendered")));
    QStringList toolTip;
    if (!gapsText.isEmpty()) {
        toolTip << QStringLiteral("%1.").arg(gapsText);
    }
    if (inactive) {
        toolTip << kInactiveFrameToolTip;
    }
    m_frameStatusLabel->setToolTip(toolTip.join(QChar('\n')));
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
        if (m_runtimeRotationTimer) {
            m_runtimeRotationTimer->stop();
        }
        if (m_runtimeView.isPlaying()) {
            m_runtimeView.stopPlayback();
            if (m_preview) {
                m_preview->update();
            }
            updateFrameStatusText();
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
    m_playbackTimer->start(playbackIntervalForCurrentFrame());
    if (m_runtimeView.isEnabled()) {
        // libserum plays: it keeps its state from frame to frame and rotates the colors.
        m_runtimeView.startPlayback(playbackFrameOrder());
        if (m_preview) {
            m_preview->update();
        }
        startRuntimeRotations();
        // Fed to libserum now: the status line says which frame it identified.
        updateFrameStatusText();
    } else {
        if (m_preview) {
            m_preview->setColorRotationPreviewElapsedMs(0);
        }
        if (m_colorRotationPreviewTimer) {
            m_colorRotationPreviewTimer->start();
        }
    }
    updatePlaybackControls();
}

std::vector<std::size_t> MainWindow::playbackFrameOrder() const
{
    std::vector<std::size_t> order;
    if (!m_frameList) {
        return order;
    }
    const int rows = m_frameList->count();
    const int start = std::max(0, m_frameList->currentRow());
    order.reserve(static_cast<std::size_t>(rows));
    for (int offset = 0; offset < rows; ++offset) {
        const QListWidgetItem* item = m_frameList->item((start + offset) % rows);
        if (item) {
            order.push_back(static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong()));
        }
    }
    return order;
}

void MainWindow::setRuntimeViewEnabled(bool enabled)
{
    if (m_runtimeView.isEnabled() == enabled) {
        updateRuntimeViewControls();
        return;
    }
    setPlaybackRunning(false);
    if (enabled) {
        leaveSpriteCanvas();
        m_canvasEdit.cancelFreeGradient();
        if (m_preview) {
            m_preview->clearPreviewPixels();
        }
        // The plane the editing mode is on is the one shown first.
        m_runtimeView.setRows(m_activeResolution == render::Resolution::Hd
                                  ? runtime::PlaneRequest::Rows64
                                  : runtime::PlaneRequest::Rows32);
    }
    m_runtimeView.setEnabled(enabled);
    updateRuntimeViewControls();
}

void MainWindow::setRuntimeViewRows(runtime::PlaneRequest rows)
{
    if (m_runtimeView.rows() == rows) {
        return;
    }
    setPlaybackRunning(false);
    m_runtimeView.setRows(rows);
    updateRuntimeViewControls();
}

void MainWindow::updateRuntimeViewControls()
{
    const bool enabled = m_runtimeView.isEnabled();
    const bool twoPlanes =
        m_document && runtime::planeRequestsFor(m_document->geometry()).size() > 1;
    if (m_runtimeViewAction) {
        const QSignalBlocker blocker(m_runtimeViewAction);
        m_runtimeViewAction->setChecked(enabled);
    }
    const bool rows64 = !m_document || m_runtimeView.rowsFor(m_document->geometry()) ==
                                           runtime::PlaneRequest::Rows64;
    if (m_runtimeRows32Action && m_runtimeRows64Action) {
        m_runtimeRows32Action->setVisible(twoPlanes);
        m_runtimeRows64Action->setVisible(twoPlanes);
        m_runtimeRows32Action->setEnabled(enabled && twoPlanes);
        m_runtimeRows64Action->setEnabled(enabled && twoPlanes);
        (rows64 ? m_runtimeRows64Action : m_runtimeRows32Action)->setChecked(true);
    }
    updateCanvasModeButtons();
    updateFrameStatusText();
    if (m_preview) {
        m_preview->update();
    }
}

void MainWindow::startRuntimeRotations()
{
    if (!m_runtimeRotationTimer) {
        return;
    }
    m_runtimeRotationTimer->stop();
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || !m_runtimeView.isPlaying()) {
        return;
    }
    const uint32_t nextMs = m_runtimeView.frame(*m_document, *frameIndex).nextRotationMs;
    if (nextMs > 0) {
        m_runtimeRotationTimer->start(static_cast<int>(nextMs));
    }
}

void MainWindow::stepRuntimeRotations()
{
    if (!m_runtimeView.isPlaying()) {
        return;
    }
    const uint32_t nextMs = m_runtimeView.rotate();
    if (m_preview) {
        m_preview->update();
    }
    if (nextMs > 0) {
        m_runtimeRotationTimer->start(static_cast<int>(nextMs));
    }
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

void MainWindow::updateCanvasModeButtons()
{
    const bool hasDocument = m_document.has_value();
    const bool twoPlanes = !isHdOnlyProject() && !isOriginalPlaneOnlyProject();
    // In the Runtime view the plane is chosen in the View menu and no overlay is drawn.
    const bool editing = !m_runtimeView.isEnabled();
    m_sdButton->setEnabled(hasDocument && twoPlanes && editing);
    m_hdButton->setEnabled(hasDocument && twoPlanes && editing);
    m_comparisonMaskOverlayButton->setEnabled(hasDocument && editing);
    // Dynamic zones are shown and painted on the plane the runtime takes them from.
    const bool dynamicZonesHere = isDynamicPlaneActive();
    m_dynamicMaskOverlayButton->setEnabled(hasDocument && editing && dynamicZonesHere);
    m_dynamicMaskOverlayButton->setToolTip(
        dynamicZonesHere ? QStringLiteral("Show dynamic mask overlay")
                         : QStringLiteral("Dynamic zones are defined on the 128x32 plane: "
                                          "switch to SD to see and paint them"));
    m_backgroundMaskOverlayButton->setEnabled(hasDocument && editing);
    for (QToolButton* button : m_toolButtons) {
        button->setEnabled(editing);
    }
}

void MainWindow::updateResolutionMode()
{
    const bool hasDocument = m_document.has_value();
    const bool hdOnly = isHdOnlyProject();
    const bool originalOnly = isOriginalPlaneOnlyProject();
    if (hdOnly) {
        m_activeResolution = render::Resolution::Hd;
    } else if (originalOnly) {
        m_activeResolution = render::Resolution::Sd;
    }
    // With one plane there is nothing to switch and nothing called SD or HD.
    const bool twoPlanes = !hdOnly && !originalOnly;
    m_sdButton->setVisible(twoPlanes);
    m_hdButton->setVisible(twoPlanes);
    if (!isDynamicPlaneActive() && m_dynamicMaskOverlayButton->isChecked()) {
        // Leaves the mode: the overlay goes and the tools paint the frame again.
        m_dynamicMaskOverlayButton->setChecked(false);
        statusBar()->showMessage(
            QStringLiteral("Dynamic mode is off in HD: dynamic zones are defined on the "
                           "128x32 plane"),
            4000);
    }
    updateCanvasModeButtons();
    if (m_dynamicPanel) {
        m_dynamicPanel->setNote(
            isDynamicPlaneActive()
                ? QString()
                : QStringLiteral("Dynamic zones are defined on the 128x32 plane. The colors "
                                 "and shadows here are the ones of that plane; switch to SD "
                                 "to see and paint the zones."));
    }
    if (m_framePanel) {
        m_framePanel->setImportImageEnabled(hasDocument && !selectedFrameIndices().empty());
    }
    if (m_dynamicPanel) {
        m_dynamicPanel->setDynamicSetEnabled(hasDocument);
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
    if (isSpriteCanvasActive()) {
        m_framePanel->showSpriteHdActions(m_document->hasHdSprite(*currentSpriteIndex()),
                                          m_document->geometry().layout());
    }

    if (!hasFrame || m_document->backgroundCount() == 0) {
        setPaintTarget(PaintTarget::Frame);
    }
    updateDynamicColorControls();
    updatePaintTargetMode();
    updateBackgroundControls();
    if (hasFrame) {
        updateMaskBrowserSelection();
    }
    updateMaskControls();
}

void MainWindow::updateMaskControls()
{
    if (!m_maskPanel) {
        return;
    }

    m_maskPanel->refreshSelection(m_document ? &*m_document : nullptr,
                                  currentFrameIndex(),
                                  m_document ? m_selectedMaskIndex : std::nullopt,
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
                        .arg(extraDataSuffix(*m_document,
                                             background.colorizedHd.has_value())),
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

MainWindow::FrameEditCount MainWindow::editSelectedFramesAttribute(
    edit::FrameAttribute attribute,
    uint32_t value)
{
    FrameEditCount count;
    if (!m_document) {
        return count;
    }
    for (std::size_t frameIndex : selectedFrameIndices()) {
        if (attribute == edit::FrameAttribute::BackgroundId &&
            !m_document->backgroundData(frameIndex)) {
            ++count.skipped;
            continue;
        }
        const uint32_t oldValue = edit::frameAttributeValue(*m_document, frameIndex, attribute);
        if (oldValue == value) {
            continue;
        }
        commitEdit(std::make_unique<edit::FrameAttributeEditCommand>(frameIndex,
                                                                     attribute,
                                                                     oldValue,
                                                                     value));
        ++count.changed;
    }
    updateUndoRedoActions();
    return count;
}

void MainWindow::setCurrentFrameComparisonMaskId(int maskId)
{
    if (!m_document || selectedFrameIndices().empty()) {
        return;
    }

    const uint32_t storedMaskId = maskId < 0 ? 255u : static_cast<uint32_t>(maskId & 0xff);
    try {
        const FrameEditCount count =
            editSelectedFramesAttribute(edit::FrameAttribute::ComparisonMaskId, storedMaskId);
        statusBar()->showMessage(
            maskId < 0 ? QStringLiteral("Cleared comparison mask assignment on %1")
                             .arg(frameEditCountText(count.changed, count.skipped))
                       : QStringLiteral("Assigned comparison mask %1 on %2")
                             .arg(maskId)
                             .arg(frameEditCountText(count.changed, count.skipped)),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::assignSelectedMaskToFrames()
{
    if (!m_document || !m_selectedMaskIndex || selectedFrameIndices().empty() ||
        *m_selectedMaskIndex >= m_document->comparisonMaskCount()) {
        statusBar()->showMessage(QStringLiteral("No comparison mask selected"), 2500);
        updateMaskControls();
        return;
    }
    setCurrentFrameComparisonMaskId(static_cast<int>(*m_selectedMaskIndex));
}

void MainWindow::leaveSpriteCanvas()
{
    if (!m_spritePanel || !m_spritePanel->isSpriteCanvasChecked()) {
        if (m_canvasStack && m_preview) {
            m_canvasStack->setCurrentWidget(m_preview);
        }
        return;
    }
    applyPendingSpriteDependentViews();
    m_spritePanel->setSpriteCanvasChecked(false);
    if (m_canvasStack && m_preview) {
        m_canvasStack->setCurrentWidget(m_preview);
    }
    updateFrameMetadataControls();
    updateSpriteControls();
    updateUndoRedoActions();
}

void MainWindow::editAssignedMask()
{
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        statusBar()->showMessage(QStringLiteral("Assign a comparison mask before editing"), 2500);
        updateMaskControls();
        return;
    }
    leaveSpriteCanvas();
    if (m_comparisonMaskOverlayButton) {
        m_comparisonMaskOverlayButton->setChecked(true);
    }
    updateUndoRedoActions();
    statusBar()->showMessage(
        QStringLiteral("Editing comparison mask %1").arg(static_cast<int>(*maskIndex)),
        1500);
}

void MainWindow::setCurrentFrameShapeComparisonMode(int mode)
{
    if (!m_document || selectedFrameIndices().empty()) {
        return;
    }

    try {
        const FrameEditCount count =
            editSelectedFramesAttribute(edit::FrameAttribute::ShapeComparisonMode,
                                        static_cast<uint32_t>(mode & 0xff));
        statusBar()->showMessage(
            (mode == 0 ? QStringLiteral("Set full comparison mode on %1")
                       : QStringLiteral("Set shape mode on %1"))
                .arg(frameEditCountText(count.changed, count.skipped)),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameBackgroundId(int backgroundId)
{
    if (!m_document || selectedFrameIndices().empty()) {
        return;
    }

    const uint32_t storedBackgroundId =
        backgroundId < 0 ? 0xffffu : static_cast<uint32_t>(backgroundId & 0xffff);
    try {
        const FrameEditCount count =
            editSelectedFramesAttribute(edit::FrameAttribute::BackgroundId, storedBackgroundId);
        if (count.changed == 0) {
            // Nothing was committed, so the spin box may still show the rejected value.
            updateFrameMetadataControls();
        }
        const QString countText = frameEditCountText(count.changed,
                                                     count.skipped,
                                                     QStringLiteral("no background data"));
        statusBar()->showMessage(
            backgroundId < 0
                ? QStringLiteral("Cleared background assignment on %1").arg(countText)
                : QStringLiteral("Assigned background %1 on %2").arg(backgroundId).arg(countText),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameTriggerId(int triggerId)
{
    if (!m_document || selectedFrameIndices().empty()) {
        return;
    }
    if (m_framePanel && m_framePanel->isSwitchToMonochromeChecked()) {
        return;
    }

    const uint32_t storedTriggerId =
        triggerId < 0 ? document::kNoTriggerId
                      : static_cast<uint32_t>(std::min(triggerId, kMaxManualTriggerId));
    try {
        const FrameEditCount count =
            editSelectedFramesAttribute(edit::FrameAttribute::TriggerId, storedTriggerId);
        const QString countText = frameEditCountText(count.changed, count.skipped);
        statusBar()->showMessage(
            triggerId < 0
                ? QStringLiteral("Cleared trigger ID on %1").arg(countText)
                : QStringLiteral("Set trigger ID %1 on %2").arg(storedTriggerId).arg(countText),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameDurationMs(int durationMs)
{
    if (!m_document || selectedFrameIndices().empty()) {
        return;
    }

    const uint32_t storedDurationMs =
        static_cast<uint32_t>(std::clamp(durationMs, 0, kMaxFrameDurationMs));
    try {
        const FrameEditCount count =
            editSelectedFramesAttribute(edit::FrameAttribute::DurationMs, storedDurationMs);
        statusBar()->showMessage(
            QStringLiteral("Set duration %1 ms on %2")
                .arg(storedDurationMs)
                .arg(frameEditCountText(count.changed, count.skipped)),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentFrameSwitchToMonochrome(bool enabled)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    try {
        int changed = 0;
        for (std::size_t frameIndex : frameIndices) {
            const uint32_t oldTriggerId = m_document->triggerId(frameIndex);
            // Switching off only concerns frames that carry the monochrome trigger; other
            // selected frames keep their own trigger ID.
            if (enabled ? oldTriggerId == kSwitchToMonochromeTriggerId
                        : oldTriggerId != kSwitchToMonochromeTriggerId) {
                continue;
            }
            commitEdit(std::make_unique<edit::FrameAttributeEditCommand>(
                frameIndex,
                edit::FrameAttribute::TriggerId,
                oldTriggerId,
                enabled ? kSwitchToMonochromeTriggerId : document::kNoTriggerId));
            ++changed;
        }
        if (changed == 0) {
            updateFrameMetadataControls();
        }
        updateUndoRedoActions();
        statusBar()->showMessage(
            (enabled ? QStringLiteral("Enabled switch-to-monochrome trigger on %1")
                     : QStringLiteral("Disabled switch-to-monochrome trigger on %1"))
                .arg(frameEditCountText(changed, 0)),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
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
        if (isHdOnlyProject()) {
            statusBar()->showMessage(
                QStringLiteral("Background %1 has no %2 image yet: import an image, or use "
                               "Create %2 Frame on a frame it is assigned to")
                    .arg(static_cast<int>(*backgroundIndex))
                    .arg(nativePlaneText(*m_document)),
                5000);
            updateBackgroundControls();
            return;
        }
        m_activeResolution = render::Resolution::Sd;
        updateResolutionMode();
    }
    leaveSpriteCanvas();
    setPaintTarget(PaintTarget::Background);
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
    if (isHdOnlyProject() && targetResolution != render::Resolution::Hd) {
        statusBar()->showMessage(
            QStringLiteral("The frame and the background both need their %1 data first")
                .arg(nativePlaneText(*m_document)),
            4000);
        return;
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
                .arg(planeName(targetResolution)),
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
        if (isHdOnlyProject()) {
            statusBar()->showMessage(QStringLiteral("The background has no %1 image yet")
                                         .arg(nativePlaneText(*m_document)),
                                     4000);
            return;
        }
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
                .arg(planeName(sourceResolution)),
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
    const render::Size targetSize = m_document->geometry().planeSize(targetResolution);
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
                .arg(planeName(targetResolution)),
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
        if (isHdOnlyProject()) {
            statusBar()->showMessage(QStringLiteral("The background has no %1 image yet")
                                         .arg(nativePlaneText(*m_document)),
                                     4000);
            return;
        }
        exportResolution = render::Resolution::Sd;
    }

    const QString resolutionName = planeName(exportResolution).toLower();
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
        updateFrameStatusText();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Assigned sprite %1 to slot %2 on %3 frame(s)%4")
                .arg(spriteIndex)
                .arg(static_cast<int>(*slotIndex))
                .arg(static_cast<int>(frameIndices.size()))
                .arg(isHdOnlyProject() &&
                             !m_document->hasHdSprite(static_cast<std::size_t>(spriteIndex))
                         ? QStringLiteral(". The sprite has no %1 picture: these frames are not "
                                          "rendered at runtime until it has")
                               .arg(nativePlaneText(*m_document))
                         : QString()),
            isHdOnlyProject() ? 6000 : 2500);
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

    // Bounds, selection and the captured pixels are original frame pixels. The colors come
    // from the native plane, which lines up with the original pixel for pixel: the HD frame of
    // an HD-only project.
    const bool hdOnly = isHdOnlyProject();
    if (hdOnly && !m_document->hasHdFrame(*frameIndex)) {
        statusBar()->showMessage(
            QStringLiteral("Frame %1 has no %2 frame to capture colors from: use Create %2 "
                           "Frame first")
                .arg(static_cast<int>(*frameIndex))
                .arg(nativePlaneText(*m_document)),
            4000);
        updateSpriteControls();
        return;
    }
    const render::Size originalSize = m_document->originalFrameSize();
    const SpriteSlotBounds bounds = m_spritePanel->slotBounds();
    const int minX = std::clamp(bounds.minX, 0, originalSize.width - 1);
    const int minY = std::clamp(bounds.minY, 0, originalSize.height - 1);
    const int maxX = std::clamp(bounds.maxX, minX, originalSize.width - 1);
    const int maxY = std::clamp(bounds.maxY, minY, originalSize.height - 1);
    const std::vector<render::Point> selectedPixels =
        m_canvasEdit.captureSelectionPixels(frameIndex);
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
        captureMinX = originalSize.width - 1;
        captureMinY = originalSize.height - 1;
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
                                 m_document->geometry().nativePlane(),
                                 includeBackgroundLayer);
        const document::IndexedFrame& sourceOriginal = m_document->originalFrame(*frameIndex);
        if (selectedPixels.empty()) {
            m_document->replaceSpriteFromFrame(*spriteIndex,
                                               sourceOriginal,
                                               sourceColorized,
                                               captureMinX,
                                               captureMinY,
                                               captureWidth,
                                               captureHeight);
        } else {
            m_document->replaceSpriteFromFrameSelection(*spriteIndex,
                                                        sourceOriginal,
                                                        sourceColorized,
                                                        selectedPixels);
        }
        m_undoHistories.clear(edit::EditKind::SpriteImage, *spriteIndex);
        m_undoHistories.clear(edit::EditKind::SpriteAttributes, *spriteIndex);
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
            m_canvasEdit.clearCaptureSelection();
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

bool MainWindow::isSpriteCaptureSelectionModeActive() const
{
    return m_spritePanel &&
           m_spritePanel->isCaptureSelectionChecked() &&
           m_document.has_value() &&
           currentFrameIndex().has_value();
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
        int changed = 0;
        int skipped = 0;
        for (std::size_t frameIndex : frameIndices) {
            const std::vector<document::FrameSpriteReference>& references =
                m_document->frameSpriteReferences(frameIndex);
            if (*slotIndex >= references.size()) {
                ++skipped;
                continue;
            }
            if (references[*slotIndex].spriteId == 255) {
                continue;
            }
            m_document->clearFrameSpriteReference(frameIndex, *slotIndex);
            updateFrameListItem(frameIndex);
            ++changed;
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Cleared sprite slot %1 on %2")
                .arg(static_cast<int>(*slotIndex))
                .arg(frameEditCountText(changed, skipped, QStringLiteral("no sprite slots"))),
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
    const int minX = bounds.minX;
    const int maxX = std::max(bounds.minX, bounds.maxX);
    const int minY = bounds.minY;
    const int maxY = std::max(bounds.minY, bounds.maxY);

    try {
        int changed = 0;
        for (std::size_t frameIndex : frameIndices) {
            const std::vector<document::FrameSpriteReference>& references =
                m_document->frameSpriteReferences(frameIndex);
            // Bounds belong to an assigned sprite; an empty slot keeps none.
            if (*slotIndex >= references.size() || references[*slotIndex].spriteId == 255) {
                continue;
            }
            const document::FrameSpriteReference reference = references[*slotIndex];
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
            ++changed;
        }
        if (changed == 0) {
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
                .arg(changed),
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

    // The spin boxes edit an existing area; a new one is drawn in the sprite editor.
    const document::SpriteRecord& sprite =
        m_document->sprite(static_cast<std::size_t>(spriteIndex));
    if (areaIndex >= sprite.detectionAreas.size() ||
        !isSpriteDetectionAreaSet(sprite.detectionAreas[areaIndex])) {
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
    if (isOriginalPlaneOnlyProject()) {
        return;
    }

    try {
        std::unique_ptr<edit::EditCommand> command =
            edit::createSpriteHd(*m_document, *spriteIndex);
        m_activeResolution = render::Resolution::Hd;
        recordEdit(std::move(command));
        updateUndoRedoActions();
        statusBar()->showMessage(
            isHdOnlyProject()
                ? QStringLiteral("Created the %1 sprite from the sprite original in its plain "
                                 "colors")
                      .arg(nativePlaneText(*m_document))
                : QStringLiteral("Created HD sprite from SD"),
            2500);
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
    if (isHdOnlyProject()) {
        statusBar()->showMessage(
            QStringLiteral("The %1 sprite is the only sprite data this project renders; it "
                           "cannot be removed")
                .arg(nativePlaneText(*m_document)),
            4000);
        return;
    }

    try {
        recordEdit(edit::removeSpriteHd(*m_document, *spriteIndex));
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
    if (isHdOnlyProject()) {
        createCurrentFrameHdFromOriginal(*frameIndex);
        return;
    }
    if (isOriginalPlaneOnlyProject()) {
        return;
    }
    if (m_document->hasHdFrame(*frameIndex)) {
        updateFrameMetadataControls();
        return;
    }

    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(*frameIndex);
    const bool hasBackgroundData = backgroundData.has_value();
    const bool hasAssignedBackground =
        backgroundData && backgroundData->backgroundId < m_document->backgroundCount();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Create HD Frame"));
    auto* dialogLayout = new QVBoxLayout(&dialog);
    // No dynamic data among them: the runtime renders the dynamic zones of the SD plane into
    // the HD picture and does not read an HD dynamic mask.
    auto* description = new QLabel(QStringLiteral("Create HD frame from SD. Upscale related data?"),
                                   &dialog);
    description->setWordWrap(true);
    dialogLayout->addWidget(description);
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
        document::FrameHdParts parts;
        parts.backgroundMask = backgroundMaskCheckBox->isChecked();
        parts.backgroundImage = backgroundImageCheckBox->isChecked();
        std::unique_ptr<edit::EditCommand> command =
            edit::createFrameHd(*m_document, *frameIndex, parts);
        m_activeResolution = render::Resolution::Hd;
        recordEdit(std::move(command));
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Created HD frame from SD"), 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::createCurrentFrameHdFromOriginal(std::size_t frameIndex)
{
    const document::FrameHdGaps gaps = m_document->frameHdGaps(frameIndex);
    if (!gaps.frame && !gaps.background) {
        updateFrameMetadataControls();
        return;
    }
    try {
        // Nothing is upscaled from the unused SD data; the HD masks, dynamic colors and
        // rotations of the frame stay as they are stored.
        document::FrameHdParts parts;
        parts.backgroundImage = gaps.background;
        std::unique_ptr<edit::EditCommand> command =
            edit::createFrameHd(*m_document, frameIndex, parts);
        recordEdit(std::move(command));
        updateUndoRedoActions();
        QStringList created;
        if (gaps.frame) {
            created << QStringLiteral("the %1 frame from the original in its plain colors")
                           .arg(nativePlaneText(*m_document));
        }
        if (gaps.background) {
            created << QStringLiteral("a black %1 image for background %2")
                           .arg(nativePlaneText(*m_document))
                           .arg(static_cast<int>(m_document->backgroundId(frameIndex)));
        }
        statusBar()->showMessage(
            QStringLiteral("Created %1").arg(created.join(QStringLiteral(" and "))), 4000);
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
    if (isHdOnlyProject()) {
        statusBar()->showMessage(
            QStringLiteral("The %1 frame is the only frame data this project renders; it "
                           "cannot be removed")
                .arg(nativePlaneText(*m_document)),
            4000);
        return;
    }

    try {
        recordEdit(edit::removeFrameHd(*m_document, *frameIndex));
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
                                      dynamicPlane());
    }
    updateDynamicShadowControls();
}

void MainWindow::updateDynamicShadowControls()
{
    if (m_dynamicPanel) {
        m_dynamicPanel->refreshShadow(m_document ? &*m_document : nullptr,
                                      currentFrameIndex(),
                                      dynamicPlane());
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
                                                            dynamicPlane(),
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
                                                                   dynamicPlane(),
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
                                                   dynamicPlane(),
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
                                                                        dynamicPlane(),
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
            m_document->dynamicShadowColor(*frameIndex, dynamicPlane(), activeDynamicSet());
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
                                               dynamicPlane(),
                                               activeDynamicSet());
            if (oldColor == newRgb565) {
                continue;
            }
            edit::DynamicShadowChange change;
            change.oldColor = oldColor;
            change.newColor = newRgb565;
            commitEdit(std::make_unique<edit::DynamicShadowEditCommand>(selectedFrameIndex,
                                                                        dynamicPlane(),
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
        m_canvasEdit.leaveSelectionDirectionMode();
    }
    m_palettePanel->refresh(m_document ? &*m_document : nullptr,
                            m_canvasEdit.hasSelection(),
                            m_canvasEdit.isSelectionDirectionMode());
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
    state.hasCaptureSelection = !m_canvasEdit.captureSelectionPixels(frameIndex).empty();
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
        m_framePanel->showSpriteHdActions(sprite.hasHd, m_document->geometry().layout());
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
    if (m_framePanel) {
        m_framePanel->setPaintTargetEnabled(selectedBackgroundIndex().has_value());
    }
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
    m_canvasEdit.cancelFreeGradient();
    m_canvasEdit.clearSelection(canvasEditContext());
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

CanvasEditContext MainWindow::canvasEditContext() const
{
    CanvasEditContext context;
    context.document = m_document ? &*m_document : nullptr;
    context.currentFrame = currentFrameIndex();
    context.selectedFrames = selectedFrameIndices();
    if (isComparisonMaskModeActive()) {
        context.layer = CanvasEditLayer::ComparisonMask;
    } else if (isDynamicMaskModeActive()) {
        context.layer = CanvasEditLayer::DynamicMask;
    } else if (isBackgroundMaskModeActive()) {
        context.layer = CanvasEditLayer::BackgroundMask;
    } else if (isBackgroundImageTargetActive()) {
        context.layer = CanvasEditLayer::BackgroundImage;
    }
    context.spriteCaptureSelection = isSpriteCaptureSelectionModeActive();
    context.paintTarget = activePaintTarget();
    context.selectedBackground = selectedBackgroundIndex();
    context.selectedPalette = selectedPaletteIndex();
    context.tool = m_drawingTool;
    context.drawColor = m_activeDrawColor;
    context.dynamicSet = activeDynamicSet();
    if (m_palettePanel) {
        context.gradientShape = m_palettePanel->gradientShape();
        context.gradientDirection = m_palettePanel->gradientDirection();
        context.gradientPattern = m_palettePanel->gradientPattern();
        context.gradientEndpoints = [panel = m_palettePanel]() {
            return panel->gradientEndpoints();
        };
    }
    return context;
}

CanvasEditOutputs MainWindow::canvasEditOutputs()
{
    CanvasEditOutputs outputs;
    outputs.commit = [this](std::unique_ptr<edit::EditCommand> command) {
        commitEdit(std::move(command));
    };
    outputs.commitsFinished = [this]() {
        updateUndoRedoActions();
    };
    outputs.showStatus = [this](const QString& text, int timeoutMs) {
        statusBar()->showMessage(text, timeoutMs);
    };
    outputs.pickColor = [this](uint16_t rgb565) {
        setActiveDrawColor(rgb565);
    };
    outputs.pickDynamicSet = [this](uint8_t dynamicSet) {
        if (!m_dynamicPanel) {
            return;
        }
        if (dynamicSet != m_dynamicPanel->dynamicSet()) {
            m_dynamicPanel->setDynamicSet(static_cast<int>(dynamicSet));
            dynamicSetChanged(static_cast<int>(dynamicSet));
        }
        updateDynamicColorControls();
    };
    outputs.showPreview = [this](CanvasPixelOverlay overlay) {
        if (m_preview) {
            m_preview->setPreviewPixels(overlay.region,
                                        overlay.resolution,
                                        std::move(overlay.pixels),
                                        overlay.color);
        }
    };
    outputs.clearPreview = [this]() {
        if (m_preview) {
            m_preview->clearPreviewPixels();
        }
    };
    outputs.showSelectionOverlay = [this](CanvasPixelOverlay overlay) {
        if (m_preview) {
            m_preview->setSelectionOverlayPixels(overlay.region,
                                                 overlay.resolution,
                                                 std::move(overlay.pixels),
                                                 overlay.color);
        }
    };
    outputs.clearSelectionOverlay = [this]() {
        if (m_preview) {
            m_preview->clearSelectionOverlayPixels();
        }
    };
    outputs.selectionChanged = [this]() {
        updatePaletteControls();
    };
    outputs.selectionDirectionModeChanged = [this](bool active) {
        if (m_palettePanel) {
            m_palettePanel->setSelectionDirectionChecked(active);
        }
    };
    outputs.captureSelectionChanged = [this]() {
        updateSpriteControls();
    };
    return outputs;
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
        m_canvasEdit.cancelFreeGradient();
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

void MainWindow::commitEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (!m_document) {
        return;
    }
    command->apply(*m_document);
    recordEdit(std::move(command));
}

void MainWindow::recordEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const edit::EditCommand& applied = *command;
    m_undoHistories.push(edit::historyKey(applied.target()), std::move(command));
    refreshAfterEdit(applied);
}

void MainWindow::refreshAfterEdit(const edit::EditCommand& command)
{
    if (!m_document) {
        return;
    }
    const edit::EditTarget target = command.target();
    const bool hdDataReplaced = !command.replacedData().empty();
    switch (target.kind) {
        case edit::EditKind::FramePixels:
            m_preview->update();
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameThumbnail(frameIndex);
                updateInactiveFrameMarker(frameIndex);
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::DynamicMask:
            for (std::size_t frameIndex : target.frameIndices) {
                updateInactiveFrameMarker(frameIndex);
            }
            [[fallthrough]];
        case edit::EditKind::ComparisonMask:
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
        case edit::EditKind::FrameAttributes:
            if (hdDataReplaced) {
                const std::optional<std::size_t> frameIndex = currentFrameIndex();
                if (m_activeResolution == render::Resolution::Hd && frameIndex &&
                    !m_document->hasHdFrame(*frameIndex) && !isHdOnlyProject()) {
                    m_activeResolution = render::Resolution::Sd;
                }
                updateResolutionMode();
            }
            m_preview->update();
            updateFrameMetadataControls();
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameListItem(frameIndex);
            }
            updateFrameStatusText();
            if (m_playbackTimer && m_playbackTimer->isActive()) {
                m_playbackTimer->setInterval(playbackIntervalForCurrentFrame());
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::SpriteAttributes:
            if (m_activeResolution == render::Resolution::Hd &&
                !m_document->hasHdSprite(target.index) && !isHdOnlyProject()) {
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
            break;
    }
    if (m_runtimeView.isEnabled()) {
        m_preview->update();
    }
}

std::vector<edit::HistoryKey> MainWindow::currentHistoryKeys() const
{
    const render::Resolution sd = render::Resolution::Sd;
    std::vector<edit::HistoryKey> keys;
    if (isSpriteCanvasActive()) {
        const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
        if (spriteIndex) {
            render::Resolution resolution = m_activeResolution;
            if (m_document) {
                const document::SpriteRecord& sprite = m_document->sprite(*spriteIndex);
                if (resolution == render::Resolution::Hd && !sprite.hasHd &&
                    !isHdOnlyProject()) {
                    resolution = render::Resolution::Sd;
                }
            }
            keys.push_back({edit::EditKind::SpriteImage, *spriteIndex, resolution});
            keys.push_back({edit::EditKind::SpriteAttributes, *spriteIndex, sd});
        }
        return keys;
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (isComparisonMaskModeActive()) {
        const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
        if (maskIndex) {
            keys.push_back({edit::EditKind::ComparisonMask, *maskIndex, sd});
        }
        if (frameIndex) {
            keys.push_back({edit::EditKind::FrameAttributes, *frameIndex, sd});
        }
        return keys;
    }
    if (isDynamicMaskModeActive()) {
        if (frameIndex) {
            keys.push_back({edit::EditKind::DynamicMask, *frameIndex, dynamicPlane()});
            keys.push_back({edit::EditKind::DynamicColor, *frameIndex, dynamicPlane()});
            keys.push_back({edit::EditKind::DynamicShadow, *frameIndex, dynamicPlane()});
        }
        return keys;
    }
    if (isBackgroundMaskModeActive()) {
        if (frameIndex) {
            keys.push_back({edit::EditKind::BackgroundMask, *frameIndex, m_activeResolution});
            keys.push_back({edit::EditKind::FrameAttributes, *frameIndex, sd});
        }
        return keys;
    }
    if (frameIndex) {
        keys.push_back({edit::EditKind::FramePixels, *frameIndex, m_activeResolution});
    }
    const std::optional<std::size_t> paletteIndex = selectedPaletteIndex();
    if (paletteIndex) {
        keys.push_back({edit::EditKind::Palette, *paletteIndex, sd});
    }
    if (frameIndex) {
        keys.push_back({edit::EditKind::DynamicColor, *frameIndex, dynamicPlane()});
        keys.push_back({edit::EditKind::DynamicShadow, *frameIndex, dynamicPlane()});
    }
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (backgroundIndex) {
        keys.push_back({edit::EditKind::BackgroundImage, *backgroundIndex, m_activeResolution});
    }
    if (frameIndex) {
        keys.push_back({edit::EditKind::FrameAttributes, *frameIndex, sd});
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

render::Resolution MainWindow::dynamicPlane() const
{
    return m_document ? m_document->dynamicPlane() : m_activeResolution;
}

bool MainWindow::isDynamicPlaneActive() const
{
    return m_activeResolution == dynamicPlane();
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
    const edit::EditCommand* command = nullptr;
    try {
        command = m_undoHistories.undoLatest(currentHistoryKeys(), *m_document);
    } catch (const std::exception& ex) {
        // The edited data no longer exists in that shape, e.g. its HD frame was removed.
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    if (!command) {
        return;
    }
    const edit::EditKind kind = command->target().kind;
    refreshAfterEdit(*command);
    updateUndoRedoActions();
    statusBar()->showMessage(QStringLiteral("Undo") + editKindStatusSuffix(kind, showsHdAndSd()),
                             1500);
}

void MainWindow::redo()
{
    if (!m_document) {
        return;
    }
    const edit::EditCommand* command = nullptr;
    try {
        command = m_undoHistories.redoLatest(currentHistoryKeys(), *m_document);
    } catch (const std::exception& ex) {
        // The edited data no longer exists in that shape, e.g. its HD frame was removed.
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    if (!command) {
        return;
    }
    const edit::EditKind kind = command->target().kind;
    refreshAfterEdit(*command);
    updateUndoRedoActions();
    statusBar()->showMessage(QStringLiteral("Redo") + editKindStatusSuffix(kind, showsHdAndSd()),
                             1500);
}

void MainWindow::updateUndoRedoActions()
{
    const std::vector<edit::HistoryKey> keys = currentHistoryKeys();
    if (m_undoAction) {
        m_undoAction->setEnabled(m_undoHistories.canUndoAny(keys));
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(m_undoHistories.canRedoAny(keys));
    }
}

void MainWindow::updateProjectHeaderStatus()
{
    // Called after every change of the document: the Runtime view shows the document.
    if (m_runtimeView.isEnabled() && m_preview) {
        m_preview->update();
    }
    if (!m_document) {
        setStatusText(QStringLiteral("No project loaded"));
        return;
    }
    int hdCount = 0;
    int notRenderedCount = 0;
    for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
        if (m_document->hasHdFrame(frameIndex)) {
            ++hdCount;
        }
        if (isHdOnlyProject() && m_document->frameHdGaps(frameIndex).any()) {
            ++notRenderedCount;
        }
    }
    setStatusText(QStringLiteral("%1: %2 frames%3, %4 backgrounds, %5 sprites%6%7")
                      .arg(QString::fromStdString(m_document->romName()))
                      .arg(static_cast<int>(m_document->frameCount()))
                      .arg(showsHdAndSd() ? QStringLiteral(", %1 HD").arg(hdCount) : QString())
                      .arg(static_cast<int>(m_document->backgroundCount()))
                      .arg(static_cast<int>(m_document->spriteCount()))
                      .arg(notRenderedCount > 0
                               ? QStringLiteral(", %1 not rendered at runtime")
                                     .arg(notRenderedCount)
                               : QString())
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
            m_frameThumbnails->invalidate(item);
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
            item->setToolTip(frameListItemToolTip(frameIndex));
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
        item->setToolTip(frameListItemToolTip(frameIndex));
        m_frameThumbnails->invalidate(item);
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
