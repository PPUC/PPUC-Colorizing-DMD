#include "MainWindow.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <deque>
#include <set>

#include <QAbstractItemView>
#include <QAction>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
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

#include "DrawingGeometry.h"
#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "FrameThumbnail.h"
#include "SpriteEditorWidget.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"

namespace colorizing::app {

namespace {

constexpr int kMaxRecentProjects = 8;
constexpr char kRecentProjectsSettingsKey[] = "recentProjects";
constexpr int kExportDmdPixelScale = 8;
constexpr uint32_t kSwitchToMonochromeTriggerId = 65432;
constexpr int kMaxManualTriggerId = static_cast<int>(kSwitchToMonochromeTriggerId - 1);
constexpr std::array<uint8_t, 8> kDynamicShadowDirectionBits{
    0x01, 0x02, 0x04, 0x80, 0x08, 0x40, 0x20, 0x10,
};
constexpr std::array<const char*, 8> kDynamicShadowDirectionLabels{
    "TL", "T", "TR", "L", "R", "BL", "B", "BR",
};
constexpr std::array<int, 8> kDynamicShadowDirectionRows{0, 0, 0, 1, 1, 2, 2, 2};
constexpr std::array<int, 8> kDynamicShadowDirectionColumns{0, 1, 2, 0, 2, 0, 1, 2};

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

QString colorButtonStyle(uint16_t rgb565, bool enabled = true)
{
    const QColor color = rgb565ToColor(rgb565);
    const QString border = enabled ? QStringLiteral("#666") : QStringLiteral("#333");
    return QStringLiteral("QToolButton { background: %1; border: 1px solid %2; }"
                          "QToolButton:disabled { color: #777; }")
        .arg(color.name(), border);
}

QString spriteDisplayName(const document::ProjectDocument& document, std::size_t spriteIndex)
{
    const document::SpriteRecord& sprite = document.sprite(spriteIndex);
    const QString base = QStringLiteral("Sprite %1").arg(static_cast<int>(spriteIndex));
    if (sprite.name.empty()) {
        return base;
    }
    return QStringLiteral("%1 - %2").arg(base, QString::fromStdString(sprite.name));
}

uint16_t colorToRgb565(const QColor& color)
{
    const uint16_t r = static_cast<uint16_t>((color.red() * 31) / 255);
    const uint16_t g = static_cast<uint16_t>((color.green() * 63) / 255);
    const uint16_t b = static_cast<uint16_t>((color.blue() * 31) / 255);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

std::size_t spritePixelIndex(int x, int y)
{
    return static_cast<std::size_t>(y) * document::kMaxSpriteWidth +
           static_cast<std::size_t>(x);
}

QImage renderSpriteOriginalPreviewImage(const document::SpriteRecord& sprite,
                                        bool showDetectionAreas)
{
    QImage image(static_cast<int>(document::kMaxSpriteWidth),
                 static_cast<int>(document::kMaxSpriteHeight),
                 QImage::Format_ARGB32);
    image.fill(Qt::black);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const uint8_t value = sprite.originalSd.pixels[spritePixelIndex(x, y)];
            if (value == 255) {
                continue;
            }
            image.setPixelColor(x, y, originalIndexToColor(value));
        }
    }
    if (showDetectionAreas) {
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        const std::array<QColor, document::kMaxSpriteDetectAreas> colors{
            QColor(0, 255, 255, 92),
            QColor(255, 200, 0, 92),
            QColor(255, 0, 255, 92),
            QColor(0, 255, 0, 92),
        };
        for (std::size_t areaIndex = 0;
             areaIndex < sprite.detectionAreas.size() &&
                 areaIndex < document::kMaxSpriteDetectAreas;
             ++areaIndex) {
            const document::SpriteDetectionArea& area = sprite.detectionAreas[areaIndex];
            if (area.x == 0xffff || area.y == 0xffff ||
                area.width == 0xffff || area.height == 0xffff ||
                area.width == 0 || area.height == 0) {
                continue;
            }
            const int x = std::clamp(static_cast<int>(area.x),
                                     0,
                                     static_cast<int>(document::kMaxSpriteWidth) - 1);
            const int y = std::clamp(static_cast<int>(area.y),
                                     0,
                                     static_cast<int>(document::kMaxSpriteHeight) - 1);
            const int right = std::clamp(static_cast<int>(area.x) +
                                             static_cast<int>(area.width),
                                         0,
                                         static_cast<int>(document::kMaxSpriteWidth));
            const int bottom = std::clamp(static_cast<int>(area.y) +
                                              static_cast<int>(area.height),
                                          0,
                                          static_cast<int>(document::kMaxSpriteHeight));
            if (right <= x || bottom <= y) {
                continue;
            }
            painter.fillRect(QRect(x, y, right - x, bottom - y), colors[areaIndex]);
        }
    }
    return image;
}

QImage renderSpriteColorizedPreviewImage(const document::SpriteRecord& sprite,
                                         render::Resolution resolution)
{
    const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
    const document::IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
    const document::Rgb565Frame& colorized = useHd ? sprite.colorizedHd : sprite.colorizedSd;
    QImage image(static_cast<int>(document::kMaxSpriteWidth),
                 static_cast<int>(document::kMaxSpriteHeight),
                 QImage::Format_ARGB32);
    image.fill(Qt::black);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const std::size_t index = spritePixelIndex(x, y);
            if (mask.pixels[index] == 255) {
                continue;
            }
            image.setPixelColor(x, y, rgb565ToColor(colorized.pixels[index]));
        }
    }
    return image;
}

QImage renderBackgroundPreviewImage(const document::BackgroundRecord& background,
                                    render::Resolution resolution)
{
    const bool useHd = resolution == render::Resolution::Hd && background.colorizedHd;
    const document::Rgb565Frame& frame =
        useHd ? *background.colorizedHd : background.colorizedSd;
    QImage image(frame.size.width, frame.size.height, QImage::Format_ARGB32);
    image.fill(Qt::black);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
                static_cast<std::size_t>(x);
            image.setPixelColor(x, y, rgb565ToColor(frame.pixels[index]));
        }
    }
    return image;
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

}  // namespace

bool MainWindow::EditHistoryKey::operator<(const EditHistoryKey& other) const
{
    if (frameIndex != other.frameIndex) {
        return frameIndex < other.frameIndex;
    }
    return static_cast<int>(resolution) < static_cast<int>(other.resolution);
}

bool MainWindow::BackgroundEditHistoryKey::operator<(
    const BackgroundEditHistoryKey& other) const
{
    if (backgroundIndex != other.backgroundIndex) {
        return backgroundIndex < other.backgroundIndex;
    }
    return static_cast<int>(resolution) < static_cast<int>(other.resolution);
}

bool MainWindow::SpriteEditHistoryKey::operator<(const SpriteEditHistoryKey& other) const
{
    if (spriteIndex != other.spriteIndex) {
        return spriteIndex < other.spriteIndex;
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
    m_comparisonMaskIdSpinBox = new QSpinBox(central);
    m_comparisonMaskIdSpinBox->setToolTip(QStringLiteral("Assigned comparison mask ID"));
    m_comparisonMaskIdSpinBox->setMinimum(-1);
    m_comparisonMaskIdSpinBox->setMaximum(-1);
    m_comparisonMaskIdSpinBox->setSpecialValueText(QStringLiteral("None"));
    m_comparisonMaskIdSpinBox->setEnabled(false);
    m_dynamicSetSpinBox = new QSpinBox(central);
    m_dynamicSetSpinBox->setToolTip(QStringLiteral("Dynamic mask set to paint"));
    m_dynamicSetSpinBox->setRange(0, static_cast<int>(document::kMaxDynamicSetsPerFrame - 1));
    m_dynamicSetSpinBox->setValue(0);
    m_dynamicSetSpinBox->setEnabled(false);
    m_shapeComparisonModeComboBox = new QComboBox(central);
    m_shapeComparisonModeComboBox->setToolTip(QStringLiteral("Shape comparison mode"));
    m_shapeComparisonModeComboBox->setEnabled(false);
    m_backgroundIdSpinBox = new QSpinBox(central);
    m_backgroundIdSpinBox->setToolTip(QStringLiteral("Assigned background ID"));
    m_backgroundIdSpinBox->setMinimum(-1);
    m_backgroundIdSpinBox->setMaximum(-1);
    m_backgroundIdSpinBox->setSpecialValueText(QStringLiteral("None"));
    m_backgroundIdSpinBox->setEnabled(false);
    m_triggerIdSpinBox = new QSpinBox(central);
    m_triggerIdSpinBox->setToolTip(QStringLiteral("Per-frame trigger ID"));
    m_triggerIdSpinBox->setRange(0, kMaxManualTriggerId);
    m_triggerIdSpinBox->setEnabled(false);
    m_switchToMonochromeCheckBox = new QCheckBox(QStringLiteral("Switch mono"), central);
    m_switchToMonochromeCheckBox->setToolTip(
        QStringLiteral("Use legacy switch-to-monochrome trigger ID 65432"));
    m_switchToMonochromeCheckBox->setEnabled(false);
    m_backgroundComboBox = new QComboBox(central);
    m_backgroundComboBox->setToolTip(QStringLiteral("Loaded background"));
    m_backgroundComboBox->setEnabled(false);
    m_backgroundComboBox->setVisible(false);
    m_assignMaskButton = new QToolButton(central);
    m_assignMaskButton->setText(QStringLiteral("Assign Mask"));
    m_assignMaskButton->setToolTip(QStringLiteral("Assign selected comparison mask to selected frames"));
    m_assignMaskButton->setEnabled(false);
    m_editMaskButton = new QToolButton(central);
    m_editMaskButton->setText(QStringLiteral("Edit Mask"));
    m_editMaskButton->setToolTip(QStringLiteral("Show and edit the assigned comparison mask"));
    m_editMaskButton->setEnabled(false);
    m_assignBackgroundButton = new QToolButton(central);
    m_assignBackgroundButton->setText(QStringLiteral("Assign BG"));
    m_assignBackgroundButton->setToolTip(QStringLiteral("Assign selected background to selected frames"));
    m_assignBackgroundButton->setEnabled(false);
    m_editBackgroundButton = new QToolButton(central);
    m_editBackgroundButton->setText(QStringLiteral("Edit BG"));
    m_editBackgroundButton->setToolTip(QStringLiteral("Switch drawing target to the selected background"));
    m_editBackgroundButton->setEnabled(false);
    m_copyFrameToBackgroundButton = new QToolButton(central);
    m_copyFrameToBackgroundButton->setText(QStringLiteral("Frame -> BG"));
    m_copyFrameToBackgroundButton->setToolTip(
        QStringLiteral("Copy the current frame image into the selected background"));
    m_copyFrameToBackgroundButton->setEnabled(false);
    m_copyBackgroundToFrameButton = new QToolButton(central);
    m_copyBackgroundToFrameButton->setText(QStringLiteral("BG -> Frame"));
    m_copyBackgroundToFrameButton->setToolTip(
        QStringLiteral("Copy the selected background image into selected frames"));
    m_copyBackgroundToFrameButton->setEnabled(false);
    m_importBackgroundImageButton = new QToolButton(central);
    m_importBackgroundImageButton->setText(QStringLiteral("Import Image"));
    m_importBackgroundImageButton->setToolTip(
        QStringLiteral("Import an image into the selected background"));
    m_importBackgroundImageButton->setEnabled(false);
    m_exportBackgroundButton = new QToolButton(central);
    m_exportBackgroundButton->setText(QStringLiteral("Export BG"));
    m_exportBackgroundButton->setToolTip(QStringLiteral("Export the selected background as PNG"));
    m_exportBackgroundButton->setEnabled(false);
    m_backgroundLayerButton = new QToolButton(central);
    m_backgroundLayerButton->setText(QStringLiteral("Show BG"));
    m_backgroundLayerButton->setToolTip(QStringLiteral("Show assigned background layer"));
    m_backgroundLayerButton->setCheckable(true);
    m_backgroundLayerButton->setChecked(true);
    m_backgroundLayerButton->setEnabled(false);
    m_importFrameImageButton = new QToolButton(central);
    m_importFrameImageButton->setText(QStringLiteral("Import Image"));
    m_importFrameImageButton->setToolTip(
        QStringLiteral("Import an image into selected frame pixels"));
    m_importFrameImageButton->setEnabled(false);
    m_createHdFrameButton = new QToolButton(central);
    m_createHdFrameButton->setText(QStringLiteral("Create HD"));
    m_createHdFrameButton->setToolTip(QStringLiteral("Create HD frame from SD frame"));
    m_createHdFrameButton->setEnabled(false);
    m_removeHdFrameButton = new QToolButton(central);
    m_removeHdFrameButton->setText(QStringLiteral("Remove HD"));
    m_removeHdFrameButton->setToolTip(QStringLiteral("Remove HD frame data"));
    m_removeHdFrameButton->setEnabled(false);
    m_paintTargetComboBox = new QComboBox(central);
    m_paintTargetComboBox->setToolTip(QStringLiteral("Drawing target"));
    m_paintTargetComboBox->addItem(QStringLiteral("Frame"),
                                   static_cast<int>(PaintTarget::Frame));
    m_paintTargetComboBox->addItem(QStringLiteral("Background"),
                                   static_cast<int>(PaintTarget::Background));
    m_paintTargetComboBox->setEnabled(false);
    m_dmdPixelShapeComboBox = new QComboBox(central);
    m_dmdPixelShapeComboBox->setToolTip(QStringLiteral("DMD pixel shape for canvas and PNG export"));
    m_dmdPixelShapeComboBox->addItem(QStringLiteral("Squares"),
                                     static_cast<int>(DmdPixelShape::Square));
    m_dmdPixelShapeComboBox->addItem(QStringLiteral("Dots"),
                                     static_cast<int>(DmdPixelShape::Dot));
    m_dmdPixelSizeSpinBox = new QSpinBox(central);
    m_dmdPixelSizeSpinBox->setToolTip(QStringLiteral("Relative DMD pixel size"));
    m_dmdPixelSizeSpinBox->setRange(50, 100);
    m_dmdPixelSizeSpinBox->setSingleStep(10);
    m_dmdPixelSizeSpinBox->setSuffix(QStringLiteral("%"));
    m_dmdPixelSizeSpinBox->setValue(100);
    m_spriteComboBox = new QComboBox(central);
    m_spriteComboBox->setToolTip(QStringLiteral("Loaded sprite preview"));
    m_spriteComboBox->setEnabled(false);
    m_spriteComboBox->setVisible(false);
    m_spriteNameLineEdit = new QLineEdit(central);
    m_spriteNameLineEdit->setToolTip(QStringLiteral("Selected sprite name"));
    m_spriteNameLineEdit->setMaxLength(31);
    m_spriteNameLineEdit->setEnabled(false);
    m_spriteSlotComboBox = new QComboBox(central);
    m_spriteSlotComboBox->setToolTip(QStringLiteral("Current frame sprite slot"));
    m_spriteSlotComboBox->setEnabled(false);
    m_assignSpriteButton = new QToolButton(central);
    m_assignSpriteButton->setText(QStringLiteral("Assign"));
    m_assignSpriteButton->setToolTip(QStringLiteral("Assign selected sprite to selected slot"));
    m_assignSpriteButton->setEnabled(false);
    m_captureSpriteButton = new QToolButton(central);
    m_captureSpriteButton->setText(QStringLiteral("Capture"));
    m_captureSpriteButton->setToolTip(
        QStringLiteral("Copy original/colorized pixels from current frame bounds into selected sprite"));
    m_captureSpriteButton->setEnabled(false);
    m_spriteCaptureSelectionButton = new QToolButton(central);
    m_spriteCaptureSelectionButton->setText(QStringLiteral("Select Capture"));
    m_spriteCaptureSelectionButton->setToolTip(
        QStringLiteral("Draw a shaped sprite capture selection on the original frame"));
    m_spriteCaptureSelectionButton->setCheckable(true);
    m_spriteCaptureSelectionButton->setEnabled(false);
    m_clearSpriteCaptureSelectionButton = new QToolButton(central);
    m_clearSpriteCaptureSelectionButton->setText(QStringLiteral("Clear Sel"));
    m_clearSpriteCaptureSelectionButton->setToolTip(
        QStringLiteral("Clear the current sprite capture selection"));
    m_clearSpriteCaptureSelectionButton->setEnabled(false);
    m_removeSpriteButton = new QToolButton(central);
    m_removeSpriteButton->setText(QStringLiteral("Remove"));
    m_removeSpriteButton->setToolTip(QStringLiteral("Clear selected sprite slot"));
    m_removeSpriteButton->setEnabled(false);
    m_spriteBoundsOverlayButton = new QToolButton(central);
    m_spriteBoundsOverlayButton->setText(QStringLiteral("Show Bounds"));
    m_spriteBoundsOverlayButton->setToolTip(
        QStringLiteral("Show selected sprite slot bounds on original frame"));
    m_spriteBoundsOverlayButton->setCheckable(true);
    m_spriteBoundsOverlayButton->setChecked(false);
    m_spriteBoundsOverlayButton->setEnabled(false);
    m_spriteDetectionAreasButton = new QToolButton(central);
    m_spriteDetectionAreasButton->setText(QStringLiteral("Show Det Areas"));
    m_spriteDetectionAreasButton->setToolTip(QStringLiteral("Show sprite detection areas on original sprite preview"));
    m_spriteDetectionAreasButton->setCheckable(true);
    m_spriteDetectionAreasButton->setChecked(false);
    m_spriteDetectionAreasButton->setEnabled(false);
    m_spriteCanvasButton = new QToolButton(central);
    m_spriteCanvasButton->setText(QStringLiteral("Edit Sprite"));
    m_spriteCanvasButton->setToolTip(QStringLiteral("Show selected sprite in the main canvas"));
    m_spriteCanvasButton->setCheckable(true);
    m_spriteCanvasButton->setEnabled(false);
    m_applySpriteEditsButton = new QToolButton(central);
    m_applySpriteEditsButton->setText(QStringLiteral("Apply Sprite"));
    m_applySpriteEditsButton->setToolTip(QStringLiteral("Refresh frame previews from edited sprite data"));
    m_applySpriteEditsButton->setEnabled(false);
    m_spriteDetectionAreaComboBox = new QComboBox(central);
    m_spriteDetectionAreaComboBox->setToolTip(QStringLiteral("Sprite detection area"));
    for (std::size_t areaIndex = 0; areaIndex < document::kMaxSpriteDetectAreas; ++areaIndex) {
        m_spriteDetectionAreaComboBox->addItem(QStringLiteral("Area %1")
                                                   .arg(static_cast<int>(areaIndex)),
                                               static_cast<int>(areaIndex));
    }
    m_spriteDetectionAreaComboBox->setEnabled(false);
    m_clearSpriteDetectionAreaButton = new QToolButton(central);
    m_clearSpriteDetectionAreaButton->setText(QStringLiteral("Clear Area"));
    m_clearSpriteDetectionAreaButton->setToolTip(QStringLiteral("Disable selected sprite detection area"));
    m_clearSpriteDetectionAreaButton->setEnabled(false);
    m_spriteDetectionAreaXSpinBox = new QSpinBox(central);
    m_spriteDetectionAreaXSpinBox->setRange(0, static_cast<int>(document::kMaxSpriteWidth - 1));
    m_spriteDetectionAreaXSpinBox->setToolTip(QStringLiteral("Sprite detection area X"));
    m_spriteDetectionAreaXSpinBox->setEnabled(false);
    m_spriteDetectionAreaYSpinBox = new QSpinBox(central);
    m_spriteDetectionAreaYSpinBox->setRange(0, static_cast<int>(document::kMaxSpriteHeight - 1));
    m_spriteDetectionAreaYSpinBox->setToolTip(QStringLiteral("Sprite detection area Y"));
    m_spriteDetectionAreaYSpinBox->setEnabled(false);
    m_spriteDetectionAreaWidthSpinBox = new QSpinBox(central);
    m_spriteDetectionAreaWidthSpinBox->setRange(1, static_cast<int>(document::kMaxSpriteWidth));
    m_spriteDetectionAreaWidthSpinBox->setToolTip(QStringLiteral("Sprite detection area width"));
    m_spriteDetectionAreaWidthSpinBox->setEnabled(false);
    m_spriteDetectionAreaHeightSpinBox = new QSpinBox(central);
    m_spriteDetectionAreaHeightSpinBox->setRange(1, static_cast<int>(document::kMaxSpriteHeight));
    m_spriteDetectionAreaHeightSpinBox->setToolTip(QStringLiteral("Sprite detection area height"));
    m_spriteDetectionAreaHeightSpinBox->setEnabled(false);
    m_spriteMinXSpinBox = new QSpinBox(central);
    m_spriteMinXSpinBox->setRange(0, 127);
    m_spriteMinXSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box minimum X"));
    m_spriteMinXSpinBox->setEnabled(false);
    m_spriteMinYSpinBox = new QSpinBox(central);
    m_spriteMinYSpinBox->setRange(0, 31);
    m_spriteMinYSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box minimum Y"));
    m_spriteMinYSpinBox->setEnabled(false);
    m_spriteMaxXSpinBox = new QSpinBox(central);
    m_spriteMaxXSpinBox->setRange(0, 127);
    m_spriteMaxXSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box maximum X"));
    m_spriteMaxXSpinBox->setEnabled(false);
    m_spriteMaxYSpinBox = new QSpinBox(central);
    m_spriteMaxYSpinBox->setRange(0, 31);
    m_spriteMaxYSpinBox->setToolTip(QStringLiteral("Sprite slot bounding box maximum Y"));
    m_spriteMaxYSpinBox->setEnabled(false);
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

    auto* framePage = new QWidget(inspectorTabs);
    auto* framePageLayout = new QVBoxLayout(framePage);
    framePageLayout->setContentsMargins(6, 6, 6, 6);
    auto* frameForm = new QFormLayout();
    frameForm->addRow(QStringLiteral("Target"), m_paintTargetComboBox);
    frameForm->addRow(QStringLiteral("BG ID"), m_backgroundIdSpinBox);
    frameForm->addRow(QStringLiteral("Trigger ID"), m_triggerIdSpinBox);
    frameForm->addRow(QString(), m_switchToMonochromeCheckBox);
    frameForm->addRow(QStringLiteral("DMD Shape"), m_dmdPixelShapeComboBox);
    frameForm->addRow(QStringLiteral("DMD Size"), m_dmdPixelSizeSpinBox);
    framePageLayout->addLayout(frameForm);
    framePageLayout->addWidget(m_backgroundLayerButton);
    framePageLayout->addWidget(m_importFrameImageButton);
    auto* hdButtonLayout = new QHBoxLayout();
    hdButtonLayout->addWidget(m_createHdFrameButton);
    hdButtonLayout->addWidget(m_removeHdFrameButton);
    framePageLayout->addLayout(hdButtonLayout);
    framePageLayout->addStretch(1);
    inspectorTabs->addTab(framePage, QStringLiteral("Frame"));

    auto* maskPage = new QWidget(inspectorTabs);
    auto* maskPageLayout = new QVBoxLayout(maskPage);
    maskPageLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedMaskLabel = new QLabel(QStringLiteral("Selected: None"), maskPage);
    m_selectedMaskLabel->setWordWrap(true);
    maskPageLayout->addWidget(m_selectedMaskLabel);
    auto* maskForm = new QFormLayout();
    maskForm->addRow(QStringLiteral("Mask ID"), m_comparisonMaskIdSpinBox);
    maskForm->addRow(QStringLiteral("Shape"), m_shapeComparisonModeComboBox);
    maskPageLayout->addLayout(maskForm);
    auto* maskButtonLayout = new QHBoxLayout();
    maskButtonLayout->addWidget(m_assignMaskButton);
    maskButtonLayout->addWidget(m_editMaskButton);
    maskPageLayout->addLayout(maskButtonLayout);
    maskPageLayout->addStretch(1);
    inspectorTabs->addTab(maskPage, QStringLiteral("Mask"));

    auto* backgroundPage = new QWidget(inspectorTabs);
    auto* backgroundPageLayout = new QVBoxLayout(backgroundPage);
    backgroundPageLayout->setContentsMargins(6, 6, 6, 6);
    m_selectedBackgroundLabel = new QLabel(QStringLiteral("Selected: None"), backgroundPage);
    m_selectedBackgroundLabel->setWordWrap(true);
    backgroundPageLayout->addWidget(m_selectedBackgroundLabel);
    auto* backgroundButtonLayout = new QHBoxLayout();
    backgroundButtonLayout->addWidget(m_assignBackgroundButton);
    backgroundButtonLayout->addWidget(m_editBackgroundButton);
    backgroundPageLayout->addLayout(backgroundButtonLayout);
    auto* backgroundCopyLayout = new QHBoxLayout();
    backgroundCopyLayout->addWidget(m_copyFrameToBackgroundButton);
    backgroundCopyLayout->addWidget(m_copyBackgroundToFrameButton);
    backgroundPageLayout->addLayout(backgroundCopyLayout);
    auto* backgroundFileLayout = new QHBoxLayout();
    backgroundFileLayout->addWidget(m_importBackgroundImageButton);
    backgroundFileLayout->addWidget(m_exportBackgroundButton);
    backgroundPageLayout->addLayout(backgroundFileLayout);
    m_backgroundPreviewLabel = new QLabel(backgroundPage);
    m_backgroundPreviewLabel->setFixedSize(190, 48);
    m_backgroundPreviewLabel->setFrameShape(QFrame::Box);
    m_backgroundPreviewLabel->setAlignment(Qt::AlignCenter);
    m_backgroundPreviewLabel->setToolTip(QStringLiteral("Selected background preview"));
    m_backgroundPreviewLabel->setVisible(false);
    backgroundPageLayout->addStretch(1);
    inspectorTabs->addTab(backgroundPage, QStringLiteral("Background"));

    auto* dynamicPage = new QWidget(inspectorTabs);
    auto* dynamicPageLayout = new QVBoxLayout(dynamicPage);
    dynamicPageLayout->setContentsMargins(6, 6, 6, 6);
    auto* dynamicForm = new QFormLayout();
    dynamicForm->addRow(QStringLiteral("Dyn Set"), m_dynamicSetSpinBox);
    dynamicPageLayout->addLayout(dynamicForm);
    auto* dynamicColorsGrid = new QGridLayout();
    dynamicColorsGrid->setHorizontalSpacing(4);
    dynamicColorsGrid->setVerticalSpacing(4);
    for (int colorIndex = 0; colorIndex < 16; ++colorIndex) {
        auto* button = new QToolButton(dynamicPage);
        button->setText(QString::number(colorIndex));
        button->setToolTip(QStringLiteral("Dynamic color for original color %1").arg(colorIndex));
        button->setFixedSize(32, 24);
        button->setEnabled(false);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        dynamicColorsGrid->addWidget(button, colorIndex / 4, colorIndex % 4);
        m_dynamicColorButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this, colorIndex]() {
            editDynamicColor(static_cast<std::size_t>(colorIndex));
        });
    }
    dynamicPageLayout->addLayout(dynamicColorsGrid);
    auto* shadowTitleLabel = new QLabel(QStringLiteral("Shadow"), dynamicPage);
    shadowTitleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    dynamicPageLayout->addWidget(shadowTitleLabel);
    auto* shadowGrid = new QGridLayout();
    shadowGrid->setHorizontalSpacing(4);
    shadowGrid->setVerticalSpacing(4);
    for (std::size_t directionIndex = 0; directionIndex < kDynamicShadowDirectionBits.size();
         ++directionIndex) {
        auto* button = new QToolButton(dynamicPage);
        button->setText(QString::fromLatin1(kDynamicShadowDirectionLabels[directionIndex]));
        button->setToolTip(QStringLiteral("Toggle dynamic shadow direction %1")
                               .arg(QString::fromLatin1(
                                   kDynamicShadowDirectionLabels[directionIndex])));
        button->setCheckable(true);
        button->setFixedSize(32, 24);
        button->setEnabled(false);
        button->setProperty("shadowBit",
                            static_cast<int>(kDynamicShadowDirectionBits[directionIndex]));
        shadowGrid->addWidget(button,
                              kDynamicShadowDirectionRows[directionIndex],
                              kDynamicShadowDirectionColumns[directionIndex]);
        m_dynamicShadowDirectionButtons.push_back(button);
        connect(button, &QToolButton::toggled, this, [this, button](bool checked) {
            setDynamicShadowDirectionBit(static_cast<uint8_t>(
                                             button->property("shadowBit").toInt()),
                                         checked);
        });
    }
    auto* shadowCenterLabel = new QLabel(QStringLiteral("S"), dynamicPage);
    shadowCenterLabel->setAlignment(Qt::AlignCenter);
    shadowGrid->addWidget(shadowCenterLabel, 1, 1);
    dynamicPageLayout->addLayout(shadowGrid);
    m_dynamicShadowColorButton = new QToolButton(dynamicPage);
    m_dynamicShadowColorButton->setText(QStringLiteral("Shadow Color"));
    m_dynamicShadowColorButton->setToolTip(QStringLiteral("Dynamic shadow color for active set"));
    m_dynamicShadowColorButton->setEnabled(false);
    dynamicPageLayout->addWidget(m_dynamicShadowColorButton);
    connect(m_dynamicShadowColorButton,
            &QToolButton::clicked,
            this,
            &MainWindow::editDynamicShadowColor);
    dynamicPageLayout->addStretch(1);
    inspectorTabs->addTab(dynamicPage, QStringLiteral("Dynamic"));

    auto* spritePage = new QWidget(inspectorTabs);
    auto* spritePageLayout = new QVBoxLayout(spritePage);
    spritePageLayout->setContentsMargins(6, 6, 6, 6);
    m_frameSpritesLabel = new QLabel(QStringLiteral("No frame sprites"), spritePage);
    m_frameSpritesLabel->setWordWrap(true);
    m_frameSpritesLabel->setMinimumHeight(28);
    m_spriteOriginalPreviewLabel = new QLabel(spritePage);
    m_spriteOriginalPreviewLabel->setFixedSize(190, 48);
    m_spriteOriginalPreviewLabel->setFrameShape(QFrame::Box);
    m_spriteOriginalPreviewLabel->setAlignment(Qt::AlignCenter);
    m_spriteOriginalPreviewLabel->setToolTip(QStringLiteral("Sprite original mask preview"));
    m_spriteColorizedPreviewLabel = new QLabel(spritePage);
    m_spriteColorizedPreviewLabel->setFixedSize(190, 48);
    m_spriteColorizedPreviewLabel->setFrameShape(QFrame::Box);
    m_spriteColorizedPreviewLabel->setAlignment(Qt::AlignCenter);
    m_spriteColorizedPreviewLabel->setToolTip(QStringLiteral("Sprite colorized preview"));
    m_spriteOriginalPreviewLabel->setVisible(false);
    m_spriteColorizedPreviewLabel->setVisible(false);
    inspectorLayout->addWidget(inspectorTitle);
    inspectorLayout->addWidget(inspectorTabs, 1);
    m_selectedSpriteLabel = new QLabel(QStringLiteral("Selected: None"), spritePage);
    m_selectedSpriteLabel->setWordWrap(true);
    spritePageLayout->addWidget(m_selectedSpriteLabel);
    spritePageLayout->addWidget(m_spriteNameLineEdit);
    spritePageLayout->addWidget(m_spriteSlotComboBox);
    auto* spriteButtonLayout = new QHBoxLayout();
    spriteButtonLayout->addWidget(m_assignSpriteButton);
    spriteButtonLayout->addWidget(m_captureSpriteButton);
    spriteButtonLayout->addWidget(m_removeSpriteButton);
    spritePageLayout->addLayout(spriteButtonLayout);
    auto* spriteCaptureSelectionLayout = new QHBoxLayout();
    spriteCaptureSelectionLayout->addWidget(m_spriteCaptureSelectionButton);
    spriteCaptureSelectionLayout->addWidget(m_clearSpriteCaptureSelectionButton);
    spritePageLayout->addLayout(spriteCaptureSelectionLayout);
    auto* spriteVisualizationGrid = new QGridLayout();
    spriteVisualizationGrid->setHorizontalSpacing(4);
    spriteVisualizationGrid->setVerticalSpacing(4);
    spriteVisualizationGrid->addWidget(m_spriteBoundsOverlayButton, 0, 0);
    spriteVisualizationGrid->addWidget(m_spriteDetectionAreasButton, 0, 1);
    spriteVisualizationGrid->addWidget(m_spriteCanvasButton, 1, 0);
    spriteVisualizationGrid->addWidget(m_applySpriteEditsButton, 1, 1);
    spritePageLayout->addLayout(spriteVisualizationGrid);
    auto* spriteBoundsGrid = new QGridLayout();
    spriteBoundsGrid->setHorizontalSpacing(4);
    spriteBoundsGrid->setVerticalSpacing(4);
    spriteBoundsGrid->addWidget(new QLabel(QStringLiteral("Min X"), spritePage), 0, 0);
    spriteBoundsGrid->addWidget(m_spriteMinXSpinBox, 0, 1);
    spriteBoundsGrid->addWidget(new QLabel(QStringLiteral("Min Y"), spritePage), 0, 2);
    spriteBoundsGrid->addWidget(m_spriteMinYSpinBox, 0, 3);
    spriteBoundsGrid->addWidget(new QLabel(QStringLiteral("Max X"), spritePage), 1, 0);
    spriteBoundsGrid->addWidget(m_spriteMaxXSpinBox, 1, 1);
    spriteBoundsGrid->addWidget(new QLabel(QStringLiteral("Max Y"), spritePage), 1, 2);
    spriteBoundsGrid->addWidget(m_spriteMaxYSpinBox, 1, 3);
    spritePageLayout->addLayout(spriteBoundsGrid);
    auto* spriteDetectionAreaLayout = new QHBoxLayout();
    spriteDetectionAreaLayout->addWidget(m_spriteDetectionAreaComboBox);
    spriteDetectionAreaLayout->addWidget(m_clearSpriteDetectionAreaButton);
    spritePageLayout->addLayout(spriteDetectionAreaLayout);
    auto* spriteDetectionAreaGrid = new QGridLayout();
    spriteDetectionAreaGrid->setHorizontalSpacing(4);
    spriteDetectionAreaGrid->setVerticalSpacing(4);
    spriteDetectionAreaGrid->addWidget(new QLabel(QStringLiteral("X"), spritePage), 0, 0);
    spriteDetectionAreaGrid->addWidget(m_spriteDetectionAreaXSpinBox, 0, 1);
    spriteDetectionAreaGrid->addWidget(new QLabel(QStringLiteral("Y"), spritePage), 0, 2);
    spriteDetectionAreaGrid->addWidget(m_spriteDetectionAreaYSpinBox, 0, 3);
    spriteDetectionAreaGrid->addWidget(new QLabel(QStringLiteral("W"), spritePage), 1, 0);
    spriteDetectionAreaGrid->addWidget(m_spriteDetectionAreaWidthSpinBox, 1, 1);
    spriteDetectionAreaGrid->addWidget(new QLabel(QStringLiteral("H"), spritePage), 1, 2);
    spriteDetectionAreaGrid->addWidget(m_spriteDetectionAreaHeightSpinBox, 1, 3);
    spritePageLayout->addLayout(spriteDetectionAreaGrid);
    spritePageLayout->addWidget(m_frameSpritesLabel);
    spritePageLayout->addStretch(1);
    auto* spriteScrollArea = new QScrollArea(inspectorTabs);
    spriteScrollArea->setWidgetResizable(true);
    spriteScrollArea->setFrameShape(QFrame::NoFrame);
    spriteScrollArea->setWidget(spritePage);
    inspectorTabs->addTab(spriteScrollArea, QStringLiteral("Sprites"));
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
        if (!item || !m_comparisonMaskIdSpinBox) {
            return;
        }
        const int maskIndex = item->data(Qt::UserRole).toInt();
        if (maskIndex >= 0) {
            m_comparisonMaskIdSpinBox->setValue(maskIndex);
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
        if (!item || !m_spriteComboBox) {
            return;
        }
        const int spriteIndex = item->data(Qt::UserRole).toInt();
        const int comboIndex = m_spriteComboBox->findData(spriteIndex);
        if (comboIndex >= 0) {
            m_spriteComboBox->setCurrentIndex(comboIndex);
        }
    });
    connect(m_frameFilterComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                refreshFrameList(currentFrameIndex());
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
    connect(m_backgroundLayerButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_preview) {
            m_preview->setBackgroundLayerEnabled(checked);
        }
    });
    connect(m_importFrameImageButton, &QToolButton::clicked, this, [this]() {
        importImageIntoSelectedFrames();
    });
    connect(m_createHdFrameButton, &QToolButton::clicked, this, [this]() {
        if (isSpriteCanvasActive()) {
            createCurrentSpriteHd();
        } else {
            createCurrentFrameHd();
        }
    });
    connect(m_removeHdFrameButton, &QToolButton::clicked, this, [this]() {
        if (isSpriteCanvasActive()) {
            removeCurrentSpriteHd();
        } else {
            removeCurrentFrameHd();
        }
    });
    connect(m_paintTargetComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updatePaintTargetMode();
            });
    connect(m_dmdPixelShapeComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateDmdRenderOptions();
            });
    connect(m_dmdPixelSizeSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int) {
                updateDmdRenderOptions();
            });
    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::advancePlaybackFrame);
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
    connect(m_spriteComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateSpriteControls();
                updateUndoRedoActions();
            });
    connect(m_spriteNameLineEdit, &QLineEdit::editingFinished, this, [this]() {
        setCurrentSpriteName();
    });
    connect(m_spriteSlotComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateSpriteControls();
            });
    connect(m_assignSpriteButton, &QToolButton::clicked, this, [this]() {
        assignSelectedSpriteToCurrentSlot();
    });
    connect(m_captureSpriteButton, &QToolButton::clicked, this, [this]() {
        captureSelectedSpriteFromCurrentFrame();
    });
    connect(m_spriteCaptureSelectionButton, &QToolButton::toggled, this, [this](bool) {
        updateSpriteCaptureSelectionOverlay();
        updateSpriteControls();
    });
    connect(m_clearSpriteCaptureSelectionButton, &QToolButton::clicked, this, [this]() {
        clearSpriteCaptureSelection();
    });
    connect(m_removeSpriteButton, &QToolButton::clicked, this, [this]() {
        removeCurrentSpriteSlot();
    });
    connect(m_spriteBoundsOverlayButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_preview) {
            m_preview->setSelectedSpriteBoundsOverlayEnabled(checked);
        }
    });
    connect(m_spriteDetectionAreasButton, &QToolButton::toggled, this, [this](bool) {
        updateSpriteControls();
    });
    connect(m_spriteCanvasButton, &QToolButton::toggled, this, [this](bool checked) {
        if (!checked) {
            applyPendingSpriteDependentViews();
        }
        if (m_canvasStack) {
            m_canvasStack->setCurrentWidget(checked && m_spriteEditor ? static_cast<QWidget*>(m_spriteEditor)
                                                                      : static_cast<QWidget*>(m_preview));
        }
        updateFrameMetadataControls();
        updateSpriteControls();
        updateUndoRedoActions();
    });
    connect(m_applySpriteEditsButton, &QToolButton::clicked, this, [this]() {
        applyPendingSpriteDependentViews();
    });
    connect(m_spriteDetectionAreaComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateSpriteControls();
            });
    connect(m_clearSpriteDetectionAreaButton, &QToolButton::clicked, this, [this]() {
        clearCurrentSpriteDetectionArea();
    });
    auto connectSpriteBoundsSpinBox = [this](QSpinBox* spinBox) {
        connect(spinBox,
                static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this,
                [this](int) {
                    setCurrentSpriteSlotBounds();
                });
    };
    connectSpriteBoundsSpinBox(m_spriteMinXSpinBox);
    connectSpriteBoundsSpinBox(m_spriteMinYSpinBox);
    connectSpriteBoundsSpinBox(m_spriteMaxXSpinBox);
    connectSpriteBoundsSpinBox(m_spriteMaxYSpinBox);
    auto connectSpriteDetectionAreaSpinBox = [this](QSpinBox* spinBox) {
        connect(spinBox,
                static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this,
                [this](int) {
                    setCurrentSpriteDetectionArea();
                });
    };
    connectSpriteDetectionAreaSpinBox(m_spriteDetectionAreaXSpinBox);
    connectSpriteDetectionAreaSpinBox(m_spriteDetectionAreaYSpinBox);
    connectSpriteDetectionAreaSpinBox(m_spriteDetectionAreaWidthSpinBox);
    connectSpriteDetectionAreaSpinBox(m_spriteDetectionAreaHeightSpinBox);
    connect(m_dynamicSetSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int setIndex) {
                statusBar()->showMessage(QStringLiteral("Selected dynamic set %1").arg(setIndex),
                                         1500);
                updateDynamicColorControls();
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
    connect(m_backgroundIdSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int backgroundId) {
                setCurrentFrameBackgroundId(backgroundId);
            });
    connect(m_triggerIdSpinBox,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this,
            [this](int triggerId) {
                setCurrentFrameTriggerId(triggerId);
            });
    connect(m_switchToMonochromeCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        setCurrentFrameSwitchToMonochrome(checked);
    });
    connect(m_backgroundComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                updateBackgroundControls();
            });
    connect(m_assignMaskButton, &QToolButton::clicked, this, [this]() {
        assignSelectedMaskToFrames();
    });
    connect(m_editMaskButton, &QToolButton::clicked, this, [this]() {
        editAssignedMask();
    });
    connect(m_assignBackgroundButton, &QToolButton::clicked, this, [this]() {
        assignSelectedBackgroundToFrames();
    });
    connect(m_editBackgroundButton, &QToolButton::clicked, this, [this]() {
        editAssignedBackground();
    });
    connect(m_copyFrameToBackgroundButton, &QToolButton::clicked, this, [this]() {
        copyCurrentFrameToSelectedBackground();
    });
    connect(m_copyBackgroundToFrameButton, &QToolButton::clicked, this, [this]() {
        copySelectedBackgroundToFrames();
    });
    connect(m_importBackgroundImageButton, &QToolButton::clicked, this, [this]() {
        importImageIntoSelectedBackground();
    });
    connect(m_exportBackgroundButton, &QToolButton::clicked, this, [this]() {
        exportSelectedBackgroundPng();
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
            if (!m_spriteDetectionAreaComboBox) {
                return;
            }
            const int comboIndex =
                m_spriteDetectionAreaComboBox->findData(static_cast<int>(areaIndex));
            if (comboIndex >= 0) {
                QSignalBlocker blocker(m_spriteDetectionAreaComboBox);
                m_spriteDetectionAreaComboBox->setCurrentIndex(comboIndex);
            }
            const int spriteIndex = m_spriteComboBox ? m_spriteComboBox->currentData().toInt() : -1;
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

    if (QFileInfo::exists(targetCromPath) && !QFile::remove(targetCromPath)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project As Failed"),
                              QStringLiteral("Could not replace target cROM."));
        return;
    }
    if (QFileInfo::exists(targetCrpPath) && !QFile::remove(targetCrpPath)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project As Failed"),
                              QStringLiteral("Could not replace target cRP."));
        return;
    }
    if (!QFile::copy(m_currentCromPath, targetCromPath)) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project As Failed"),
                              QStringLiteral("Could not copy cROM template."));
        return;
    }
    if (!QFile::copy(m_currentCrpPath, targetCrpPath)) {
        QFile::remove(targetCromPath);
        QMessageBox::critical(this,
                              QStringLiteral("Save Project As Failed"),
                              QStringLiteral("Could not copy cRP file."));
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
        m_backgroundLayerButton && m_backgroundLayerButton->isChecked();
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

            PixelEditCommand command;
            command.frameIndex = frameIndex;
            command.resolution = targetResolution;
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
                    command.changes.push_back(PixelChange{x, y, oldColor, newColor});
                }
            }

            if (command.changes.empty()) {
                continue;
            }
            applyPixelEdit(command, true);
            EditHistory& history = m_editHistories[EditHistoryKey{frameIndex, targetResolution}];
            history.undoStack.push_back(std::move(command));
            history.redoStack.clear();
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

    try {
        m_document = document::ProjectDocument::fromSnapshot(result.snapshot);
        m_currentCromPath = cromPath;
        m_currentCrpPath = crpPath;
        rememberRecentProject(cromPath, crpPath);
        m_editHistories.clear();
        m_maskEditHistories.clear();
        m_dynamicMaskEditHistories.clear();
        m_dynamicColorEditHistories.clear();
        m_dynamicShadowEditHistories.clear();
        m_backgroundMaskEditHistories.clear();
        m_backgroundImageEditHistories.clear();
        m_spriteImageEditHistories.clear();
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
    if (m_applySpriteEditsButton) {
        m_applySpriteEditsButton->setEnabled(false);
    }
    m_preview->setDocument(m_document ? &*m_document : nullptr);
    m_preview->setBackgroundLayerEnabled(m_backgroundLayerButton &&
                                         m_backgroundLayerButton->isChecked());
    m_preview->setSelectedSpriteBoundsOverlayEnabled(
        m_spriteBoundsOverlayButton && m_spriteBoundsOverlayButton->isChecked());
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
        const bool hasHd = m_document->hasHdFrame(frameIndex);
        const uint32_t durationMs = m_document->frameDurationMs(frameIndex);
        QStringList details;
        details << QStringLiteral("%1 ms").arg(durationMs);
        if (hasHd) {
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
        auto* item = new QListWidgetItem(
            makeFrameThumbnail(*m_document, frameIndex, QSize(160, 84)),
            QStringLiteral("%1\n%2")
                .arg(static_cast<int>(frameIndex))
                .arg(details.join(QStringLiteral("\n"))));
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

    updateMaskBrowserSelection();
    updateBackgroundBrowserSelection();
    updateSpriteBrowserSelection();
    updateMaskControls();
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
        updatePlaybackControls();
        return;
    }

    applyPendingSpriteDependentViews();
    if (m_spriteCanvasButton && m_spriteCanvasButton->isChecked()) {
        QSignalBlocker blocker(m_spriteCanvasButton);
        m_spriteCanvasButton->setChecked(false);
    }
    if (m_canvasStack && m_preview) {
        m_canvasStack->setCurrentWidget(m_preview);
    }
    m_playbackTimer->start(playbackIntervalForCurrentFrame());
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
    if (m_importFrameImageButton) {
        m_importFrameImageButton->setEnabled(hasDocument && !selectedFrameIndices().empty());
    }
    if (m_dynamicSetSpinBox) {
        m_dynamicSetSpinBox->setEnabled(hasDocument);
    }
    if (m_paintTargetComboBox) {
        m_paintTargetComboBox->setEnabled(hasDocument && selectedBackgroundIndex().has_value());
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
    updateSpriteControls();
    updateBackgroundControls();
    refreshComponentBrowsers();
    updateUndoRedoActions();
    updatePaintTargetMode();
}

void MainWindow::updateFrameMetadataControls()
{
    if (!m_comparisonMaskIdSpinBox || !m_shapeComparisonModeComboBox ||
        !m_backgroundIdSpinBox || !m_triggerIdSpinBox ||
        !m_switchToMonochromeCheckBox) {
        return;
    }

    const std::optional<std::size_t> currentIndex = currentFrameIndex();
    const bool hasFrame = m_document && currentIndex.has_value();
    QSignalBlocker maskBlocker(m_comparisonMaskIdSpinBox);
    QSignalBlocker shapeBlocker(m_shapeComparisonModeComboBox);
    QSignalBlocker backgroundBlocker(m_backgroundIdSpinBox);
    QSignalBlocker triggerBlocker(m_triggerIdSpinBox);
    QSignalBlocker monoBlocker(m_switchToMonochromeCheckBox);

    if (!hasFrame) {
        m_comparisonMaskIdSpinBox->setRange(-1, -1);
        m_comparisonMaskIdSpinBox->setValue(-1);
        m_comparisonMaskIdSpinBox->setEnabled(false);
        m_shapeComparisonModeComboBox->clear();
        m_shapeComparisonModeComboBox->addItem(QStringLiteral("Full comparison"), 0);
        m_shapeComparisonModeComboBox->addItem(QStringLiteral("Shape mode"), 1);
        m_shapeComparisonModeComboBox->setCurrentIndex(0);
        m_shapeComparisonModeComboBox->setEnabled(false);
        m_backgroundIdSpinBox->setRange(-1, -1);
        m_backgroundIdSpinBox->setValue(-1);
        m_backgroundIdSpinBox->setEnabled(false);
        m_triggerIdSpinBox->setValue(0);
        m_triggerIdSpinBox->setEnabled(false);
        m_switchToMonochromeCheckBox->setChecked(false);
        m_switchToMonochromeCheckBox->setEnabled(false);
        if (m_backgroundLayerButton) {
            m_backgroundLayerButton->setEnabled(false);
        }
        if (m_importFrameImageButton) {
            m_importFrameImageButton->setEnabled(false);
        }
        if (m_createHdFrameButton) {
            m_createHdFrameButton->setEnabled(false);
        }
        if (m_removeHdFrameButton) {
            m_removeHdFrameButton->setEnabled(false);
        }
        if (m_paintTargetComboBox) {
            m_paintTargetComboBox->setEnabled(false);
            m_paintTargetComboBox->setCurrentIndex(0);
        }
        updateDynamicColorControls();
        updateMaskControls();
        updateBackgroundControls();
        return;
    }

    const std::size_t frameIndex = *currentIndex;
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

    const int maxBackgroundId = m_document->backgroundCount() == 0
                                    ? -1
                                    : static_cast<int>(m_document->backgroundCount() - 1);
    m_backgroundIdSpinBox->setRange(-1, maxBackgroundId);
    int backgroundValue = -1;
    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(frameIndex);
    if (backgroundData && backgroundData->backgroundId <= maxBackgroundId) {
        backgroundValue = static_cast<int>(backgroundData->backgroundId);
    }
    m_backgroundIdSpinBox->setValue(backgroundValue);
    m_backgroundIdSpinBox->setEnabled(backgroundData.has_value() && maxBackgroundId >= 0);
    const uint32_t triggerId = m_document->triggerId(frameIndex);
    const bool switchToMonochrome = triggerId == kSwitchToMonochromeTriggerId;
    m_switchToMonochromeCheckBox->setChecked(switchToMonochrome);
    m_switchToMonochromeCheckBox->setEnabled(true);
    const bool triggerIdEditable = !switchToMonochrome;
    m_triggerIdSpinBox->setEnabled(triggerIdEditable);
    m_triggerIdSpinBox->setValue(triggerId <= static_cast<uint32_t>(kMaxManualTriggerId)
                                     ? static_cast<int>(triggerId)
                                     : 0);
    m_triggerIdSpinBox->setToolTip(
        triggerId <= static_cast<uint32_t>(kMaxManualTriggerId) || switchToMonochrome
            ? QStringLiteral("Per-frame trigger ID")
            : QStringLiteral("Unsupported legacy trigger ID %1 is preserved until edited")
                  .arg(triggerId));
    if (m_backgroundLayerButton) {
        m_backgroundLayerButton->setEnabled(backgroundData.has_value() && maxBackgroundId >= 0);
    }
    if (m_importFrameImageButton) {
        m_importFrameImageButton->setEnabled(!selectedFrameIndices().empty());
    }
    const bool hasHdFrame = m_document->hasHdFrame(frameIndex);
    if (m_createHdFrameButton) {
        m_createHdFrameButton->setText(QStringLiteral("Create HD"));
        m_createHdFrameButton->setToolTip(QStringLiteral("Create HD frame from SD frame"));
        m_createHdFrameButton->setEnabled(!hasHdFrame);
    }
    if (m_removeHdFrameButton) {
        m_removeHdFrameButton->setText(QStringLiteral("Remove HD"));
        m_removeHdFrameButton->setToolTip(QStringLiteral("Remove HD frame data"));
        m_removeHdFrameButton->setEnabled(hasHdFrame);
    }
    if (m_paintTargetComboBox) {
        const bool hasSelectableBackground = m_document->backgroundCount() > 0;
        m_paintTargetComboBox->setEnabled(hasSelectableBackground);
        if (!hasSelectableBackground) {
            m_paintTargetComboBox->setCurrentIndex(0);
        }
    }
    updateDynamicColorControls();
    updatePaintTargetMode();
    updateBackgroundControls();
    updateMaskBrowserSelection();
    updateMaskControls();
}

void MainWindow::updateMaskControls()
{
    if (!m_selectedMaskLabel || !m_assignMaskButton || !m_editMaskButton) {
        return;
    }

    const bool hasDocument = static_cast<bool>(m_document);
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    const QListWidgetItem* selectedItem = m_maskList ? m_maskList->currentItem() : nullptr;
    const int selectedMask = selectedItem ? selectedItem->data(Qt::UserRole).toInt() : -1;
    const bool selectedMaskValid =
        hasDocument && selectedMask >= 0 &&
        static_cast<std::size_t>(selectedMask) < m_document->comparisonMaskCount();
    const std::optional<std::size_t> assignedMask = currentComparisonMaskIndex();

    if (selectedMaskValid) {
        const QString assignedSuffix =
            assignedMask && *assignedMask == static_cast<std::size_t>(selectedMask)
                ? QStringLiteral(" (assigned)")
                : QString();
        m_selectedMaskLabel->setText(
            QStringLiteral("Selected: %1%2").arg(selectedMask).arg(assignedSuffix));
    } else if (assignedMask) {
        m_selectedMaskLabel->setText(
            QStringLiteral("Selected: %1 (assigned)").arg(static_cast<int>(*assignedMask)));
    } else {
        m_selectedMaskLabel->setText(QStringLiteral("Selected: None"));
    }

    m_assignMaskButton->setEnabled(selectedMaskValid && !frameIndices.empty());
    m_editMaskButton->setEnabled(assignedMask.has_value());
}

void MainWindow::updateBackgroundControls()
{
    if (!m_backgroundComboBox || !m_backgroundPreviewLabel ||
        !m_assignBackgroundButton || !m_editBackgroundButton ||
        !m_copyFrameToBackgroundButton || !m_copyBackgroundToFrameButton ||
        !m_importBackgroundImageButton || !m_exportBackgroundButton) {
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

    const int selectedBackground = m_backgroundComboBox->currentData().toInt();
    const bool hasBackground =
        m_document && selectedBackground >= 0 &&
        static_cast<std::size_t>(selectedBackground) < m_document->backgroundCount();
    m_backgroundComboBox->setEnabled(m_document && m_document->backgroundCount() > 0);
    m_assignBackgroundButton->setEnabled(hasBackground && !selectedFrameIndices().empty());
    m_copyFrameToBackgroundButton->setEnabled(hasBackground && currentFrameIndex().has_value());
    m_copyBackgroundToFrameButton->setEnabled(hasBackground && !selectedFrameIndices().empty());
    m_importBackgroundImageButton->setEnabled(hasBackground);
    m_exportBackgroundButton->setEnabled(hasBackground);
    m_editBackgroundButton->setEnabled(hasBackground);

    if (!hasBackground) {
        if (m_selectedBackgroundLabel) {
            m_selectedBackgroundLabel->setText(QStringLiteral("Selected: None"));
        }
        m_backgroundPreviewLabel->clear();
        m_backgroundPreviewLabel->setText(QStringLiteral("Background"));
        m_copyFrameToBackgroundButton->setEnabled(false);
        m_copyBackgroundToFrameButton->setEnabled(false);
        m_importBackgroundImageButton->setEnabled(false);
        m_exportBackgroundButton->setEnabled(false);
        if (m_preview) {
            m_preview->setSelectedBackgroundIndex(std::nullopt);
        }
        return;
    }

    const document::BackgroundRecord& background =
        m_document->background(static_cast<std::size_t>(selectedBackground));
    if (m_selectedBackgroundLabel) {
        m_selectedBackgroundLabel->setText(QStringLiteral("Selected: %1%2")
                                               .arg(selectedBackground)
                                               .arg(background.colorizedHd
                                                        ? QStringLiteral(" (HD)")
                                                        : QString()));
    }
    m_backgroundPreviewLabel->setPixmap(
        previewPixmap(renderBackgroundPreviewImage(background, m_activeResolution),
                      m_backgroundPreviewLabel->size()));
    if (m_preview) {
        m_preview->setSelectedBackgroundIndex(static_cast<std::size_t>(selectedBackground));
    }
    updateBackgroundBrowserSelection();
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
    if (m_switchToMonochromeCheckBox && m_switchToMonochromeCheckBox->isChecked()) {
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

void MainWindow::setCurrentFrameSwitchToMonochrome(bool enabled)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty()) {
        return;
    }

    const uint32_t storedTriggerId =
        enabled ? kSwitchToMonochromeTriggerId
                : static_cast<uint32_t>(m_triggerIdSpinBox
                                            ? std::clamp(m_triggerIdSpinBox->value(),
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
    if (!m_paintTargetComboBox) {
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
    const int targetIndex =
        m_paintTargetComboBox->findData(static_cast<int>(PaintTarget::Background));
    if (targetIndex >= 0) {
        m_paintTargetComboBox->setCurrentIndex(targetIndex);
    }
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
        BackgroundImageEditCommand command;
        command.backgroundIndex = *backgroundIndex;
        command.resolution = targetResolution;
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
                command.changes.push_back(PixelChange{x, y, oldColor, newColor});
            }
        }
        if (command.changes.empty()) {
            statusBar()->showMessage(QStringLiteral("Background already matches frame"), 2500);
            return;
        }
        applyBackgroundImageEdit(command, true);
        BackgroundImageEditHistory& history =
            m_backgroundImageEditHistories[BackgroundEditHistoryKey{*backgroundIndex,
                                                                    targetResolution}];
        history.undoStack.push_back(std::move(command));
        history.redoStack.clear();
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
            PixelEditCommand command;
            command.frameIndex = frameIndex;
            command.resolution = sourceResolution;
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
                    command.changes.push_back(PixelChange{x, y, oldColor, newColor});
                }
            }
            if (command.changes.empty()) {
                continue;
            }
            applyPixelEdit(command, true);
            EditHistory& history = m_editHistories[EditHistoryKey{frameIndex, sourceResolution}];
            history.undoStack.push_back(std::move(command));
            history.redoStack.clear();
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
            m_document->setBackgroundFrame(*backgroundIndex, targetResolution, imported);
        } else {
            const document::Rgb565Frame& oldBackground =
                m_document->backgroundFrame(*backgroundIndex, targetResolution);
            BackgroundImageEditCommand command;
            command.backgroundIndex = *backgroundIndex;
            command.resolution = targetResolution;
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
                    command.changes.push_back(PixelChange{x, y, oldColor, newColor});
                }
            }
            if (command.changes.empty()) {
                statusBar()->showMessage(QStringLiteral("Background already matches image"), 2500);
                return;
            }
            applyBackgroundImageEdit(command, true);
            BackgroundImageEditHistory& history =
                m_backgroundImageEditHistories[BackgroundEditHistoryKey{*backgroundIndex,
                                                                        targetResolution}];
            history.undoStack.push_back(std::move(command));
            history.redoStack.clear();
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
    if (!m_document || frameIndices.empty() || !m_spriteComboBox || !m_spriteSlotComboBox) {
        return;
    }

    const int spriteIndex = m_spriteComboBox->currentData().toInt();
    const int slotIndex = m_spriteSlotComboBox->currentData().toInt();
    if (spriteIndex < 0 || slotIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    try {
        for (std::size_t frameIndex : frameIndices) {
            std::vector<document::FrameSpriteReference> references =
                m_document->frameSpriteReferences(frameIndex);
            if (static_cast<std::size_t>(slotIndex) >= references.size()) {
                continue;
            }
            document::FrameSpriteReference reference =
                references[static_cast<std::size_t>(slotIndex)];
            reference.spriteId = static_cast<uint8_t>(spriteIndex);
            m_document->setFrameSpriteReference(frameIndex,
                                                static_cast<std::size_t>(slotIndex),
                                                reference);
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
                .arg(slotIndex)
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
    if (!m_document || !frameIndex || !spriteIndex || !m_spriteSlotComboBox) {
        statusBar()->showMessage(QStringLiteral("Select a frame and sprite first"), 2500);
        updateSpriteControls();
        return;
    }

    const int slotIndex = m_spriteSlotComboBox->currentData().toInt();
    if (slotIndex < 0) {
        statusBar()->showMessage(QStringLiteral("Select a sprite slot first"), 2500);
        updateSpriteControls();
        return;
    }

    const int minX = std::clamp(m_spriteMinXSpinBox ? m_spriteMinXSpinBox->value() : 0,
                                0,
                                render::kSdFrameSize.width - 1);
    const int minY = std::clamp(m_spriteMinYSpinBox ? m_spriteMinYSpinBox->value() : 0,
                                0,
                                render::kSdFrameSize.height - 1);
    const int maxX = std::clamp(m_spriteMaxXSpinBox ? m_spriteMaxXSpinBox->value() : minX,
                                minX,
                                render::kSdFrameSize.width - 1);
    const int maxY = std::clamp(m_spriteMaxYSpinBox ? m_spriteMaxYSpinBox->value() : minY,
                                minY,
                                render::kSdFrameSize.height - 1);
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
            m_backgroundLayerButton && m_backgroundLayerButton->isChecked();
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
        m_document->setSpriteDetectionArea(*spriteIndex,
                                           0,
                                           document::SpriteDetectionArea{
                                               0,
                                               0,
                                               static_cast<uint16_t>(captureWidth),
                                               static_cast<uint16_t>(captureHeight),
                                           });
        m_document->setFrameSpriteReference(*frameIndex,
                                            static_cast<std::size_t>(slotIndex),
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
    return m_spriteCaptureSelectionButton &&
           m_spriteCaptureSelectionButton->isChecked() &&
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
    if (!m_document || frameIndices.empty() || !m_spriteSlotComboBox) {
        return;
    }

    const int slotIndex = m_spriteSlotComboBox->currentData().toInt();
    if (slotIndex < 0) {
        updateSpriteControls();
        return;
    }

    try {
        for (std::size_t frameIndex : frameIndices) {
            m_document->clearFrameSpriteReference(frameIndex,
                                                  static_cast<std::size_t>(slotIndex));
            updateFrameListItem(frameIndex);
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Cleared sprite slot %1 on %2 frame(s)")
                .arg(slotIndex)
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
    if (!m_document || frameIndices.empty() || !m_spriteSlotComboBox ||
        !m_spriteMinXSpinBox || !m_spriteMinYSpinBox ||
        !m_spriteMaxXSpinBox || !m_spriteMaxYSpinBox) {
        return;
    }

    const int slotIndex = m_spriteSlotComboBox->currentData().toInt();
    if (slotIndex < 0) {
        updateSpriteControls();
        return;
    }

    const int minX = std::min(m_spriteMinXSpinBox->value(), m_spriteMaxXSpinBox->value());
    const int maxX = std::max(m_spriteMinXSpinBox->value(), m_spriteMaxXSpinBox->value());
    const int minY = std::min(m_spriteMinYSpinBox->value(), m_spriteMaxYSpinBox->value());
    const int maxY = std::max(m_spriteMinYSpinBox->value(), m_spriteMaxYSpinBox->value());

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            std::vector<document::FrameSpriteReference> references =
                m_document->frameSpriteReferences(frameIndex);
            if (static_cast<std::size_t>(slotIndex) >= references.size()) {
                continue;
            }
            document::FrameSpriteReference reference =
                references[static_cast<std::size_t>(slotIndex)];
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
            m_document->setFrameSpriteReference(frameIndex,
                                                static_cast<std::size_t>(slotIndex),
                                                updated);
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
                .arg(slotIndex)
                .arg(static_cast<int>(frameIndices.size())),
            1500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentSpriteName()
{
    if (!m_document || !m_spriteComboBox || !m_spriteNameLineEdit) {
        return;
    }
    const int spriteIndex = m_spriteComboBox->currentData().toInt();
    if (spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    try {
        const QString name = m_spriteNameLineEdit->text();
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
    if (!m_document || !m_spriteComboBox || !m_spriteDetectionAreaComboBox ||
        !m_spriteDetectionAreaXSpinBox || !m_spriteDetectionAreaYSpinBox ||
        !m_spriteDetectionAreaWidthSpinBox || !m_spriteDetectionAreaHeightSpinBox) {
        return;
    }

    const int spriteIndex = m_spriteComboBox->currentData().toInt();
    const int areaIndex = m_spriteDetectionAreaComboBox->currentData().toInt();
    if (spriteIndex < 0 || areaIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    const int x = m_spriteDetectionAreaXSpinBox->value();
    const int y = m_spriteDetectionAreaYSpinBox->value();
    const int width = std::min(m_spriteDetectionAreaWidthSpinBox->value(),
                               static_cast<int>(document::kMaxSpriteWidth) - x);
    const int height = std::min(m_spriteDetectionAreaHeightSpinBox->value(),
                                static_cast<int>(document::kMaxSpriteHeight) - y);
    if (width <= 0 || height <= 0) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                           static_cast<std::size_t>(areaIndex),
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
                .arg(areaIndex),
            1500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::clearCurrentSpriteDetectionArea()
{
    if (!m_document || !m_spriteComboBox || !m_spriteDetectionAreaComboBox) {
        return;
    }

    const int spriteIndex = m_spriteComboBox->currentData().toInt();
    const int areaIndex = m_spriteDetectionAreaComboBox->currentData().toInt();
    if (spriteIndex < 0 || areaIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        updateSpriteControls();
        return;
    }

    try {
        m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                           static_cast<std::size_t>(areaIndex),
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
                .arg(areaIndex),
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
        const document::SpriteRecord& sprite = m_document->sprite(spriteIndex);
        const document::IndexedFrame& mask =
            resolution == render::Resolution::Hd && sprite.hasHd ? sprite.maskHd : sprite.originalSd;
        SpriteImageEditCommand command;
        command.spriteIndex = spriteIndex;
        command.resolution = resolution;
        std::set<std::pair<int, int>> seen;
        for (const render::Point point : pixels) {
            if (!seen.insert({point.x, point.y}).second) {
                continue;
            }
            if (point.x < 0 || point.y < 0 ||
                point.x >= static_cast<int>(document::kMaxSpriteWidth) ||
                point.y >= static_cast<int>(document::kMaxSpriteHeight)) {
                continue;
            }
            if (mask.pixels[spritePixelIndex(point.x, point.y)] == 255) {
                continue;
            }
            const uint16_t currentColor =
                m_document->spriteColorizedPixel(spriteIndex, resolution, point.x, point.y);
            if (!erase && currentColor == 0) {
                continue;
            }
            if (currentColor == color) {
                continue;
            }
            command.changes.push_back(PixelChange{
                point.x,
                point.y,
                currentColor,
                color,
            });
        }
        if (command.changes.empty()) {
            return;
        }
        applySpriteImageEdit(command, true);
        SpriteImageEditHistory& history =
            m_spriteImageEditHistories[SpriteEditHistoryKey{spriteIndex, resolution}];
        history.undoStack.push_back(command);
        history.redoStack.clear();
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
        const document::SpriteRecord& sprite = m_document->sprite(spriteIndex);
        const bool useHd = resolution == render::Resolution::Hd && sprite.hasHd;
        const document::IndexedFrame& mask = useHd ? sprite.maskHd : sprite.originalSd;
        const document::Rgb565Frame& frame = useHd ? sprite.colorizedHd : sprite.colorizedSd;
        const uint16_t targetColor = m_document->spriteColorizedPixel(spriteIndex, resolution, x, y);
        if (targetColor == m_activeDrawColor || targetColor == 0 ||
            mask.pixels[spritePixelIndex(x, y)] == 255) {
            return;
        }

        std::vector<uint8_t> visited(frame.pixels.size(), 0);
        std::deque<render::Point> queue;
        std::vector<render::Point> pixels;
        queue.push_back(render::Point{x, y});

        while (!queue.empty()) {
            const render::Point point = queue.front();
            queue.pop_front();
            if (point.x < 0 || point.y < 0 ||
                point.x >= frame.size.width || point.y >= frame.size.height) {
                continue;
            }
            const std::size_t index = spritePixelIndex(point.x, point.y);
            if (visited[index]) {
                continue;
            }
            visited[index] = 1;
            if (mask.pixels[index] == 255 || frame.pixels[index] != targetColor) {
                continue;
            }

            pixels.push_back(point);
            queue.push_back(render::Point{point.x - 1, point.y});
            queue.push_back(render::Point{point.x + 1, point.y});
            queue.push_back(render::Point{point.x, point.y - 1});
            queue.push_back(render::Point{point.x, point.y + 1});
        }

        paintSpriteColorizedPixels(spriteIndex, resolution, pixels, false);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setSpriteDependentViewsDirty()
{
    m_spriteDependentViewsDirty = true;
    if (m_applySpriteEditsButton) {
        m_applySpriteEditsButton->setEnabled(true);
    }
}

void MainWindow::applyPendingSpriteDependentViews()
{
    if (!m_spriteDependentViewsDirty) {
        return;
    }
    m_spriteDependentViewsDirty = false;
    if (m_applySpriteEditsButton) {
        m_applySpriteEditsButton->setEnabled(false);
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
        m_document->createHdFrame(*frameIndex);
        if (dynamicMaskCheckBox->isChecked()) {
            m_document->upscaleDynamicMaskToHd(*frameIndex);
        }
        if (backgroundMaskCheckBox->isChecked()) {
            m_document->upscaleBackgroundMaskToHd(*frameIndex);
        }
        if (backgroundImageCheckBox->isChecked()) {
            m_document->upscaleAssignedBackgroundToHd(*frameIndex);
        }
        m_activeResolution = render::Resolution::Hd;
        updateResolutionMode();
        updateFrameMetadataControls();
        updateFrameListItem(*frameIndex);
        updateProjectHeaderStatus();
        statusBar()->showMessage(QStringLiteral("Created HD frame from SD"), 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
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
        if (m_activeResolution == render::Resolution::Hd) {
            m_activeResolution = render::Resolution::Sd;
        }
        updateResolutionMode();
        updateFrameMetadataControls();
        updateFrameListItem(*frameIndex);
        updateProjectHeaderStatus();
        statusBar()->showMessage(QStringLiteral("Removed HD frame"), 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updateDynamicColorControls()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const bool hasFrame = m_document && frameIndex.has_value();
    const std::optional<document::FrameDynamicData>* dynamicData = nullptr;
    if (hasFrame) {
        dynamicData = &m_document->dynamicData(*frameIndex);
    }

    std::size_t colorCount = 0;
    if (dynamicData && dynamicData->has_value()) {
        const std::vector<uint16_t>& colors =
            m_activeResolution == render::Resolution::Hd ? (*dynamicData)->colorsHd
                                                         : (*dynamicData)->colorsSd;
        colorCount = colors.size() / document::kMaxDynamicSetsPerFrame;
    }

    for (std::size_t colorIndex = 0; colorIndex < m_dynamicColorButtons.size(); ++colorIndex) {
        QToolButton* button = m_dynamicColorButtons[colorIndex];
        const bool enabled = dynamicData && dynamicData->has_value() && colorIndex < colorCount;
        button->setEnabled(enabled);
        if (!enabled) {
            button->setToolTip(QStringLiteral("Dynamic color is not available"));
            button->setStyleSheet(colorButtonStyle(0, false));
            continue;
        }

        const uint16_t rgb565 = m_document->dynamicColor(*frameIndex,
                                                         m_activeResolution,
                                                         activeDynamicSet(),
                                                         colorIndex);
        button->setProperty("rgb565", static_cast<int>(rgb565));
        button->setToolTip(QStringLiteral("Set %1 original %2: 0x%3")
                               .arg(activeDynamicSet())
                               .arg(static_cast<int>(colorIndex))
                               .arg(rgb565, 4, 16, QLatin1Char('0')));
        button->setStyleSheet(colorButtonStyle(rgb565));
    }
    updateDynamicShadowControls();
}

void MainWindow::updateDynamicShadowControls()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const bool enabled = m_document && frameIndex &&
                         m_document->dynamicData(*frameIndex).has_value();
    uint8_t direction = 0;
    uint16_t color = 0;
    if (enabled) {
        try {
            direction = m_document->dynamicShadowDirection(*frameIndex,
                                                           m_activeResolution,
                                                           activeDynamicSet());
            color = m_document->dynamicShadowColor(*frameIndex,
                                                   m_activeResolution,
                                                   activeDynamicSet());
        } catch (const std::exception&) {
            direction = 0;
            color = 0;
        }
    }

    for (QToolButton* button : m_dynamicShadowDirectionButtons) {
        QSignalBlocker blocker(button);
        const uint8_t bit = static_cast<uint8_t>(button->property("shadowBit").toInt());
        button->setEnabled(enabled);
        button->setChecked(enabled && (direction & bit) != 0);
    }

    if (m_dynamicShadowColorButton) {
        m_dynamicShadowColorButton->setEnabled(enabled);
        m_dynamicShadowColorButton->setProperty("rgb565", static_cast<int>(color));
        m_dynamicShadowColorButton->setStyleSheet(colorButtonStyle(color, enabled));
        m_dynamicShadowColorButton->setToolTip(
            enabled ? QStringLiteral("Set %1 shadow color: 0x%2")
                          .arg(activeDynamicSet())
                          .arg(color, 4, 16, QLatin1Char('0'))
                    : QStringLiteral("Dynamic shadow color is not available"));
    }
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
        DynamicColorEditCommand command;
        command.frameIndex = *frameIndex;
        command.resolution = m_activeResolution;
        command.setIndex = activeDynamicSet();
        command.originalColorIndex = originalColorIndex;
        command.oldColor = oldRgb565;
        command.newColor = newRgb565;
        applyDynamicColorEdit(command, true);
        DynamicColorEditHistory& history =
            m_dynamicColorEditHistories[EditHistoryKey{*frameIndex, m_activeResolution}];
        history.undoStack.push_back(command);
        history.redoStack.clear();
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
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }

    DynamicShadowEditCommand command;
    command.resolution = m_activeResolution;
    command.setIndex = activeDynamicSet();
    command.editDirection = true;

    try {
        for (std::size_t selectedFrameIndex : selectedFrameIndices()) {
            if (!m_document->dynamicData(selectedFrameIndex).has_value()) {
                continue;
            }
            const uint8_t oldDirection =
                m_document->dynamicShadowDirection(selectedFrameIndex,
                                                   m_activeResolution,
                                                   activeDynamicSet());
            const uint8_t newDirection =
                enabled ? static_cast<uint8_t>(oldDirection | bit)
                        : static_cast<uint8_t>(oldDirection & ~bit);
            if (newDirection == oldDirection) {
                continue;
            }
            DynamicShadowEditChange change;
            change.frameIndex = selectedFrameIndex;
            change.oldDirection = oldDirection;
            change.newDirection = newDirection;
            change.oldColor = m_document->dynamicShadowColor(selectedFrameIndex,
                                                             m_activeResolution,
                                                             activeDynamicSet());
            change.newColor = change.oldColor;
            command.changes.push_back(change);
        }
        if (command.changes.empty()) {
            updateDynamicShadowControls();
            return;
        }
        applyDynamicShadowEdit(command, true);
        DynamicShadowEditHistory& history =
            m_dynamicShadowEditHistories[EditHistoryKey{*frameIndex, m_activeResolution}];
        history.undoStack.push_back(command);
        history.redoStack.clear();
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Updated dynamic shadow direction"), 2500);
    } catch (const std::exception& ex) {
        updateDynamicShadowControls();
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

        DynamicShadowEditCommand command;
        command.resolution = m_activeResolution;
        command.setIndex = activeDynamicSet();
        command.editColor = true;
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
            DynamicShadowEditChange change;
            change.frameIndex = selectedFrameIndex;
            change.oldDirection = m_document->dynamicShadowDirection(selectedFrameIndex,
                                                                     m_activeResolution,
                                                                     activeDynamicSet());
            change.newDirection = change.oldDirection;
            change.oldColor = oldColor;
            change.newColor = newRgb565;
            command.changes.push_back(change);
        }
        if (command.changes.empty()) {
            return;
        }
        applyDynamicShadowEdit(command, true);
        DynamicShadowEditHistory& history =
            m_dynamicShadowEditHistories[EditHistoryKey{*frameIndex, m_activeResolution}];
        history.undoStack.push_back(command);
        history.redoStack.clear();
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Updated dynamic shadow color 0x%1")
                                     .arg(newRgb565, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        updateDynamicShadowControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updateSpriteControls()
{
    if (!m_spriteComboBox || !m_spriteOriginalPreviewLabel ||
        !m_spriteColorizedPreviewLabel || !m_frameSpritesLabel ||
        !m_spriteSlotComboBox || !m_assignSpriteButton || !m_removeSpriteButton ||
        !m_captureSpriteButton || !m_spriteCaptureSelectionButton ||
        !m_clearSpriteCaptureSelectionButton ||
        !m_spriteBoundsOverlayButton || !m_spriteDetectionAreasButton ||
        !m_spriteCanvasButton || !m_applySpriteEditsButton || !m_spriteNameLineEdit ||
        !m_spriteDetectionAreaComboBox || !m_clearSpriteDetectionAreaButton ||
        !m_spriteMinXSpinBox || !m_spriteMinYSpinBox ||
        !m_spriteMaxXSpinBox || !m_spriteMaxYSpinBox ||
        !m_spriteDetectionAreaXSpinBox || !m_spriteDetectionAreaYSpinBox ||
        !m_spriteDetectionAreaWidthSpinBox || !m_spriteDetectionAreaHeightSpinBox) {
        return;
    }

    const QVariant previousSpriteData = m_spriteComboBox->currentData();
    const QVariant previousSlotData = m_spriteSlotComboBox->currentData();
    const QVariant previousAreaData = m_spriteDetectionAreaComboBox->currentData();
    {
        QSignalBlocker blocker(m_spriteComboBox);
        m_spriteComboBox->clear();
        if (m_document) {
            for (std::size_t spriteIndex = 0; spriteIndex < m_document->spriteCount();
                 ++spriteIndex) {
                const document::SpriteRecord& sprite = m_document->sprite(spriteIndex);
                m_spriteComboBox->addItem(
                    QStringLiteral("%1%2")
                        .arg(spriteDisplayName(*m_document, spriteIndex))
                        .arg(sprite.hasHd ? QStringLiteral(" (HD)") : QString()),
                    static_cast<int>(spriteIndex));
            }
        }

        if (m_spriteComboBox->count() == 0) {
            m_spriteComboBox->addItem(QStringLiteral("No sprites"), -1);
            m_spriteComboBox->setCurrentIndex(0);
        } else if (previousSpriteData.isValid()) {
            const int previousIndex = m_spriteComboBox->findData(previousSpriteData);
            m_spriteComboBox->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        } else {
            m_spriteComboBox->setCurrentIndex(0);
        }
    }
    m_spriteComboBox->setEnabled(m_document && m_document->spriteCount() > 0);

    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    {
        QSignalBlocker blocker(m_spriteSlotComboBox);
        m_spriteSlotComboBox->clear();
        if (m_document && frameIndex) {
            const std::vector<document::FrameSpriteReference>& references =
                m_document->frameSpriteReferences(*frameIndex);
            for (std::size_t slot = 0; slot < references.size(); ++slot) {
                const document::FrameSpriteReference& reference = references[slot];
                const QString assignedSprite =
                    reference.spriteId == 255
                        ? QString()
                        : static_cast<std::size_t>(reference.spriteId) >=
                                m_document->spriteCount()
                            ? QStringLiteral("Sprite %1")
                                  .arg(static_cast<int>(reference.spriteId))
                        : spriteDisplayName(*m_document, reference.spriteId);
                m_spriteSlotComboBox->addItem(
                    reference.spriteId == 255
                        ? QStringLiteral("Slot %1: Empty").arg(static_cast<int>(slot))
                        : QStringLiteral("Slot %1: %2")
                              .arg(static_cast<int>(slot))
                              .arg(assignedSprite),
                    static_cast<int>(slot));
            }
        }
        if (m_spriteSlotComboBox->count() == 0) {
            m_spriteSlotComboBox->addItem(QStringLiteral("No slots"), -1);
            m_spriteSlotComboBox->setCurrentIndex(0);
        } else if (previousSlotData.isValid()) {
            const int previousIndex = m_spriteSlotComboBox->findData(previousSlotData);
            m_spriteSlotComboBox->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        } else {
            m_spriteSlotComboBox->setCurrentIndex(0);
        }
    }
    m_spriteSlotComboBox->setEnabled(m_document && frameIndex.has_value());

    if (!m_document || !frameIndex) {
        m_frameSpritesLabel->setText(QStringLiteral("No frame sprites"));
    } else {
        const std::vector<document::FrameSpriteReference>& references =
            m_document->frameSpriteReferences(*frameIndex);
        QStringList assignedSprites;
        for (std::size_t slot = 0; slot < references.size(); ++slot) {
            const document::FrameSpriteReference& reference = references[slot];
            if (reference.spriteId == 255) {
                continue;
            }
            const QString assignedSprite =
                reference.spriteId >= m_document->spriteCount()
                    ? QStringLiteral("sprite %1").arg(static_cast<int>(reference.spriteId))
                    : spriteDisplayName(*m_document, reference.spriteId);
            assignedSprites << QStringLiteral("%1: %2 [%3,%4-%5,%6]")
                                   .arg(static_cast<int>(slot))
                                   .arg(assignedSprite)
                                   .arg(reference.minX)
                                   .arg(reference.minY)
                                   .arg(reference.maxX)
                                   .arg(reference.maxY);
        }
        m_frameSpritesLabel->setText(
            assignedSprites.isEmpty()
                ? QStringLiteral("Current frame uses no sprites")
                : assignedSprites.join(QStringLiteral("\n")));
    }

    const int spriteIndex = m_spriteComboBox->currentData().toInt();
    const int slotIndex = m_spriteSlotComboBox->currentData().toInt();
    m_assignSpriteButton->setEnabled(m_document && frameIndex && spriteIndex >= 0 &&
                                     slotIndex >= 0 &&
                                     static_cast<std::size_t>(spriteIndex) <
                                         m_document->spriteCount());
    m_captureSpriteButton->setEnabled(m_assignSpriteButton->isEnabled());
    m_spriteCaptureSelectionButton->setEnabled(m_captureSpriteButton->isEnabled());
    m_clearSpriteCaptureSelectionButton->setEnabled(!spriteCaptureSelectionPixels().empty());
    bool slotHasSprite = false;
    std::optional<document::FrameSpriteReference> selectedReference;
    if (m_document && frameIndex && slotIndex >= 0) {
        const std::vector<document::FrameSpriteReference>& references =
            m_document->frameSpriteReferences(*frameIndex);
        if (static_cast<std::size_t>(slotIndex) < references.size()) {
            selectedReference = references[static_cast<std::size_t>(slotIndex)];
            slotHasSprite = selectedReference->spriteId != 255;
        }
    }
    m_removeSpriteButton->setEnabled(slotHasSprite);
    m_applySpriteEditsButton->setEnabled(m_spriteDependentViewsDirty);
    const bool hasSlot = selectedReference.has_value();
    m_spriteBoundsOverlayButton->setEnabled(hasSlot);
    if (m_preview) {
        m_preview->setSelectedSpriteReference(hasSlot ? selectedReference : std::nullopt);
    }
    {
        QSignalBlocker minXBlocker(m_spriteMinXSpinBox);
        QSignalBlocker minYBlocker(m_spriteMinYSpinBox);
        QSignalBlocker maxXBlocker(m_spriteMaxXSpinBox);
        QSignalBlocker maxYBlocker(m_spriteMaxYSpinBox);
        m_spriteMinXSpinBox->setEnabled(hasSlot);
        m_spriteMinYSpinBox->setEnabled(hasSlot);
        m_spriteMaxXSpinBox->setEnabled(hasSlot);
        m_spriteMaxYSpinBox->setEnabled(hasSlot);
        if (hasSlot) {
            m_spriteMinXSpinBox->setValue(static_cast<int>(selectedReference->minX));
            m_spriteMinYSpinBox->setValue(static_cast<int>(selectedReference->minY));
            m_spriteMaxXSpinBox->setValue(static_cast<int>(selectedReference->maxX));
            m_spriteMaxYSpinBox->setValue(static_cast<int>(selectedReference->maxY));
        } else {
            m_spriteMinXSpinBox->setValue(0);
            m_spriteMinYSpinBox->setValue(0);
            m_spriteMaxXSpinBox->setValue(render::kSdFrameSize.width - 1);
            m_spriteMaxYSpinBox->setValue(render::kSdFrameSize.height - 1);
        }
    }

    if (!m_document || spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        m_spriteDetectionAreasButton->setEnabled(false);
        m_spriteCanvasButton->setEnabled(false);
        m_captureSpriteButton->setEnabled(false);
        m_spriteCaptureSelectionButton->setEnabled(false);
        m_clearSpriteCaptureSelectionButton->setEnabled(false);
        m_spriteNameLineEdit->setEnabled(false);
        m_spriteNameLineEdit->clear();
        m_applySpriteEditsButton->setEnabled(false);
        if (isSpriteCanvasActive()) {
            m_createHdFrameButton->setEnabled(false);
            m_removeHdFrameButton->setEnabled(false);
        }
        if (m_canvasStack && m_preview) {
            m_canvasStack->setCurrentWidget(m_preview);
        }
        m_spriteDetectionAreaComboBox->setEnabled(false);
        m_clearSpriteDetectionAreaButton->setEnabled(false);
        m_spriteDetectionAreaXSpinBox->setEnabled(false);
        m_spriteDetectionAreaYSpinBox->setEnabled(false);
        m_spriteDetectionAreaWidthSpinBox->setEnabled(false);
        m_spriteDetectionAreaHeightSpinBox->setEnabled(false);
        if (m_selectedSpriteLabel) {
            m_selectedSpriteLabel->setText(QStringLiteral("Selected: None"));
        }
        m_spriteOriginalPreviewLabel->clear();
        m_spriteOriginalPreviewLabel->setText(QStringLiteral("Original"));
        m_spriteColorizedPreviewLabel->clear();
        m_spriteColorizedPreviewLabel->setText(QStringLiteral("Colorized"));
        if (m_spriteEditor) {
            m_spriteEditor->setSpriteIndex(std::nullopt);
        }
        return;
    }

    const document::SpriteRecord& sprite =
        m_document->sprite(static_cast<std::size_t>(spriteIndex));
    if (m_selectedSpriteLabel) {
        const QString label = sprite.name.empty()
                                  ? QString::number(static_cast<int>(spriteIndex))
                                  : QStringLiteral("%1 - %2")
                                        .arg(static_cast<int>(spriteIndex))
                                        .arg(QString::fromStdString(sprite.name));
        m_selectedSpriteLabel->setText(QStringLiteral("Selected: %1%2")
                                           .arg(label)
                                           .arg(sprite.hasHd ? QStringLiteral(" (HD)")
                                                             : QString()));
    }
    {
        QSignalBlocker blocker(m_spriteNameLineEdit);
        m_spriteNameLineEdit->setText(QString::fromStdString(sprite.name));
        m_spriteNameLineEdit->setEnabled(true);
    }
    m_spriteCanvasButton->setEnabled(true);
    const bool spriteCanvasActive = isSpriteCanvasActive();
    if (isSpriteCanvasActive()) {
        m_createHdFrameButton->setText(QStringLiteral("Create HD Sprite"));
        m_createHdFrameButton->setToolTip(QStringLiteral("Create HD sprite from SD sprite"));
        m_createHdFrameButton->setEnabled(!sprite.hasHd);
        m_removeHdFrameButton->setText(QStringLiteral("Remove HD Sprite"));
        m_removeHdFrameButton->setToolTip(QStringLiteral("Remove HD sprite data"));
        m_removeHdFrameButton->setEnabled(sprite.hasHd);
    }
    {
        QSignalBlocker blocker(m_spriteDetectionAreaComboBox);
        const int previousIndex = m_spriteDetectionAreaComboBox->findData(previousAreaData);
        if (previousIndex >= 0) {
            m_spriteDetectionAreaComboBox->setCurrentIndex(previousIndex);
        } else {
            m_spriteDetectionAreaComboBox->setCurrentIndex(0);
        }
    }
    const int areaIndex = m_spriteDetectionAreaComboBox->currentData().toInt();
    const bool hasAreaSelection =
        areaIndex >= 0 &&
        static_cast<std::size_t>(areaIndex) < sprite.detectionAreas.size();
    std::optional<document::SpriteDetectionArea> selectedArea;
    if (hasAreaSelection) {
        selectedArea = sprite.detectionAreas[static_cast<std::size_t>(areaIndex)];
    }
    const bool selectedAreaValid =
        selectedArea &&
        selectedArea->x != 0xffff && selectedArea->y != 0xffff &&
        selectedArea->width != 0xffff && selectedArea->height != 0xffff &&
        selectedArea->width > 0 && selectedArea->height > 0;
    m_spriteDetectionAreaComboBox->setEnabled(true);
    m_clearSpriteDetectionAreaButton->setEnabled(selectedAreaValid);
    {
        QSignalBlocker xBlocker(m_spriteDetectionAreaXSpinBox);
        QSignalBlocker yBlocker(m_spriteDetectionAreaYSpinBox);
        QSignalBlocker widthBlocker(m_spriteDetectionAreaWidthSpinBox);
        QSignalBlocker heightBlocker(m_spriteDetectionAreaHeightSpinBox);
        m_spriteDetectionAreaXSpinBox->setEnabled(hasAreaSelection);
        m_spriteDetectionAreaYSpinBox->setEnabled(hasAreaSelection);
        m_spriteDetectionAreaWidthSpinBox->setEnabled(hasAreaSelection);
        m_spriteDetectionAreaHeightSpinBox->setEnabled(hasAreaSelection);
        if (selectedAreaValid) {
            m_spriteDetectionAreaXSpinBox->setValue(static_cast<int>(selectedArea->x));
            m_spriteDetectionAreaYSpinBox->setValue(static_cast<int>(selectedArea->y));
            m_spriteDetectionAreaWidthSpinBox->setValue(static_cast<int>(selectedArea->width));
            m_spriteDetectionAreaHeightSpinBox->setValue(static_cast<int>(selectedArea->height));
        } else {
            m_spriteDetectionAreaXSpinBox->setValue(0);
            m_spriteDetectionAreaYSpinBox->setValue(0);
            m_spriteDetectionAreaWidthSpinBox->setValue(1);
            m_spriteDetectionAreaHeightSpinBox->setValue(1);
        }
    }
    const bool hasDetectionArea =
        std::any_of(sprite.detectionAreas.begin(),
                    sprite.detectionAreas.end(),
                    [](const document::SpriteDetectionArea& area) {
                        return area.x != 0xffff && area.y != 0xffff &&
                               area.width != 0xffff && area.height != 0xffff &&
                               area.width > 0 && area.height > 0;
                    });
    m_spriteDetectionAreasButton->setEnabled(hasDetectionArea);
    if (m_spriteEditor) {
        m_spriteEditor->setSpriteIndex(static_cast<std::size_t>(spriteIndex));
        m_spriteEditor->setDetectionAreasVisible(
            m_spriteDetectionAreasButton->isChecked());
        m_spriteEditor->setSelectedDetectionAreaIndex(
            hasAreaSelection ? std::optional<std::size_t>(static_cast<std::size_t>(areaIndex))
                             : std::nullopt);
    }
    if (m_canvasStack && m_preview && m_spriteEditor) {
        m_canvasStack->setCurrentWidget(m_spriteCanvasButton->isChecked()
                                            ? static_cast<QWidget*>(m_spriteEditor)
                                            : static_cast<QWidget*>(m_preview));
    }
    const QSize previewSize = m_spriteOriginalPreviewLabel->size();
    m_spriteOriginalPreviewLabel->setPixmap(
        previewPixmap(renderSpriteOriginalPreviewImage(
                          sprite,
                          m_spriteDetectionAreasButton->isChecked() && hasDetectionArea),
                      previewSize));
    m_spriteColorizedPreviewLabel->setPixmap(
        previewPixmap(renderSpriteColorizedPreviewImage(sprite, m_activeResolution), previewSize));
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

MainWindow::PaintTarget MainWindow::activePaintTarget() const
{
    if (!m_paintTargetComboBox) {
        return PaintTarget::Frame;
    }
    return static_cast<PaintTarget>(m_paintTargetComboBox->currentData().toInt());
}

uint8_t MainWindow::activeDynamicSet() const
{
    return m_dynamicSetSpinBox ? static_cast<uint8_t>(m_dynamicSetSpinBox->value()) : uint8_t{0};
}

void MainWindow::setActiveDrawColor(uint16_t rgb565)
{
    m_activeDrawColor = rgb565;
    if (m_spriteEditor) {
        m_spriteEditor->setActiveDrawColor(rgb565);
    }
    for (QToolButton* button : m_colorButtons) {
        QSignalBlocker blocker(button);
        button->setChecked(button->property("rgb565").toInt() == static_cast<int>(rgb565));
    }
    if (m_activeColorButton) {
        m_activeColorButton->setToolTip(
            QStringLiteral("Active draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
        m_activeColorButton->setStyleSheet(colorButtonStyle(rgb565, true));
    }
}

void MainWindow::setDrawingTool(DrawingTool tool)
{
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
    if (!m_document || !frameIndex || !m_dynamicSetSpinBox) {
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
        m_dynamicSetSpinBox->setValue(static_cast<int>(dynamicSet));
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

        commitBackgroundImagePixels(hit.resolution,
                                    pixels,
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

void MainWindow::paintDynamicMaskPixel(const render::CanvasHit& hit, bool erase)
{
    if (!m_document || !isDynamicMaskModeActive()) {
        return;
    }
    commitDynamicMaskPixels(hit.resolution,
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

        commitDynamicMaskPixels(hit.resolution,
                                pixels,
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
    commitBackgroundMaskPixels(hit.resolution,
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

        commitBackgroundMaskPixels(hit.resolution,
                                   pixels,
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
        commitDynamicMaskPixels(start.resolution,
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
        commitBackgroundMaskPixels(start.resolution,
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
        commitBackgroundImagePixels(start.resolution,
                                    pixelsForStroke(start, end),
                                    statusText.replace(QStringLiteral("Painted"),
                                                       QStringLiteral("Painted background")));
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
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || pixels.empty()) {
        return;
    }
    const uint16_t drawColor = m_activeDrawColor;

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            if (resolution == render::Resolution::Hd && !m_document->hasHdFrame(frameIndex)) {
                continue;
            }
            PixelEditCommand command;
            command.frameIndex = frameIndex;
            command.resolution = resolution;
            std::set<std::pair<int, int>> seen;
            for (const render::Point& pixel : pixels) {
                if (!seen.insert({pixel.x, pixel.y}).second) {
                    continue;
                }
                const uint16_t oldColor =
                    m_document->colorizedPixel(frameIndex, resolution, pixel.x, pixel.y);
                if (oldColor == drawColor) {
                    continue;
                }
                command.changes.push_back(PixelChange{
                    pixel.x,
                    pixel.y,
                    oldColor,
                    drawColor,
                });
            }
            if (command.changes.empty()) {
                continue;
            }
            applyPixelEdit(command, true);
            EditHistory& history = m_editHistories[EditHistoryKey{frameIndex, resolution}];
            history.undoStack.push_back(command);
            history.redoStack.clear();
            changed = true;
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
            MaskEditCommand command;
            command.maskIndex = maskIndex;
            std::set<std::pair<int, int>> seen;
            for (const render::Point& pixel : pixels) {
                if (!seen.insert({pixel.x, pixel.y}).second) {
                    continue;
                }
                const uint8_t oldValue =
                    m_document->comparisonMaskPixel(maskIndex, pixel.x, pixel.y);
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
                continue;
            }
            applyMaskEdit(command, true);
            MaskEditHistory& history = m_maskEditHistories[maskIndex];
            history.undoStack.push_back(command);
            history.redoStack.clear();
            changed = true;
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

void MainWindow::commitDynamicMaskPixels(render::Resolution resolution,
                                         const std::vector<render::Point>& pixels,
                                         uint8_t value,
                                         const QString& statusText)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || pixels.empty()) {
        return;
    }

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            if (!m_document->dynamicData(frameIndex)) {
                continue;
            }
            DynamicMaskEditCommand command;
            command.frameIndex = frameIndex;
            command.resolution = resolution;
            std::set<std::pair<int, int>> seen;
            for (const render::Point& pixel : pixels) {
                if (!seen.insert({pixel.x, pixel.y}).second) {
                    continue;
                }
                const uint8_t oldValue =
                    m_document->dynamicMaskPixel(frameIndex, resolution, pixel.x, pixel.y);
                if (oldValue == value) {
                    continue;
                }
                command.changes.push_back(DynamicMaskPixelChange{
                    pixel.x,
                    pixel.y,
                    oldValue,
                    value,
                });
            }
            if (command.changes.empty()) {
                continue;
            }
            applyDynamicMaskEdit(command, true);
            DynamicMaskEditHistory& history =
                m_dynamicMaskEditHistories[EditHistoryKey{frameIndex, resolution}];
            history.undoStack.push_back(command);
            history.redoStack.clear();
            changed = true;
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

void MainWindow::commitBackgroundMaskPixels(render::Resolution resolution,
                                            const std::vector<render::Point>& pixels,
                                            uint8_t value,
                                            const QString& statusText)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    if (!m_document || frameIndices.empty() || pixels.empty()) {
        return;
    }

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            const std::optional<document::FrameBackgroundData>& backgroundData =
                m_document->backgroundData(frameIndex);
            if (!backgroundData || backgroundData->backgroundId >= m_document->backgroundCount()) {
                continue;
            }
            BackgroundMaskEditCommand command;
            command.frameIndex = frameIndex;
            command.resolution = resolution;
            std::set<std::pair<int, int>> seen;
            for (const render::Point& pixel : pixels) {
                if (!seen.insert({pixel.x, pixel.y}).second) {
                    continue;
                }
                const uint8_t oldValue =
                    m_document->backgroundMaskPixel(frameIndex, resolution, pixel.x, pixel.y);
                if (oldValue == value) {
                    continue;
                }
                command.changes.push_back(BackgroundMaskPixelChange{
                    pixel.x,
                    pixel.y,
                    oldValue,
                    value,
                });
            }
            if (command.changes.empty()) {
                continue;
            }
            applyBackgroundMaskEdit(command, true);
            BackgroundMaskEditHistory& history =
                m_backgroundMaskEditHistories[EditHistoryKey{frameIndex, resolution}];
            history.undoStack.push_back(command);
            history.redoStack.clear();
            changed = true;
        }
        if (!changed) {
            statusBar()->showMessage(QStringLiteral("No selected frame has an assigned background"),
                                     2500);
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

    BackgroundImageEditCommand command;
    command.backgroundIndex = *backgroundIndex;
    command.resolution = resolution;
    std::set<std::pair<int, int>> seen;

    try {
        for (const render::Point& pixel : pixels) {
            if (!seen.insert({pixel.x, pixel.y}).second) {
                continue;
            }
            const uint16_t oldColor =
                m_document->backgroundPixel(*backgroundIndex, resolution, pixel.x, pixel.y);
            if (oldColor == drawColor) {
                continue;
            }
            command.changes.push_back(PixelChange{
                pixel.x,
                pixel.y,
                oldColor,
                drawColor,
            });
        }
        if (command.changes.empty()) {
            return;
        }
        applyBackgroundImageEdit(command, true);
        BackgroundImageEditHistory& history =
            m_backgroundImageEditHistories[BackgroundEditHistoryKey{*backgroundIndex, resolution}];
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

void MainWindow::applyDynamicMaskEdit(const DynamicMaskEditCommand& command, bool useNewValue)
{
    if (!m_document) {
        return;
    }
    for (const DynamicMaskPixelChange& change : command.changes) {
        m_document->setDynamicMaskPixel(command.frameIndex,
                                        command.resolution,
                                        change.x,
                                        change.y,
                                        useNewValue ? change.newValue : change.oldValue);
    }
    m_preview->update();
    updateProjectHeaderStatus();
}

void MainWindow::applyDynamicColorEdit(const DynamicColorEditCommand& command, bool useNewColor)
{
    if (!m_document) {
        return;
    }
    m_document->setDynamicColor(command.frameIndex,
                                command.resolution,
                                command.setIndex,
                                command.originalColorIndex,
                                useNewColor ? command.newColor : command.oldColor);
    updateDynamicColorControls();
    m_preview->update();
    updateFrameThumbnail(command.frameIndex);
    updateProjectHeaderStatus();
}

void MainWindow::applyDynamicShadowEdit(const DynamicShadowEditCommand& command, bool useNewValue)
{
    if (!m_document) {
        return;
    }
    for (const DynamicShadowEditChange& change : command.changes) {
        if (command.editDirection) {
            m_document->setDynamicShadowDirection(change.frameIndex,
                                                  command.resolution,
                                                  command.setIndex,
                                                  useNewValue ? change.newDirection
                                                              : change.oldDirection);
        }
        if (command.editColor) {
            m_document->setDynamicShadowColor(change.frameIndex,
                                             command.resolution,
                                             command.setIndex,
                                             useNewValue ? change.newColor : change.oldColor);
        }
        updateFrameThumbnail(change.frameIndex);
    }
    updateDynamicShadowControls();
    m_preview->update();
    updateProjectHeaderStatus();
}

void MainWindow::applyBackgroundMaskEdit(const BackgroundMaskEditCommand& command, bool useNewValue)
{
    if (!m_document) {
        return;
    }
    for (const BackgroundMaskPixelChange& change : command.changes) {
        m_document->setBackgroundMaskPixel(command.frameIndex,
                                           command.resolution,
                                           change.x,
                                           change.y,
                                           useNewValue ? change.newValue : change.oldValue);
    }
    m_preview->update();
    updateProjectHeaderStatus();
}

void MainWindow::applyBackgroundImageEdit(const BackgroundImageEditCommand& command,
                                          bool useNewColor)
{
    if (!m_document) {
        return;
    }
    for (const PixelChange& change : command.changes) {
        m_document->setBackgroundPixel(command.backgroundIndex,
                                       command.resolution,
                                       change.x,
                                       change.y,
                                       useNewColor ? change.newColor : change.oldColor);
    }
    m_preview->update();
    updateBackgroundControls();
    if (m_document) {
        for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
            const std::optional<document::FrameBackgroundData>& backgroundData =
                m_document->backgroundData(frameIndex);
            if (backgroundData && backgroundData->backgroundId == command.backgroundIndex) {
                updateFrameThumbnail(frameIndex);
            }
        }
    }
    updateProjectHeaderStatus();
}

void MainWindow::applySpriteImageEdit(const SpriteImageEditCommand& command, bool useNewColor)
{
    if (!m_document) {
        return;
    }
    for (const PixelChange& change : command.changes) {
        m_document->setSpriteColorizedPixel(command.spriteIndex,
                                            command.resolution,
                                            change.x,
                                            change.y,
                                            useNewColor ? change.newColor : change.oldColor);
    }
    setSpriteDependentViewsDirty();
    if (m_spriteEditor) {
        m_spriteEditor->update();
    }
}

std::optional<MainWindow::EditHistoryKey> MainWindow::currentEditHistoryKey() const
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!frameIndex) {
        return std::nullopt;
    }
    return EditHistoryKey{*frameIndex, m_activeResolution};
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

MainWindow::DynamicMaskEditHistory* MainWindow::currentDynamicMaskEditHistory()
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_dynamicMaskEditHistories.find(*key);
    if (history == m_dynamicMaskEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::DynamicMaskEditHistory* MainWindow::currentDynamicMaskEditHistory() const
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_dynamicMaskEditHistories.find(*key);
    if (history == m_dynamicMaskEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

bool MainWindow::isDynamicMaskModeActive() const
{
    return m_dynamicMaskOverlayButton && m_dynamicMaskOverlayButton->isChecked();
}

MainWindow::DynamicColorEditHistory* MainWindow::currentDynamicColorEditHistory()
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_dynamicColorEditHistories.find(*key);
    if (history == m_dynamicColorEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::DynamicColorEditHistory* MainWindow::currentDynamicColorEditHistory() const
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_dynamicColorEditHistories.find(*key);
    if (history == m_dynamicColorEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

MainWindow::DynamicShadowEditHistory* MainWindow::currentDynamicShadowEditHistory()
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_dynamicShadowEditHistories.find(*key);
    if (history == m_dynamicShadowEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::DynamicShadowEditHistory* MainWindow::currentDynamicShadowEditHistory() const
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_dynamicShadowEditHistories.find(*key);
    if (history == m_dynamicShadowEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

MainWindow::BackgroundMaskEditHistory* MainWindow::currentBackgroundMaskEditHistory()
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_backgroundMaskEditHistories.find(*key);
    if (history == m_backgroundMaskEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::BackgroundMaskEditHistory* MainWindow::currentBackgroundMaskEditHistory() const
{
    const std::optional<EditHistoryKey> key = currentEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_backgroundMaskEditHistories.find(*key);
    if (history == m_backgroundMaskEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

bool MainWindow::isBackgroundMaskModeActive() const
{
    return m_backgroundMaskOverlayButton && m_backgroundMaskOverlayButton->isChecked();
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

std::optional<MainWindow::BackgroundEditHistoryKey>
MainWindow::currentBackgroundEditHistoryKey() const
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        return std::nullopt;
    }
    return BackgroundEditHistoryKey{*backgroundIndex, m_activeResolution};
}

MainWindow::BackgroundImageEditHistory* MainWindow::currentBackgroundImageEditHistory()
{
    const std::optional<BackgroundEditHistoryKey> key = currentBackgroundEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_backgroundImageEditHistories.find(*key);
    if (history == m_backgroundImageEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::BackgroundImageEditHistory*
MainWindow::currentBackgroundImageEditHistory() const
{
    const std::optional<BackgroundEditHistoryKey> key = currentBackgroundEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_backgroundImageEditHistories.find(*key);
    if (history == m_backgroundImageEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

bool MainWindow::isBackgroundImageTargetActive() const
{
    return activePaintTarget() == PaintTarget::Background && selectedBackgroundIndex().has_value();
}

std::optional<std::size_t> MainWindow::currentSpriteIndex() const
{
    if (!m_document || !m_spriteComboBox) {
        return std::nullopt;
    }
    const int spriteIndex = m_spriteComboBox->currentData().toInt();
    if (spriteIndex < 0 ||
        static_cast<std::size_t>(spriteIndex) >= m_document->spriteCount()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(spriteIndex);
}

std::optional<MainWindow::SpriteEditHistoryKey>
MainWindow::currentSpriteEditHistoryKey() const
{
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!spriteIndex) {
        return std::nullopt;
    }
    render::Resolution resolution = m_activeResolution;
    if (m_document) {
        const document::SpriteRecord& sprite = m_document->sprite(*spriteIndex);
        if (resolution == render::Resolution::Hd && !sprite.hasHd) {
            resolution = render::Resolution::Sd;
        }
    }
    return SpriteEditHistoryKey{*spriteIndex, resolution};
}

MainWindow::SpriteImageEditHistory* MainWindow::currentSpriteImageEditHistory()
{
    const std::optional<SpriteEditHistoryKey> key = currentSpriteEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_spriteImageEditHistories.find(*key);
    if (history == m_spriteImageEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

const MainWindow::SpriteImageEditHistory*
MainWindow::currentSpriteImageEditHistory() const
{
    const std::optional<SpriteEditHistoryKey> key = currentSpriteEditHistoryKey();
    if (!key) {
        return nullptr;
    }
    auto history = m_spriteImageEditHistories.find(*key);
    if (history == m_spriteImageEditHistories.end()) {
        return nullptr;
    }
    return &history->second;
}

bool MainWindow::isSpriteCanvasActive() const
{
    return m_spriteCanvasButton && m_spriteCanvasButton->isChecked() &&
           currentSpriteIndex().has_value();
}

void MainWindow::undo()
{
    if (isSpriteCanvasActive()) {
        SpriteImageEditHistory* history = currentSpriteImageEditHistory();
        if (!history || history->undoStack.empty()) {
            return;
        }
        const SpriteImageEditCommand command = history->undoStack.back();
        history->undoStack.pop_back();
        applySpriteImageEdit(command, false);
        history->redoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo sprite edit"), 1500);
        return;
    }
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
    if (isDynamicMaskModeActive()) {
        DynamicMaskEditHistory* history = currentDynamicMaskEditHistory();
        if (history && !history->undoStack.empty()) {
            const DynamicMaskEditCommand command = history->undoStack.back();
            history->undoStack.pop_back();
            applyDynamicMaskEdit(command, false);
            history->redoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Undo dynamic mask edit"), 1500);
            return;
        }
        DynamicColorEditHistory* colorHistory = currentDynamicColorEditHistory();
        if (colorHistory && !colorHistory->undoStack.empty()) {
            const DynamicColorEditCommand command = colorHistory->undoStack.back();
            colorHistory->undoStack.pop_back();
            applyDynamicColorEdit(command, false);
            colorHistory->redoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Undo dynamic color edit"), 1500);
            return;
        }
        DynamicShadowEditHistory* shadowHistory = currentDynamicShadowEditHistory();
        if (shadowHistory && !shadowHistory->undoStack.empty()) {
            const DynamicShadowEditCommand command = shadowHistory->undoStack.back();
            shadowHistory->undoStack.pop_back();
            applyDynamicShadowEdit(command, false);
            shadowHistory->redoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Undo dynamic shadow edit"), 1500);
            return;
        }
        return;
    }
    if (isBackgroundMaskModeActive()) {
        BackgroundMaskEditHistory* history = currentBackgroundMaskEditHistory();
        if (history && !history->undoStack.empty()) {
            const BackgroundMaskEditCommand command = history->undoStack.back();
            history->undoStack.pop_back();
            applyBackgroundMaskEdit(command, false);
            history->redoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Undo background mask edit"), 1500);
            return;
        }
        return;
    }
    if (isBackgroundImageTargetActive()) {
        BackgroundImageEditHistory* history = currentBackgroundImageEditHistory();
        if (!history || history->undoStack.empty()) {
            return;
        }
        const BackgroundImageEditCommand command = history->undoStack.back();
        history->undoStack.pop_back();
        applyBackgroundImageEdit(command, false);
        history->redoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo background edit"), 1500);
        return;
    }

    EditHistory* history = currentEditHistory();
    if (history && !history->undoStack.empty()) {
        const PixelEditCommand command = history->undoStack.back();
        history->undoStack.pop_back();
        applyPixelEdit(command, false);
        history->redoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo"), 1500);
        return;
    }
    DynamicColorEditHistory* colorHistory = currentDynamicColorEditHistory();
    if (colorHistory && !colorHistory->undoStack.empty()) {
        const DynamicColorEditCommand command = colorHistory->undoStack.back();
        colorHistory->undoStack.pop_back();
        applyDynamicColorEdit(command, false);
        colorHistory->redoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo dynamic color edit"), 1500);
        return;
    }
    DynamicShadowEditHistory* shadowHistory = currentDynamicShadowEditHistory();
    if (shadowHistory && !shadowHistory->undoStack.empty()) {
        const DynamicShadowEditCommand command = shadowHistory->undoStack.back();
        shadowHistory->undoStack.pop_back();
        applyDynamicShadowEdit(command, false);
        shadowHistory->redoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Undo dynamic shadow edit"), 1500);
    }
}

void MainWindow::redo()
{
    if (isSpriteCanvasActive()) {
        SpriteImageEditHistory* history = currentSpriteImageEditHistory();
        if (!history || history->redoStack.empty()) {
            return;
        }
        const SpriteImageEditCommand command = history->redoStack.back();
        history->redoStack.pop_back();
        applySpriteImageEdit(command, true);
        history->undoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo sprite edit"), 1500);
        return;
    }
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
    if (isDynamicMaskModeActive()) {
        DynamicMaskEditHistory* history = currentDynamicMaskEditHistory();
        if (history && !history->redoStack.empty()) {
            const DynamicMaskEditCommand command = history->redoStack.back();
            history->redoStack.pop_back();
            applyDynamicMaskEdit(command, true);
            history->undoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Redo dynamic mask edit"), 1500);
            return;
        }
        DynamicColorEditHistory* colorHistory = currentDynamicColorEditHistory();
        if (colorHistory && !colorHistory->redoStack.empty()) {
            const DynamicColorEditCommand command = colorHistory->redoStack.back();
            colorHistory->redoStack.pop_back();
            applyDynamicColorEdit(command, true);
            colorHistory->undoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Redo dynamic color edit"), 1500);
            return;
        }
        DynamicShadowEditHistory* shadowHistory = currentDynamicShadowEditHistory();
        if (shadowHistory && !shadowHistory->redoStack.empty()) {
            const DynamicShadowEditCommand command = shadowHistory->redoStack.back();
            shadowHistory->redoStack.pop_back();
            applyDynamicShadowEdit(command, true);
            shadowHistory->undoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Redo dynamic shadow edit"), 1500);
            return;
        }
        return;
    }
    if (isBackgroundMaskModeActive()) {
        BackgroundMaskEditHistory* history = currentBackgroundMaskEditHistory();
        if (history && !history->redoStack.empty()) {
            const BackgroundMaskEditCommand command = history->redoStack.back();
            history->redoStack.pop_back();
            applyBackgroundMaskEdit(command, true);
            history->undoStack.push_back(command);
            updateUndoRedoActions();
            statusBar()->showMessage(QStringLiteral("Redo background mask edit"), 1500);
            return;
        }
        return;
    }
    if (isBackgroundImageTargetActive()) {
        BackgroundImageEditHistory* history = currentBackgroundImageEditHistory();
        if (!history || history->redoStack.empty()) {
            return;
        }
        const BackgroundImageEditCommand command = history->redoStack.back();
        history->redoStack.pop_back();
        applyBackgroundImageEdit(command, true);
        history->undoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo background edit"), 1500);
        return;
    }

    EditHistory* history = currentEditHistory();
    if (history && !history->redoStack.empty()) {
        const PixelEditCommand command = history->redoStack.back();
        history->redoStack.pop_back();
        applyPixelEdit(command, true);
        history->undoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo"), 1500);
        return;
    }
    DynamicColorEditHistory* colorHistory = currentDynamicColorEditHistory();
    if (colorHistory && !colorHistory->redoStack.empty()) {
        const DynamicColorEditCommand command = colorHistory->redoStack.back();
        colorHistory->redoStack.pop_back();
        applyDynamicColorEdit(command, true);
        colorHistory->undoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo dynamic color edit"), 1500);
        return;
    }
    DynamicShadowEditHistory* shadowHistory = currentDynamicShadowEditHistory();
    if (shadowHistory && !shadowHistory->redoStack.empty()) {
        const DynamicShadowEditCommand command = shadowHistory->redoStack.back();
        shadowHistory->redoStack.pop_back();
        applyDynamicShadowEdit(command, true);
        shadowHistory->undoStack.push_back(command);
        updateUndoRedoActions();
        statusBar()->showMessage(QStringLiteral("Redo dynamic shadow edit"), 1500);
    }
}

void MainWindow::updateUndoRedoActions()
{
    if (isSpriteCanvasActive()) {
        const SpriteImageEditHistory* history = currentSpriteImageEditHistory();
        if (m_undoAction) {
            m_undoAction->setEnabled(history && !history->undoStack.empty());
        }
        if (m_redoAction) {
            m_redoAction->setEnabled(history && !history->redoStack.empty());
        }
        return;
    }
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
    if (isDynamicMaskModeActive()) {
        const DynamicMaskEditHistory* history = currentDynamicMaskEditHistory();
        const DynamicColorEditHistory* colorHistory = currentDynamicColorEditHistory();
        const DynamicShadowEditHistory* shadowHistory = currentDynamicShadowEditHistory();
        if (m_undoAction) {
            m_undoAction->setEnabled((history && !history->undoStack.empty()) ||
                                     (colorHistory && !colorHistory->undoStack.empty()) ||
                                     (shadowHistory && !shadowHistory->undoStack.empty()));
        }
        if (m_redoAction) {
            m_redoAction->setEnabled((history && !history->redoStack.empty()) ||
                                     (colorHistory && !colorHistory->redoStack.empty()) ||
                                     (shadowHistory && !shadowHistory->redoStack.empty()));
        }
        return;
    }
    if (isBackgroundMaskModeActive()) {
        const BackgroundMaskEditHistory* history = currentBackgroundMaskEditHistory();
        if (m_undoAction) {
            m_undoAction->setEnabled(history && !history->undoStack.empty());
        }
        if (m_redoAction) {
            m_redoAction->setEnabled(history && !history->redoStack.empty());
        }
        return;
    }
    if (isBackgroundImageTargetActive()) {
        const BackgroundImageEditHistory* history = currentBackgroundImageEditHistory();
        if (m_undoAction) {
            m_undoAction->setEnabled(history && !history->undoStack.empty());
        }
        if (m_redoAction) {
            m_redoAction->setEnabled(history && !history->redoStack.empty());
        }
        return;
    }

    const EditHistory* history = currentEditHistory();
    const DynamicColorEditHistory* colorHistory = currentDynamicColorEditHistory();
    const DynamicShadowEditHistory* shadowHistory = currentDynamicShadowEditHistory();
    if (m_undoAction) {
        m_undoAction->setEnabled((history && !history->undoStack.empty()) ||
                                 (colorHistory && !colorHistory->undoStack.empty()) ||
                                 (shadowHistory && !shadowHistory->undoStack.empty()));
    }
    if (m_redoAction) {
        m_redoAction->setEnabled((history && !history->redoStack.empty()) ||
                                 (colorHistory && !colorHistory->redoStack.empty()) ||
                                 (shadowHistory && !shadowHistory->redoStack.empty()));
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
        const bool hasHd = m_document->hasHdFrame(frameIndex);
        item->setText(QStringLiteral("%1%2")
                          .arg(static_cast<int>(frameIndex))
                          .arg(hasHd ? QStringLiteral(" (HD)") : QString()));
        item->setIcon(makeFrameThumbnail(*m_document, frameIndex, QSize(160, 84)));
        return;
    }
    refreshFrameList(frameIndex);
}

DmdRenderOptions MainWindow::currentDmdRenderOptions() const
{
    DmdRenderOptions options;
    if (m_dmdPixelShapeComboBox) {
        options.pixelShape =
            static_cast<DmdPixelShape>(m_dmdPixelShapeComboBox->currentData().toInt());
    }
    if (m_dmdPixelSizeSpinBox) {
        options.pixelCoverage =
            static_cast<double>(m_dmdPixelSizeSpinBox->value()) / 100.0;
    }
    return options;
}

void MainWindow::updateDmdRenderOptions()
{
    if (m_dmdPixelSizeSpinBox) {
        const int value = m_dmdPixelSizeSpinBox->value();
        const int roundedValue = std::clamp(((value + 5) / 10) * 10, 50, 100);
        if (roundedValue != value) {
            QSignalBlocker blocker(m_dmdPixelSizeSpinBox);
            m_dmdPixelSizeSpinBox->setValue(roundedValue);
        }
    }
    if (m_preview) {
        m_preview->setDmdRenderOptions(currentDmdRenderOptions());
    }
    if (m_spriteEditor) {
        m_spriteEditor->setDmdRenderOptions(currentDmdRenderOptions());
    }
}

}  // namespace colorizing::app
