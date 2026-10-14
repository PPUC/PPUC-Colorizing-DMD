#include "MainWindow.h"

#include <algorithm>
#include <fstream>
#include <functional>

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QApplication>
#include <QActionGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
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
#include <QTime>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "EditColorKeys.h"
#include "EditMenuActions.h"
#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "GifExport.h"
#include "FrameThumbnail.h"
#include "LazyListIcons.h"
#include "ListSelection.h"
#include "PlaneLabels.h"
#include "SpriteEditorWidget.h"
#include "dialogs/ColorPickerDialog.h"
#include "dialogs/DumpImportDialog.h"
#include "dialogs/FrameTesterDialog.h"
#include "dialogs/ProjectTaskDialog.h"
#include "dialogs/SelectionDialogs.h"
#include "edit/FrameContentCommand.h"
#include "edit/ColorSetEdits.h"
#include "edit/Sections.h"
#include "edit/SpriteClipboard.h"
#include "edit/StateCommands.h"
#include "import/FrameDumpImport.h"
#include "panels/BackgroundPanel.h"
#include "panels/ColorRotationPanel.h"
#include "panels/ColorSetPanel.h"
#include "panels/DynamicPanel.h"
#include "panels/EditColorBar.h"
#include "panels/FramePanel.h"
#include "panels/MaskPanel.h"
#include "panels/PalettePanel.h"
#include "panels/RecordListPage.h"
#include "panels/SameFramesPanel.h"
#include "panels/SectionBar.h"
#include "panels/SpritePanel.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"
#include "platform/Utf8Path.h"
#include "runtime/CromcExport.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::app {

namespace {

constexpr int kMaxRecentProjects = 8;
constexpr char kRecentProjectsSettingsKey[] = "recentProjects";
constexpr int kExportDmdPixelScale = 8;
constexpr QSize kFrameThumbnailSize{160, 84};
constexpr char kRgb565MimeType[] = "application/x-colorizing-rgb565";
constexpr char kWindowTitle[] = "ColorizingDMD Qt Rewrite v2";

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

// A page of the inspector that asks for no width of its own: it takes the width the other
// pages give the inspector and scrolls if that is too little.
class NarrowPage final : public QWidget {
public:
    using QWidget::QWidget;

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(0, QWidget::sizeHint().height());
    }
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

QString editKindStatusSuffix(const edit::EditCommand& command, bool showsHdAndSd)
{
    switch (command.target().kind) {
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
            return showsHdAndSd && dynamic_cast<const edit::SpriteHdCommand*>(&command)
                       ? QStringLiteral(" sprite HD change")
                       : QStringLiteral(" sprite change");
        case edit::EditKind::ProjectStructure:
            break;
        case edit::EditKind::EditorColors:
            return QStringLiteral(" edit colors and color sets change");
        case edit::EditKind::Sections:
            return QStringLiteral(" section change");
    }
    return QString();
}

// "delete 3 frame(s)", "add sprite 4": what a structure command does when it is applied.
QString structureCommandText(const edit::StructureCommand& command)
{
    const int index = static_cast<int>(command.recordIndices().front());
    const QString verb = command.addsRecords() ? QStringLiteral("add") : QStringLiteral("delete");
    switch (command.recordKind()) {
        case edit::RecordKind::Frame:
            return QStringLiteral("%1 %2 frame(s)")
                .arg(verb)
                .arg(static_cast<int>(command.recordIndices().size()));
        case edit::RecordKind::Sprite:
            return QStringLiteral("%1 sprite %2").arg(verb).arg(index);
        case edit::RecordKind::Background:
            return QStringLiteral("%1 background %2").arg(verb).arg(index);
        case edit::RecordKind::ComparisonMask:
            return QStringLiteral("%1 comparison mask %2").arg(verb).arg(index);
    }
    return QString();
}

// "3, 5-7, 12": at most maxRanges runs of frame numbers, then "...".
QString frameRangesText(const std::vector<std::size_t>& frameIndices, int maxRanges = 8)
{
    QStringList ranges;
    for (std::size_t i = 0; i < frameIndices.size();) {
        std::size_t last = i;
        while (last + 1 < frameIndices.size() &&
               frameIndices[last + 1] == frameIndices[last] + 1) {
            ++last;
        }
        if (ranges.size() == maxRanges) {
            ranges << QStringLiteral("...");
            break;
        }
        ranges << (last == i ? QString::number(static_cast<qulonglong>(frameIndices[i]))
                             : QStringLiteral("%1-%2")
                                   .arg(static_cast<qulonglong>(frameIndices[i]))
                                   .arg(static_cast<qulonglong>(frameIndices[last])));
        i = last + 1;
    }
    return ranges.join(QStringLiteral(", "));
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
    , m_pasteMode(pasteModeOutputs())
    , m_editHelpers(editHelpersOutputs())
{
    setWindowTitle(QString::fromLatin1(kWindowTitle));
    resize(1000, 700);
    createActions();
    createCentralView();
    setStatusText(QStringLiteral("No project loaded"));
    // Every 10 minutes, as the Win32 editor (AUTOSAVE_TICKS).
    m_autosaver = std::make_unique<Autosaver>(
        this,
        std::chrono::minutes(10),
        [this]() { return autosaveJob(); },
        [this](const AutosaveFiles& files,
               uint64_t revision,
               const legacy::LegacySaveResult& result) {
            autosaveFinished(files, revision, result);
        });
}

void MainWindow::createActions()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

    QAction* newFromDumpAction =
        fileMenu->addAction(QStringLiteral("&New Project From Dump..."));
    newFromDumpAction->setToolTip(QStringLiteral("Create a project of the frames of a frame dump"));
    connect(newFromDumpAction, &QAction::triggered, this, [this]() {
        newProjectFromDump();
    });

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

    QAction* exportCromcAction = fileMenu->addAction(QStringLiteral("Export .cRO&Mc..."));
    exportCromcAction->setToolTip(
        QStringLiteral("Write the project's .cROMc as libserum generates it on a machine"));
    connect(exportCromcAction, &QAction::triggered, this, [this]() {
        exportCromc();
    });

    QAction* exportGifAction = fileMenu->addAction(QStringLiteral("Export Animated &GIF..."));
    exportGifAction->setToolTip(QStringLiteral(
        "Write the selected frames, with their colour rotations, as an animated GIF"));
    connect(exportGifAction, &QAction::triggered, this, [this]() { exportAnimatedGif(); });

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

    m_addFramesFromDumpAction = fileMenu->addAction(QStringLiteral("A&dd Frames From Dump..."));
    m_addFramesFromDumpAction->setToolTip(
        QStringLiteral("Add the frames of a frame dump that the project does not have yet"));
    m_addFramesFromDumpAction->setEnabled(false);
    connect(m_addFramesFromDumpAction, &QAction::triggered, this, [this]() {
        addFramesFromDump();
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

    editMenu->addSeparator();
    m_deleteFramesAction = editMenu->addAction(QStringLiteral("&Delete Frames..."));
    m_deleteFramesAction->setToolTip(QStringLiteral("Delete the selected frames"));
    m_deleteFramesAction->setEnabled(false);
    connect(m_deleteFramesAction, &QAction::triggered, this, [this]() {
        deleteSelectedFrames();
    });
    m_deleteShortFramesAction =
        editMenu->addAction(QStringLiteral("Delete Frames &Shorter Than..."));
    m_deleteShortFramesAction->setToolTip(
        QStringLiteral("Delete the selected frames that are shorter than a duration"));
    m_deleteShortFramesAction->setEnabled(false);
    connect(m_deleteShortFramesAction, &QAction::triggered, this, [this]() {
        deleteSelectedFramesShorterThan();
    });
    createSelectionActions(editMenu);

    QAction* frameTesterAction = viewMenu->addAction(QStringLiteral("Frame &Tester..."));
    frameTesterAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    frameTesterAction->setToolTip(QStringLiteral(
        "Feed the selected frames or a frame dump to libserum with the whole project loaded"));
    connect(frameTesterAction, &QAction::triggered, this, [this]() { openFrameTester(); });
    viewMenu->addSeparator();

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
    // The keys belong to the frame list alone, so that they keep their meaning everywhere
    // else. An action of the menu bar cannot have them: a native menu bar takes a key of
    // its entries whatever has the focus.
    m_deleteFramesListAction = new QAction(m_deleteFramesAction->text(), m_frameList);
    m_deleteFramesListAction->setShortcuts({QKeySequence(QKeySequence::Delete),
                                            QKeySequence(Qt::Key_Backspace)});
    m_deleteFramesListAction->setShortcutContext(Qt::WidgetShortcut);
    m_deleteFramesListAction->setEnabled(false);
    connect(m_deleteFramesListAction, &QAction::triggered, this, [this]() {
        deleteSelectedFrames();
    });
    m_frameList->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_frameList->addAction(m_deleteFramesListAction);
    m_frameList->addAction(m_deleteShortFramesAction);
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
    m_sameFramesPanel = new SameFramesPanel(central);
    m_sameFramesPanel->onFrameRequested = [this](std::size_t frameIndex) {
        goToFrame(frameIndex);
    };
    m_sameFramesPanel->onUpdateCountsRequested = [this]() { updateSameFrameCounts(); };
    m_sameFramesPanel->onDeleteSameAsCurrentRequested = [this]() {
        deleteSameFrames(SameFrameDeletion::SameAsCurrent);
    };
    m_sameFramesPanel->onDeleteSameAsSelectedRequested = [this]() {
        deleteSameFrames(SameFrameDeletion::SameAsSelected);
    };
    m_sameFramesPanel->onDeleteAllDuplicatesRequested = [this]() {
        deleteSameFrames(SameFrameDeletion::AllDuplicates);
    };
    m_sameFramesPanel->onSpotCommonToggled = [this](bool) { updateSpotCommonOverlay(); };
    m_sameFramesPanel->onCommonToMaskRequested = [this]() { writeCommonPixelsToMask(); };
    m_sameFramesPanel->onUsingMaskChanged = [this]() { showFramesUsingMask(); };
    m_sameFramesPanel->onSelectUsingMaskRequested = [this]() { selectFramesUsingMask(); };
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
        if (checked) {
            m_pasteMode.cancel();
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
    m_spritePanel->onSourceFrameRequested = [this]() {
        const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
        if (m_document && spriteIndex) {
            goToFrame(m_document->spriteSourceFrame(*spriteIndex));
        }
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
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, button, colorIndex](const QPoint& position) {
                    QMenu menu(button);
                    QAction* editAction = menu.addAction(QStringLiteral("Edit Color..."));
                    if (menu.exec(button->mapToGlobal(position)) == editAction) {
                        editFavoriteColor(colorIndex);
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
    m_editColorBar = new EditColorBar(central);
    m_editColorBar->setSlotKeyNames(editColorSlotKeyNames());
    m_editColorBar->onSlotClicked = [this](std::size_t slot) { selectEditColorSlot(slot); };
    m_editColorBar->onSlotEditRequested = [this](std::size_t slot) { editEditColor(slot); };
    m_editColorBar->onColorDropped = [this](std::size_t slot, uint16_t rgb565) {
        setEditColor(slot, rgb565);
    };
    m_editColorBar->onDrawColorModeChanged = [this](edit::DrawColorMode mode) {
        setDrawColorMode(mode);
    };
    m_editColorBar->onEditColorsToDynamicRequested = [this]() { copyEditColorsToDynamicSet(); };
    m_editColorBar->onDynamicToEditColorsRequested = [this]() { copyDynamicSetToEditColors(); };
    layout->addWidget(m_editColorBar);

    auto* contentLayout = new QHBoxLayout();
    m_leftBrowserTabs = new QTabWidget(central);
    m_leftBrowserTabs->setDocumentMode(true);
    m_leftBrowserTabs->setMinimumWidth(250);
    auto* frameListPanel = new QWidget(m_leftBrowserTabs);
    auto* frameListLayout = new QVBoxLayout(frameListPanel);
    frameListLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionBar = new SectionBar(frameListPanel);
    m_sectionBar->onSectionChosen = [this](std::size_t section) {
        if (m_document) {
            goToFrame(edit::sections(*m_document).at(section).firstFrame);
        }
    };
    m_sectionBar->onTriggerChosen = [this](std::size_t frameIndex) { goToFrame(frameIndex); };
    m_sectionBar->onNameRequested = [this]() { nameSectionAtCurrentFrame(); };
    m_sectionBar->onDeleteRequested = [this]() { deleteCurrentSection(); };
    m_sectionBar->onOrderRequested = [this]() { orderSections(); };
    frameListLayout->addWidget(m_sectionBar);
    frameListLayout->addWidget(m_frameFilterComboBox);
    frameListLayout->addWidget(m_frameList, 1);
    m_leftBrowserTabs->addTab(frameListPanel, QStringLiteral("Frames"));
    m_maskPage = new RecordListPage(m_maskList,
                                    QStringLiteral("Add an empty comparison mask"),
                                    QStringLiteral("Delete the selected comparison mask"),
                                    m_leftBrowserTabs);
    m_maskPage->onNewRequested = [this]() { addComparisonMask(); };
    m_maskPage->onDeleteRequested = [this]() { deleteSelectedComparisonMask(); };
    m_backgroundPage = new RecordListPage(
        m_backgroundList,
        QStringLiteral("Add a background with the picture of the current frame"),
        QStringLiteral("Delete the selected background"),
        m_leftBrowserTabs);
    m_backgroundPage->onNewRequested = [this]() { addBackgroundFromCurrentFrame(); };
    m_backgroundPage->onDeleteRequested = [this]() { deleteSelectedBackground(); };
    m_spritePage = new RecordListPage(m_spriteList,
                                      QStringLiteral("Add an empty sprite"),
                                      QStringLiteral("Delete the selected sprite"),
                                      m_leftBrowserTabs);
    m_spritePage->onNewRequested = [this]() { addSprite(); };
    m_spritePage->onDeleteRequested = [this]() { deleteSelectedSprite(); };
    m_leftBrowserTabs->addTab(m_maskPage, QStringLiteral("Masks"));
    m_leftBrowserTabs->addTab(m_backgroundPage, QStringLiteral("Backgrounds"));
    m_leftBrowserTabs->addTab(m_spritePage, QStringLiteral("Sprites"));
    auto* inspector = new QWidget(central);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    auto* inspectorTitle = new QLabel(QStringLiteral("Inspector"), inspector);
    inspectorTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    auto* inspectorTabs = new QTabWidget(inspector);
    inspectorTabs->setDocumentMode(true);

    inspectorTabs->addTab(m_framePanel, QStringLiteral("Frame"));

    // The mask of the frame and, below it, the comparison tools; they scroll together.
    auto* maskPage = new NarrowPage(inspectorTabs);
    auto* maskPageLayout = new QVBoxLayout(maskPage);
    maskPageLayout->setContentsMargins(0, 0, 0, 0);
    maskPageLayout->setSpacing(0);
    maskPageLayout->addWidget(m_maskPanel);
    maskPageLayout->addWidget(m_sameFramesPanel);
    maskPageLayout->addStretch(1);
    auto* maskScrollArea = new QScrollArea(inspectorTabs);
    maskScrollArea->setWidgetResizable(true);
    maskScrollArea->setFrameShape(QFrame::NoFrame);
    maskScrollArea->setWidget(maskPage);
    inspectorTabs->addTab(maskScrollArea, QStringLiteral("Mask"));
    connect(inspectorTabs, &QTabWidget::currentChanged, this, [this](int) {
        updateSameFrameControls();
    });

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

    m_colorSetPanel = new ColorSetPanel(central);
    m_colorSetPanel->onStoreRequested = [this](std::size_t setIndex) { storeColorSet(setIndex); };
    m_colorSetPanel->onLoadRequested = [this](std::size_t setIndex) { loadColorSet(setIndex); };
    m_colorSetPanel->onDeactivateRequested = [this](std::size_t setIndex) {
        deactivateColorSet(setIndex);
    };
    m_colorSetPanel->onNameEdited = [this](std::size_t setIndex, const QString& name) {
        renameColorSet(setIndex, name);
    };
    m_colorSetPanel->onScrolled = [this](std::size_t firstVisibleSet) {
        // A view setting of the cRP (preColSet), not a change of the project.
        if (m_document && firstVisibleSet < document::kMaxColorSets) {
            m_document->setFirstDisplayedColorSet(static_cast<uint8_t>(firstVisibleSet));
        }
    };
    inspectorTabs->addTab(m_colorSetPanel, QStringLiteral("Sets"));

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
        m_pasteMode.cancel();
        m_canvasEdit.cancelFreeGradient();
        m_canvasEdit.clearSelection(canvasEditContext());
        m_activeResolution = render::Resolution::Sd;
        updateResolutionMode();
    });
    connect(m_hdButton, &QToolButton::clicked, this, [this]() {
        m_pasteMode.cancel();
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
        colorizedCanvasClicked(hit, erase);
    });
    m_preview->setPressInterceptor([this](render::CanvasHit hit, Qt::KeyboardModifiers modifiers) {
        return interceptCanvasPress(hit, modifiers);
    });
    m_preview->setOriginalClickedHandler([this](render::CanvasHit hit, bool erase) {
        m_canvasEdit.originalPixelClicked(canvasEditContext(), hit, erase);
    });
    m_preview->setStrokePreviewHandler(
        [this](render::CanvasHit start, render::CanvasHit end, bool erase) {
            if (selectsWithPress()) {
                m_canvasEdit.previewSelectionStroke(canvasEditContext(), start, end, erase);
                return;
            }
            m_canvasEdit.strokePreviewed(canvasEditContext(), start, end, erase);
    });
    m_preview->setStrokeFinishedHandler(
        [this](render::CanvasHit start, render::CanvasHit end, bool erase) {
            if (selectsWithPress()) {
                m_canvasEdit.editSelectionWithStroke(canvasEditContext(), start, end, erase,
                                                     selectsNonBlackOnly());
                return;
            }
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
                const std::size_t sprite = static_cast<std::size_t>(spriteIndex);
                edit::SpriteState before = edit::spriteState(*m_document, sprite);
                m_document->setSpriteDetectionArea(sprite, areaIndex, area);
                pushEdit(edit::spriteStateCommand(*m_document, sprite, std::move(before)));
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
    m_preview->installEventFilter(this);
    updateDmdRenderOptions();

    // The browser pages follow the frame list, each with New and Delete before its list.
    QWidget* previous = m_frameList;
    for (const RecordListPage* page : {m_maskPage, m_backgroundPage, m_spritePage}) {
        for (QWidget* next : page->tabOrder()) {
            QWidget::setTabOrder(previous, next);
            previous = next;
        }
    }

    setCentralWidget(central);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmClosingProject(QStringLiteral("Quit"))) {
        event->ignore();
        return;
    }
    setPlaybackRunning(false);
    // A write still running is stopped; the autosave files it was replacing stay as they were.
    m_autosaver->cancelAndWait();
    event->accept();
}

void MainWindow::openProject()
{
    if (!confirmClosingProject(QStringLiteral("Open Project"))) {
        return;
    }
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
    if (!confirmClosingProject(QStringLiteral("Open Recent Project"))) {
        return;
    }
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

bool MainWindow::isUntitledProject() const
{
    return m_document && (m_currentCromPath.isEmpty() || m_currentCrpPath.isEmpty());
}

void MainWindow::updateWindowTitle()
{
    const QString application = QString::fromLatin1(kWindowTitle);
    if (!m_document) {
        setWindowTitle(application);
    } else if (isUntitledProject()) {
        setWindowTitle(QStringLiteral("%1 (not saved yet) \u2014 %2")
                           .arg(QString::fromStdString(m_document->romName()), application));
    } else {
        setWindowTitle(QStringLiteral("%1 \u2014 %2")
                           .arg(QFileInfo(m_currentCromPath).fileName(), application));
    }
}

bool MainWindow::saveProject()
{
    if (!m_document) {
        return false;
    }
    if (isUntitledProject()) {
        return saveProjectAs();
    }
    applyPendingSpriteDependentViews();

    // The files are written on a worker thread (runProjectTask) from a snapshot that shares
    // every block with the document (copy on write), so the save copies no pixels. Nothing
    // can change the document meanwhile: the UI handles no input until the save has ended.
    const legacy::LegacyProjectPaths paths{
        m_currentCromPath.toStdString(),
        m_currentCrpPath.toStdString(),
        siblingProjectFile(m_currentCromPath, QStringLiteral("cRZ")).toStdString(),
    };
    legacy::LegacySaveResult result;
    {
        const document::ProjectSnapshot snapshot = m_document->toSnapshot();
        legacy::LegacyIoProgress progress;
        const std::optional<std::string> failure =
            runProjectTask(this, QStringLiteral("Saving Project"), progress, [&]() {
                result = legacy::LegacyProjectFileWriter().save(paths, snapshot, &progress);
            });
        if (failure) {
            result = legacy::makeSaveFailure(*failure);
        }
    }

    if (result.cancelled) {
        statusBar()->showMessage(
            QStringLiteral("Save cancelled: the project files on disk are unchanged"), 4000);
        return false;
    }
    if (!result.ok) {
        QMessageBox::critical(this,
                              QStringLiteral("Save Project Failed"),
                              QString::fromStdString(result.error.message));
        statusBar()->showMessage(QStringLiteral("Save failed"), 4000);
        return false;
    }

    m_document->markClean();
    updateProjectHeaderStatus();
    statusBar()->showMessage(QStringLiteral("Project saved"), 3000);
    return true;
}

bool MainWindow::saveProjectAs()
{
    if (!m_document) {
        return false;
    }
    const bool untitled = isUntitledProject();
    // A new project is offered under its ROM name, which is the name the runtime looks for,
    // beside the dump it was made of.
    QString proposedPath;
    if (untitled) {
        const QDir directory(m_dumpDirectory.isEmpty() ? QDir::homePath() : m_dumpDirectory);
        proposedPath = directory.absoluteFilePath(QString::fromStdString(m_document->romName()) +
                                                  QStringLiteral(".cROM"));
    } else {
        const QFileInfo currentCromInfo(m_currentCromPath);
        proposedPath = currentCromInfo.dir().absoluteFilePath(
            currentCromInfo.completeBaseName() + QStringLiteral("_copy.cROM"));
    }
    QString targetCromPath = QFileDialog::getSaveFileName(
        this,
        untitled ? QStringLiteral("Save New Project As") : QStringLiteral("Save cROM As"),
        proposedPath,
        QStringLiteral("Serum colorization (*.cROM);;All files (*)"));
    if (targetCromPath.isEmpty()) {
        return false;
    }
    if (!targetCromPath.endsWith(QStringLiteral(".cROM"), Qt::CaseInsensitive)) {
        targetCromPath += QStringLiteral(".cROM");
    }

    const QFileInfo targetCromInfo(targetCromPath);
    const QString targetCrpPath =
        targetCromInfo.dir().absoluteFilePath(targetCromInfo.completeBaseName() +
                                              QStringLiteral(".cRP"));
    if (!untitled && targetCromPath == m_currentCromPath) {
        return saveProject();
    }

    const QString oldCromPath = m_currentCromPath;
    const QString oldCrpPath = m_currentCrpPath;
    m_currentCromPath = targetCromPath;
    m_currentCrpPath = targetCrpPath;
    if (saveProject()) {
        if (untitled) {
            // Its autosave goes beside the saved files from now on.
            const AutosaveFiles untitledFiles = untitledAutosaveFiles(m_document->romName());
            QFile::remove(untitledFiles.cromPath);
            QFile::remove(untitledFiles.crpPath);
        }
        rememberRecentProject(m_currentCromPath, m_currentCrpPath);
        updateWindowTitle();
        statusBar()->showMessage(untitled ? QStringLiteral("Project saved as %1")
                                                .arg(targetCromInfo.fileName())
                                          : QStringLiteral("Project saved as copy"),
                                 3000);
        return true;
    }

    m_currentCromPath = oldCromPath;
    m_currentCrpPath = oldCrpPath;
    return false;
}

UnsavedChangesAnswer MainWindow::askAboutUnsavedChanges(const QString& action)
{
    if (m_unsavedChangesAnswer) {
        return m_unsavedChangesAnswer(action);
    }
    QMessageBox question(this);
    question.setIcon(QMessageBox::Warning);
    question.setWindowTitle(action);
    question.setText(QStringLiteral("The current project has changes that are not saved."));
    question.setInformativeText(
        isUntitledProject()
            ? QStringLiteral("Save asks where to save the new project first. Discard Changes "
                             "loses them.")
            : QStringLiteral("Save writes them first. Discard Changes loses them."));
    QPushButton* saveButton = question.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    QPushButton* discardButton =
        question.addButton(QStringLiteral("Discard Changes"), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = question.addButton(QMessageBox::Cancel);
    question.setDefaultButton(saveButton);
    question.setEscapeButton(cancelButton);
    question.exec();
    if (question.clickedButton() == saveButton) {
        return UnsavedChangesAnswer::Save;
    }
    return question.clickedButton() == discardButton ? UnsavedChangesAnswer::Discard
                                                     : UnsavedChangesAnswer::Cancel;
}

bool MainWindow::confirmClosingProject(const QString& action)
{
    return mayCloseProject(
        m_document && m_document->isDirty(),
        [this, &action]() {
            const UnsavedChangesAnswer answer = askAboutUnsavedChanges(action);
            if (answer == UnsavedChangesAnswer::Discard) {
                forgetAutosave();
            }
            return answer;
        },
        [this]() { return saveProject(); });
}

void MainWindow::newProjectFromDump()
{
    if (!confirmClosingProject(QStringLiteral("New Project From Dump"))) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose Frame Dump For New Project"),
        m_dumpDirectory,
        QStringLiteral("Frame dumps (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    DumpImportDialog dialog(DumpImportDialog::Mode::NewProject, nullptr, this);
    dialog.setDumpPath(path);
    if (dialog.exec() != QDialog::Accepted || !dialog.result()) {
        return;
    }
    m_dumpDirectory = QFileInfo(dialog.dumpPath()).absolutePath();
    importing::DumpImportResult dump = dialog.takeResult();
    const QString summary = dumpCountsText(dump, false);
    try {
        setPlaybackRunning(false);
        m_document = document::ProjectDocument::fromSnapshot(
            importing::makeProjectSnapshot(dialog.romName().toStdString(), std::move(dump)));
        m_document->markUnsaved();
        m_currentCromPath.clear();
        m_currentCrpPath.clear();
        m_undoHistories.clear();
        m_pasteMode.clearClipboard();
        updateUndoRedoActions();
        if (m_saveAction) {
            m_saveAction->setEnabled(true);
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this,
                              QStringLiteral("New Project Failed"),
                              QString::fromUtf8(ex.what()));
        return;
    }
    refreshProjectSummary();
    statusBar()->showMessage(
        QStringLiteral("New project %1, not saved yet: %2")
            .arg(QString::fromStdString(m_document->romName()), summary),
        8000);
}

void MainWindow::addFramesFromDump()
{
    if (!m_document) {
        statusBar()->showMessage(QStringLiteral("No project loaded"), 2500);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Choose Frame Dump To Add"),
        m_dumpDirectory,
        QStringLiteral("Frame dumps (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    // The dialog's worker reads the document: nothing may change it meanwhile.
    setPlaybackRunning(false);
    applyPendingSpriteDependentViews();
    DumpImportDialog dialog(DumpImportDialog::Mode::AddFrames, &*m_document, this);
    dialog.setDumpPath(path);
    if (dialog.exec() != QDialog::Accepted || !dialog.result()) {
        return;
    }
    m_dumpDirectory = QFileInfo(dialog.dumpPath()).absolutePath();
    importing::DumpImportResult dump = dialog.takeResult();
    const QString summary = dumpCountsText(dump, true);
    try {
        recordStructureEdit(edit::addFrames(
            *m_document, importing::makeProjectFrames(*m_document, std::move(dump))));
        showStructureEditStatus(QStringLiteral("Frames added: %1; %2 frames now")
                                     .arg(summary)
                                     .arg(static_cast<qulonglong>(m_document->frameCount())),
                                 8000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::exportAnimatedGif()
{
    if (!m_document) {
        statusBar()->showMessage(QStringLiteral("No project loaded"), 2500);
        return;
    }
    std::vector<std::size_t> frames = selectedFrameIndices();
    std::sort(frames.begin(), frames.end());
    if (frames.empty()) {
        statusBar()->showMessage(QStringLiteral("Select the frames to export"), 2500);
        return;
    }
    GifExportOptions options;
    options.plane = isHdOnlyProject() ? render::Resolution::Hd
                    : m_document->editsOriginalPlaneOnly() ? render::Resolution::Sd
                                                           : m_activeResolution;
    options.includeBackground = !m_framePanel || m_framePanel->isBackgroundLayerChecked();
    const render::Size planeSize = m_document->geometry().planeSize(options.plane);
    // About 640 picture pixels across: 5 per pixel of a 128-wide plane, 3 of 192, 2 of 256.
    options.pixelScale = std::max(2, 640 / std::max(1, planeSize.width));

    const QDir untitledDirectory(m_dumpDirectory.isEmpty() ? QDir::homePath()
                                                           : m_dumpDirectory);
    const QString base =
        isUntitledProject()
            ? untitledDirectory.absoluteFilePath(QString::fromStdString(m_document->romName()))
            : QFileInfo(m_currentCromPath).absoluteDir().absoluteFilePath(
                  QFileInfo(m_currentCromPath).completeBaseName());
    QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Animated GIF"),
        QStringLiteral("%1_%2-%3.gif")
            .arg(base)
            .arg(static_cast<qulonglong>(frames.front()))
            .arg(static_cast<qulonglong>(frames.back())),
        QStringLiteral("Animated GIF (*.gif);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".gif");
    }

    GifExportResult result;
    const QString partial = path + QStringLiteral(".saving");
    {
        legacy::LegacyIoProgress progress;
        const std::optional<std::string> failure =
            runProjectTask(this, QStringLiteral("Exporting Animated GIF"), progress, [&]() {
                std::ofstream stream(platform::pathFromUtf8(partial.toStdString()),
                                     std::ios::binary | std::ios::trunc);
                if (!stream) {
                    result.error = "the file cannot be written";
                    return;
                }
                result = writeAnimatedGif(*m_document, frames, options, stream, &progress);
            });
        if (failure) {
            result = {};
            result.error = *failure;
        }
    }
    if (!result.ok) {
        QFile::remove(partial);
        if (result.cancelled) {
            statusBar()->showMessage(QStringLiteral("Export cancelled"), 3000);
        } else {
            QMessageBox::critical(this,
                                  QStringLiteral("Export Animated GIF Failed"),
                                  QString::fromStdString(result.error));
        }
        return;
    }
    QFile::remove(path);
    if (!QFile::rename(partial, path)) {
        QFile::remove(partial);
        QMessageBox::critical(this,
                              QStringLiteral("Export Animated GIF Failed"),
                              QStringLiteral("%1 cannot be replaced").arg(path));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1 frame(s) as %2 picture(s) to %3")
                                 .arg(static_cast<qulonglong>(frames.size()))
                                 .arg(static_cast<qulonglong>(result.pictures))
                                 .arg(QDir::toNativeSeparators(path)),
                             6000);
}

void MainWindow::openFrameTester()
{
    if (!m_document) {
        statusBar()->showMessage(QStringLiteral("No project loaded"), 2500);
        return;
    }
    // libserum holds one colorization: the Runtime view lets go of its own meanwhile.
    setPlaybackRunning(false);
    const bool runtimeWasOn = m_runtimeViewAction && m_runtimeViewAction->isChecked();
    if (runtimeWasOn) {
        m_runtimeViewAction->setChecked(false);
    }
    std::optional<std::size_t> frameToShow;
    {
        FrameTesterDialog dialog(*m_document, selectedFrameIndices(), this);
        dialog.onAddFrameRequested = [this](const runtime::TesterFrame& frame) {
            try {
                std::vector<document::FrameRecord> frames;
                frames.push_back(m_document->makeUncolorizedFrame(frame.original,
                                                                  frame.durationMs));
                recordStructureEdit(edit::addFrames(*m_document, std::move(frames)));
                showStructureEditStatus(QStringLiteral("Added the dump frame as frame %1")
                                            .arg(static_cast<qulonglong>(
                                                m_document->frameCount() - 1)),
                                        4000);
                return true;
            } catch (const std::exception& ex) {
                statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
                return false;
            }
        };
        dialog.exec();
        frameToShow = dialog.frameToShow();
    }
    if (runtimeWasOn) {
        m_runtimeViewAction->setChecked(true);
    }
    if (frameToShow && *frameToShow < m_document->frameCount()) {
        goToFrame(*frameToShow);
    }
}

void MainWindow::exportCromc()
{
    if (!m_document) {
        statusBar()->showMessage(QStringLiteral("No project loaded"), 2500);
        return;
    }
    const QString rom = QString::fromStdString(m_document->romName());
    const QString projectDirectory =
        isUntitledProject() ? QString() : QFileInfo(m_currentCromPath).absolutePath();
    const QDir proposedDirectory(!projectDirectory.isEmpty() ? projectDirectory
                                 : !m_dumpDirectory.isEmpty() ? m_dumpDirectory
                                                              : QDir::homePath());
    QString target = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export .cROMc"),
        proposedDirectory.absoluteFilePath(rom + QStringLiteral(".cROMc")),
        QStringLiteral("Serum colorization for machines (*.cROMc);;All files (*)"));
    if (target.isEmpty()) {
        return;
    }
    if (!target.endsWith(QStringLiteral(".cROMc"), Qt::CaseInsensitive)) {
        target += QStringLiteral(".cROMc");
    }

    // libserum holds one colorization: the Runtime view lets go of its own and takes it up
    // again afterwards.
    setPlaybackRunning(false);
    const bool runtimeWasOn = m_runtimeViewAction && m_runtimeViewAction->isChecked();
    if (runtimeWasOn) {
        m_runtimeViewAction->setChecked(false);
    }
    runtime::RuntimePreview::instance().close();

    const QString work = QDir::temp().absoluteFilePath(
        QStringLiteral("colorizingdmd_cromc_%1").arg(QCoreApplication::applicationPid()));
    runtime::CromcExportResult result;
    {
        const document::ProjectSnapshot snapshot = m_document->toSnapshot();
        legacy::LegacyIoProgress progress;
        const std::optional<std::string> failure =
            runProjectTask(this, QStringLiteral("Exporting .cROMc"), progress, [&]() {
                result = runtime::exportCromc(
                    snapshot,
                    platform::pathFromUtf8(work.toStdString()),
                    platform::pathFromUtf8(projectDirectory.toStdString()),
                    platform::pathFromUtf8(target.toStdString()),
                    &progress);
            });
        if (failure) {
            result = {};
            result.error = *failure;
        }
    }
    if (runtimeWasOn) {
        m_runtimeViewAction->setChecked(true);
    }

    if (result.cancelled) {
        statusBar()->showMessage(QStringLiteral("Export cancelled"), 3000);
        return;
    }
    if (!result.ok) {
        QMessageBox::critical(this,
                              QStringLiteral("Export .cROMc Failed"),
                              QString::fromStdString(result.error));
        return;
    }
    QStringList sidecars;
    for (const std::string& sidecar : result.sidecars) {
        sidecars << QString::fromStdString(sidecar);
    }
    QString note;
    if (QFileInfo(target).completeBaseName().compare(rom, Qt::CaseInsensitive) != 0) {
        note = QStringLiteral(". libserum looks for %1.cROMc").arg(rom);
    }
    statusBar()->showMessage(
        QStringLiteral("Exported %1%2%3")
            .arg(QDir::toNativeSeparators(target),
                 sidecars.isEmpty() ? QString()
                                    : QStringLiteral(" with %1").arg(sidecars.join(
                                          QStringLiteral(" and "))),
                 note),
        8000);
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
    // Saving a project opened from its archive writes the loose .cROM next to it.
    const QString savedCromPath =
        isCrzPath(cromPath) ? siblingProjectFile(cromPath, QStringLiteral("cROM")) : cromPath;
    if (!isAutosavePath(cromPath)) {
        const AutosaveFiles autosave = autosaveFilesFor(savedCromPath, crpPath);
        std::optional<QDateTime> autosaved = newerAutosaveTime(cromPath, crpPath, autosave);
        // An autosave whose changes were discarded is not offered again.
        const QDateTime discarded =
            QSettings()
                .value(QStringLiteral("autosave/discarded/") + autosave.cromPath)
                .toDateTime();
        if (autosaved && discarded.isValid() && *autosaved <= discarded) {
            autosaved.reset();
        }
        if (autosaved) {
            QMessageBox question(this);
            question.setIcon(QMessageBox::Question);
            question.setWindowTitle(QStringLiteral("Open Project"));
            question.setText(QStringLiteral("The project has an autosave from %1 that is newer "
                                            "than its files.")
                                 .arg(QLocale().toString(*autosaved, QLocale::ShortFormat)));
            question.setInformativeText(
                QStringLiteral("It holds changes that were not saved, for example after a "
                               "crash. Open Autosave opens it as this project with unsaved "
                               "changes: Save then writes them to the project files. %1 and "
                               "%2 are kept either way.")
                    .arg(QFileInfo(autosave.cromPath).fileName(),
                         QFileInfo(autosave.crpPath).fileName()));
            QPushButton* autosaveButton =
                question.addButton(QStringLiteral("Open Autosave"), QMessageBox::AcceptRole);
            QPushButton* projectButton =
                question.addButton(QStringLiteral("Open Project"), QMessageBox::RejectRole);
            question.addButton(QMessageBox::Cancel);
            question.setDefaultButton(autosaveButton);
            question.exec();
            if (question.clickedButton() == autosaveButton) {
                loadProjectFiles(autosave.cromPath, autosave.crpPath, cromPath, crpPath, true);
                return;
            }
            if (question.clickedButton() != projectButton) {
                return;
            }
        }
    }
    loadProjectFiles(cromPath, crpPath, cromPath, crpPath, false);
}

void MainWindow::loadProjectFrom(const AutosaveFiles& readFrom,
                                 const QString& cromPath,
                                 const QString& crpPath)
{
    loadProjectFiles(readFrom.cromPath, readFrom.crpPath, cromPath, crpPath, true);
}

void MainWindow::loadProjectFiles(const QString& readCromPath,
                                  const QString& readCrpPath,
                                  const QString& cromPath,
                                  const QString& crpPath,
                                  bool recovered)
{
    // Read and turned into a document on a worker thread (runProjectTask); the project that
    // is open stays open, unchanged, until the new one is complete, and is kept when the open
    // fails or is cancelled.
    const legacy::LegacyProjectPaths paths{
        readCromPath.toStdString(), readCrpPath.toStdString(), {}};
    legacy::LegacyDocumentLoadResult result;
    {
        legacy::LegacyIoProgress progress;
        const std::optional<std::string> failure =
            runProjectTask(this, QStringLiteral("Opening Project"), progress, [&]() {
                result = legacy::loadLegacyProjectDocument(paths, &progress);
            });
        if (failure) {
            result = {};
            result.error.message = *failure;
        }
    }

    if (result.cancelled) {
        setStatusText(QStringLiteral("Open cancelled"));
        return;
    }
    if (!result.ok) {
        QMessageBox::critical(this,
                              QStringLiteral("Open Project Failed"),
                              QString::fromStdString(result.error.message));
        setStatusText(QStringLiteral("Open failed"));
        return;
    }

    try {
        m_document = std::move(*result.document);
        result.document.reset();
        // Saving a project opened from its archive writes the loose .cROM next to it.
        m_currentCromPath = isCrzPath(cromPath)
                                ? siblingProjectFile(cromPath, QStringLiteral("cROM"))
                                : cromPath;
        m_currentCrpPath = crpPath;
        if (!cromPath.isEmpty() && !crpPath.isEmpty()) {
            rememberRecentProject(cromPath, crpPath);
        }
        if (recovered) {
            // What the autosave holds is not in the project files.
            m_document->markUnsaved();
        }
        m_undoHistories.clear();
        m_pasteMode.clearClipboard();
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

std::optional<Autosaver::Job> MainWindow::autosaveJob()
{
    // Not while the user is in a dialog, an open or a save: the next interval catches up.
    if (!m_document || !m_document->isDirty() ||
        m_document->revision() == m_autosavedRevision || QApplication::activeModalWidget()) {
        return std::nullopt;
    }
    Autosaver::Job job;
    job.files = isUntitledProject() ? untitledAutosaveFiles(m_document->romName())
                                    : autosaveFilesFor(m_currentCromPath, m_currentCrpPath);
    // A project opened from an autosave by hand is written over itself.
    if (isAutosavePath(m_currentCromPath)) {
        job.files = AutosaveFiles{m_currentCromPath, m_currentCrpPath};
    }
    job.revision = m_document->revision();
    job.snapshot = m_document->toSnapshot();
    return job;
}

void MainWindow::autosaveFinished(const AutosaveFiles& files,
                                  uint64_t revision,
                                  const legacy::LegacySaveResult& result)
{
    if (result.cancelled) {
        return;
    }
    if (!result.ok) {
        statusBar()->showMessage(QStringLiteral("Autosave failed: %1")
                                     .arg(QString::fromStdString(result.error.message)),
                                 8000);
        return;
    }
    m_autosavedRevision = revision;
    statusBar()->showMessage(QStringLiteral("Autosaved to %1 at %2")
                                 .arg(QDir::toNativeSeparators(files.cromPath),
                                      QLocale().toString(QTime::currentTime(),
                                                         QLocale::ShortFormat)),
                             5000);
}

void MainWindow::forgetAutosave()
{
    if (!m_document) {
        return;
    }
    // Nothing more of the discarded changes is written.
    m_autosaver->cancelAndWait();
    if (isUntitledProject()) {
        const AutosaveFiles files = untitledAutosaveFiles(m_document->romName());
        QFile::remove(files.cromPath);
        QFile::remove(files.crpPath);
        return;
    }
    // The files beside the project stay, as the Win32 editor leaves them; only the offer to
    // open them is dropped, up to their present time.
    const AutosaveFiles files = autosaveFilesFor(m_currentCromPath, m_currentCrpPath);
    const QFileInfo crom(files.cromPath);
    const QFileInfo crp(files.crpPath);
    if (crom.isFile() && crp.isFile()) {
        QSettings().setValue(QStringLiteral("autosave/discarded/") + files.cromPath,
                             std::max(crom.lastModified(), crp.lastModified()));
    }
}

void MainWindow::offerUntitledAutosave()
{
    const std::vector<AutosaveFiles> autosaves = untitledAutosaves();
    if (autosaves.empty()) {
        return;
    }
    const AutosaveFiles& newest = autosaves.front();
    const QFileInfo crom(newest.cromPath);
    QString name = crom.completeBaseName();
    name.chop(static_cast<int>(QStringLiteral("(auto)").size()));
    QMessageBox question(this);
    question.setIcon(QMessageBox::Question);
    question.setWindowTitle(QStringLiteral("Autosave Found"));
    question.setText(QStringLiteral("A new project of %1 that was never saved has an autosave "
                                    "from %2.")
                         .arg(name, QLocale().toString(crom.lastModified(),
                                                       QLocale::ShortFormat)));
    question.setInformativeText(
        QStringLiteral("Open opens it as a new project with unsaved changes. Delete removes the "
                       "autosave. Later keeps it and asks again at the next start.%1")
            .arg(autosaves.size() > 1
                     ? QStringLiteral(" %1 more autosave(s) of never saved projects are in %2.")
                           .arg(static_cast<int>(autosaves.size() - 1))
                           .arg(QDir::toNativeSeparators(untitledAutosaveDirectory()))
                     : QString()));
    QPushButton* openButton = question.addButton(QStringLiteral("Open"), QMessageBox::AcceptRole);
    QPushButton* deleteButton =
        question.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    QPushButton* laterButton = question.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    question.setDefaultButton(openButton);
    question.setEscapeButton(laterButton);
    question.exec();
    if (question.clickedButton() == openButton) {
        loadProjectFrom(newest, QString(), QString());
    } else if (question.clickedButton() == deleteButton) {
        QFile::remove(newest.cromPath);
        QFile::remove(newest.crpPath);
    }
}

void MainWindow::updateSameFrameControls()
{
    if (!m_sameFramesPanel) {
        return;
    }
    updateSpotCommonOverlay();
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    SameFramesPanelState state;
    state.hasFrame = m_document && frameIndex;
    // Hidden, the page costs nothing; it is brought up to date when it is shown.
    if (state.hasFrame && !m_sameFramesPanel->isVisible()) {
        return;
    }
    if (!state.hasFrame) {
        m_sameFramesPanel->refresh(state);
        m_sameFramesPanel->setMaskCounts(nullptr);
        m_sameFramesPanel->setMaskChoices(0, {});
        m_sameFramesPanel->setFramesUsingMask({});
        return;
    }
    state.frameCount = m_document->frameCount();
    state.selectedFrameCount = selectedFrameIndices().size();
    state.hasAssignedMask = currentComparisonMaskIndex().has_value();
    state.sameFrames = m_sameFrameFinder.sameFrames(
        *m_document, *frameIndex, edit::comparisonRuleOf(*m_document, *frameIndex));
    m_sameFramesPanel->refresh(state);
    const bool countsFit = m_sameFrameCounts && m_sameFrameCounts->frameIndex == *frameIndex &&
                           m_sameFrameCounts->identificationRevision ==
                               m_document->identificationRevision();
    m_sameFramesPanel->setMaskCounts(countsFit ? &m_sameFrameCounts->counts : nullptr);
    std::vector<std::size_t> useCounts;
    for (std::size_t mask = 0; mask < m_document->comparisonMaskCount(); ++mask) {
        useCounts.push_back(m_document->comparisonMaskUseCount(mask));
    }
    m_sameFramesPanel->setMaskChoices(
        edit::framesUsingComparisonMask(*m_document, document::kNoComparisonMaskId).size(),
        useCounts);
    showFramesUsingMask();
}

void MainWindow::updateSameFrameCounts()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || !m_sameFramesPanel) {
        return;
    }
    // One pass over all originals per mask and mode the first time.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    SameFrameCountsFor counts;
    counts.frameIndex = *frameIndex;
    counts.identificationRevision = m_document->identificationRevision();
    counts.counts = m_sameFrameFinder.countsPerMask(*m_document, *frameIndex);
    QApplication::restoreOverrideCursor();
    m_sameFrameCounts = std::move(counts);
    m_sameFramesPanel->setMaskCounts(&m_sameFrameCounts->counts);
    statusBar()->showMessage(
        QStringLiteral("Same frames of frame %1 counted for %2 mask(s)")
            .arg(static_cast<qulonglong>(*frameIndex))
            .arg(static_cast<int>(m_document->comparisonMaskCount())),
        3000);
}

void MainWindow::updateSpotCommonOverlay()
{
    if (!m_preview || !m_sameFramesPanel) {
        return;
    }
    if (!m_document || !m_sameFramesPanel->isSpotCommonChecked()) {
        m_preview->setDifferenceOverlay(std::nullopt);
        return;
    }
    m_preview->setDifferenceOverlay(edit::differingPixels(*m_document, selectedFrameIndices()));
}

void MainWindow::deleteSameFrames(SameFrameDeletion deletion)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || !m_sameFramesPanel) {
        statusBar()->showMessage(QStringLiteral("No frame selected"), 2500);
        return;
    }
    const std::vector<std::size_t> selected = selectedFrameIndices();
    const std::vector<std::size_t> kept =
        m_sameFramesPanel->keepsSelectedFrames() ? selected : std::vector<std::size_t>();
    std::vector<std::size_t> frames;
    QString what;
    switch (deletion) {
        case SameFrameDeletion::SameAsCurrent:
            frames = m_sameFrameFinder.duplicatesOf(*m_document, {*frameIndex}, kept);
            what = QStringLiteral("the same as frame %1 under its mask and shape mode")
                       .arg(static_cast<qulonglong>(*frameIndex));
            break;
        case SameFrameDeletion::SameAsSelected:
            frames = m_sameFrameFinder.duplicatesOf(*m_document, selected, kept);
            what = QStringLiteral("the same as one of the %1 selected frame(s) under that "
                                  "frame's mask and shape mode")
                       .arg(static_cast<qulonglong>(selected.size()));
            break;
        case SameFrameDeletion::AllDuplicates:
            frames = m_sameFrameFinder.allDuplicates(*m_document, kept);
            what = QStringLiteral("the same as another frame under that frame's mask and "
                                  "shape mode");
            break;
    }
    if (frames.empty()) {
        statusBar()->showMessage(QStringLiteral("Nothing to delete: no frame is %1%2")
                                     .arg(what, kept.empty() ? QString()
                                                             : QStringLiteral(" and not selected")),
                                 5000);
        return;
    }
    deleteFrames(frames,
                 QStringLiteral("%1 frame(s) are %2%3. The runtime cannot tell them apart.")
                     .arg(static_cast<qulonglong>(frames.size()))
                     .arg(what, kept.empty() ? QString()
                                             : QStringLiteral("; selected frames are kept")));
}

void MainWindow::writeCommonPixelsToMask()
{
    const std::vector<std::size_t> selected = selectedFrameIndices();
    if (!m_document || selected.size() < 2) {
        statusBar()->showMessage(
            QStringLiteral("Select the frames whose differing pixels the mask is to cover"), 4000);
        return;
    }
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        statusBar()->showMessage(
            QStringLiteral("Assign a comparison mask to the current frame first"), 4000);
        return;
    }
    const document::IndexedFrame differing = edit::differingPixels(*m_document, selected);
    const auto differingCount =
        std::count_if(differing.pixels.begin(), differing.pixels.end(),
                      [](uint8_t value) { return value != 0; });
    try {
        std::unique_ptr<edit::EditCommand> command =
            edit::replaceComparisonMask(*m_document, *maskIndex, differing);
        if (!command) {
            statusBar()->showMessage(
                QStringLiteral("Comparison mask %1 covers exactly the differing pixels already")
                    .arg(static_cast<int>(*maskIndex)),
                4000);
            return;
        }
        leaveSpriteCanvas();
        commitEdit(std::move(command));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    // The mask is shown, and its history is the one Undo reaches now.
    if (m_comparisonMaskOverlayButton && !m_runtimeView.isEnabled()) {
        m_comparisonMaskOverlayButton->setChecked(true);
    }
    refreshComponentBrowsers();
    updateSameFrameControls();
    updateUndoRedoActions();
    statusBar()->showMessage(
        QStringLiteral("Comparison mask %1 now covers the %2 pixel(s) that differ among the %3 "
                       "selected frames; %4 frame(s) use it")
            .arg(static_cast<int>(*maskIndex))
            .arg(static_cast<qlonglong>(differingCount))
            .arg(static_cast<qulonglong>(selected.size()))
            .arg(static_cast<qulonglong>(m_document->comparisonMaskUseCount(*maskIndex))),
        6000);
}

void MainWindow::showFramesUsingMask()
{
    if (!m_sameFramesPanel) {
        return;
    }
    if (!m_document) {
        m_sameFramesPanel->setFramesUsingMask({});
        return;
    }
    const int maskId = m_sameFramesPanel->usingMaskId();
    m_sameFramesPanel->setFramesUsingMask(edit::framesUsingComparisonMask(
        *m_document, maskId < 0 ? document::kNoComparisonMaskId : static_cast<uint8_t>(maskId)));
}

void MainWindow::selectFramesUsingMask()
{
    if (!m_document || !m_sameFramesPanel || !m_frameList) {
        return;
    }
    const int maskId = m_sameFramesPanel->usingMaskId();
    const std::vector<std::size_t> frames = edit::framesUsingComparisonMask(
        *m_document, maskId < 0 ? document::kNoComparisonMaskId : static_cast<uint8_t>(maskId));
    QListWidgetItem* first = nullptr;
    int listed = 0;
    {
        // Told to the window once, below.
        QSignalBlocker blocker(m_frameList);
        m_frameList->clearSelection();
        for (int row = 0; row < m_frameList->count(); ++row) {
            QListWidgetItem* item = m_frameList->item(row);
            const std::size_t frameIndex = item->data(Qt::UserRole).toULongLong();
            if (std::binary_search(frames.begin(), frames.end(), frameIndex)) {
                if (!first) {
                    first = item;
                    m_frameList->setCurrentItem(item, QItemSelectionModel::Select);
                }
                item->setSelected(true);
                ++listed;
            }
        }
    }
    const QString maskText = maskId < 0 ? QStringLiteral("no mask")
                                        : QStringLiteral("mask %1").arg(maskId);
    if (!first) {
        statusBar()->showMessage(
            QStringLiteral("No frame with %1 is in the frame list").arg(maskText), 4000);
        return;
    }
    selectFrame(m_frameList->currentRow());
    const int hidden = static_cast<int>(frames.size()) - listed;
    statusBar()->showMessage(
        QStringLiteral("Selected %1 frame(s) with %2%3")
            .arg(listed)
            .arg(maskText, hidden > 0 ? QStringLiteral("; %1 more are hidden by the frame "
                                                       "filter")
                                            .arg(hidden)
                                      : QString()),
        4000);
}

void MainWindow::updateSectionControls()
{
    if (!m_sectionBar) {
        return;
    }
    if (!m_document) {
        m_sectionBar->setContent(false, {}, {});
        m_sectionBar->setCurrent(std::nullopt, false, std::nullopt);
        m_sectionBarRevision.reset();
        return;
    }
    // The lists only when the project changed: this runs on every frame change.
    if (m_sectionBarRevision != m_document->revision()) {
        m_sectionBarRevision = m_document->revision();
        std::vector<SectionBarEntry> entries;
        for (const edit::Section& section : edit::sections(*m_document)) {
            entries.push_back(SectionBarEntry{QString::fromStdString(section.name),
                                              section.firstFrame});
        }
        std::vector<TriggerBarEntry> triggers;
        for (const edit::TriggerEntry& trigger : edit::triggerList(*m_document)) {
            triggers.push_back(TriggerBarEntry{trigger.frameIndex, trigger.triggerId});
        }
        m_sectionBar->setContent(true, entries, triggers);
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const std::optional<std::size_t> section =
        frameIndex ? edit::sectionOfFrame(*m_document, *frameIndex) : std::nullopt;
    const bool startsHere =
        frameIndex && edit::sectionStartingAt(*m_document, *frameIndex).has_value();
    m_sectionBar->setCurrent(section, startsHere, frameIndex);
}

void MainWindow::recordSectionsEdit(std::unique_ptr<edit::EditCommand> command,
                                    const QString& status)
{
    if (!command) {
        return;
    }
    recordEdit(std::move(command));
    statusBar()->showMessage(status, 3000);
}

void MainWindow::nameSectionAtCurrentFrame()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }
    const std::optional<std::size_t> existing = edit::sectionStartingAt(*m_document, *frameIndex);
    bool accepted = false;
    const QString name =
        QInputDialog::getText(
            this,
            existing ? QStringLiteral("Rename Section") : QStringLiteral("New Section"),
            existing ? QStringLiteral("Name of the section that starts at frame %1:")
                           .arg(static_cast<qulonglong>(*frameIndex))
                     : QStringLiteral("Name of the section that starts at frame %1 (up to 31 "
                                      "characters):")
                           .arg(static_cast<qulonglong>(*frameIndex)),
            QLineEdit::Normal,
            existing ? QString::fromStdString(edit::sectionName(*m_document, *existing))
                     : QString(),
            &accepted)
            .trimmed();
    if (!accepted) {
        return;
    }
    try {
        recordSectionsEdit(
            edit::addOrRenameSection(*m_document, *frameIndex, name.toStdString()),
            existing ? QStringLiteral("Renamed section %1 to \"%2\"")
                           .arg(static_cast<int>(*existing))
                           .arg(name)
                     : QStringLiteral("Section \"%1\" starts at frame %2")
                           .arg(name)
                           .arg(static_cast<qulonglong>(*frameIndex)));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QStringLiteral("Not named: %1").arg(QString::fromUtf8(ex.what())),
                                 4000);
    }
}

void MainWindow::deleteCurrentSection()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }
    const std::optional<std::size_t> section = edit::sectionOfFrame(*m_document, *frameIndex);
    if (!section) {
        return;
    }
    const QString name = QString::fromStdString(edit::sectionName(*m_document, *section));
    if (!confirmDeletion(QStringLiteral("Delete Section"),
                         QStringLiteral("Delete the section \"%1\" this frame is part of?")
                             .arg(name),
                         QStringLiteral("Its frames stay; they belong to the section before it "
                                        "then. The deletion can be undone."))) {
        return;
    }
    try {
        recordSectionsEdit(edit::deleteSection(*m_document, *section),
                           QStringLiteral("Deleted section \"%1\"").arg(name));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::orderSections()
{
    if (!m_document) {
        return;
    }
    const auto names = [this]() {
        QStringList list;
        for (const edit::Section& section : edit::sections(*m_document)) {
            list << QStringLiteral("%1 (frame %2)")
                        .arg(QString::fromStdString(section.name))
                        .arg(static_cast<qulonglong>(section.firstFrame));
        }
        return list;
    };
    SectionOrderDialog dialog(this);
    dialog.setNames(names(), -1);
    dialog.onMoveRequested = [this, &dialog, &names](int row, int offset) {
        try {
            recordSectionsEdit(
                edit::moveSection(*m_document, static_cast<std::size_t>(row), offset),
                QStringLiteral("Moved a section in the list"));
            dialog.setNames(names(), row + offset);
        } catch (const std::exception& ex) {
            statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        }
    };
    dialog.onSortRequested = [this, &dialog, &names]() {
        recordSectionsEdit(edit::sortSectionsByName(*m_document),
                           QStringLiteral("Sorted the sections by name"));
        dialog.setNames(names(), -1);
    };
    dialog.exec();
}

void MainWindow::goToFrame(std::size_t frameIndex)
{
    if (!m_frameList) {
        return;
    }
    if (QListWidgetItem* item = frameListItem(frameIndex)) {
        selectSingleRow(*m_frameList, m_frameList->row(item));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Frame %1 is hidden by the frame filter")
                                 .arg(static_cast<qulonglong>(frameIndex)),
                             4000);
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
    updateWindowTitle();
    updateEditMenuActions();
    m_selectedEditColor.reset();
    m_drawColorMode =
        m_document ? edit::drawColorModeFromStored(m_document->editorMetadata().drawColorMode)
                   : edit::DrawColorMode::SingleColor;
    updateEditColorControls(true);
    m_sameFrameCounts.reset();
    if (m_sameFramesPanel) {
        m_sameFramesPanel->setSpotCommonChecked(false);
    }
    if (m_addFramesFromDumpAction) {
        m_addFramesFromDumpAction->setEnabled(m_document.has_value());
    }
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
    m_frameRows.clear();
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

    rebuildFrameRows();

    if (selectedRow >= 0) {
        selectSingleRow(*m_frameList, selectedRow);
    } else if (m_frameList->count() > 0) {
        selectSingleRow(*m_frameList, 0);
    } else {
        selectFrame(-1);
    }
    m_frameThumbnails->scheduleRowsInView();
    updatePlaybackControls();
}

void MainWindow::rebuildFrameRows()
{
    m_frameRows.assign(m_document ? m_document->frameCount() : 0, -1);
    if (!m_frameList) {
        return;
    }
    for (int row = 0; row < m_frameList->count(); ++row) {
        const std::size_t frameIndex = m_frameList->item(row)->data(Qt::UserRole).toULongLong();
        if (frameIndex < m_frameRows.size()) {
            m_frameRows[frameIndex] = row;
        }
    }
}

QListWidgetItem* MainWindow::frameListItem(std::size_t frameIndex) const
{
    if (!m_frameList || frameIndex >= m_frameRows.size() || m_frameRows[frameIndex] < 0) {
        return nullptr;
    }
    QListWidgetItem* item = m_frameList->item(m_frameRows[frameIndex]);
    // The map is rebuilt with the list; an item that says otherwise means it was not.
    if (!item || item->data(Qt::UserRole).toULongLong() != frameIndex) {
        return nullptr;
    }
    return item;
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

QStringList MainWindow::frameReferenceProblemTexts(std::size_t frameIndex) const
{
    QStringList lines;
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return lines;
    }
    const document::FrameReferenceProblems problems =
        m_document->frameReferenceProblems(frameIndex);
    if (problems.comparisonMask) {
        lines << QStringLiteral("Comparison mask %1 does not exist; the frame is compared "
                                "without a mask")
                     .arg(static_cast<int>(m_document->comparisonMaskId(frameIndex)));
    }
    if (problems.background) {
        lines << QStringLiteral("Background %1 does not exist")
                     .arg(static_cast<int>(m_document->backgroundId(frameIndex)));
    }
    if (problems.sprite) {
        lines << QStringLiteral("A sprite slot names a sprite that does not exist");
    }
    if (problems.spriteSlotGap) {
        lines << QStringLiteral("Sprite slot %1 is empty: the sprites after slot %1 are ignored "
                                "by the runtime")
                     .arg(static_cast<int>(*problems.spriteSlotGap));
    }
    return lines;
}

QString MainWindow::frameListItemToolTip(std::size_t frameIndex) const
{
    QStringList lines;
    const QString gaps = frameHdGapsText(frameIndex);
    if (!gaps.isEmpty()) {
        lines << gaps;
    }
    lines << frameReferenceProblemTexts(frameIndex);
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
    if (m_document->hasDynamicPixels(frameIndex)) {
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
    if (m_document->frameReferenceProblems(frameIndex).any()) {
        details << QStringLiteral("ref. problem");
    }
    QString first = QString::number(static_cast<qulonglong>(frameIndex));
    if (const std::optional<std::size_t> section =
            edit::sectionStartingAt(*m_document, frameIndex)) {
        first += QStringLiteral(" [%1]").arg(
            QString::fromStdString(edit::sectionName(*m_document, *section)));
    }
    return QStringLiteral("%1\n%2").arg(first, details.join(QStringLiteral("\n")));
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

void MainWindow::scheduleMaskIconUpdate(std::size_t maskIndex)
{
    if (std::find(m_pendingMaskIcons.begin(), m_pendingMaskIcons.end(), maskIndex) !=
        m_pendingMaskIcons.end()) {
        return;
    }
    if (m_pendingMaskIcons.empty()) {
        QTimer::singleShot(0, this, [this]() { updatePendingMaskIcons(); });
    }
    m_pendingMaskIcons.push_back(maskIndex);
}

void MainWindow::updatePendingMaskIcons()
{
    const std::vector<std::size_t> pending = std::move(m_pendingMaskIcons);
    m_pendingMaskIcons.clear();
    if (!m_maskList || !m_document) {
        return;
    }
    for (std::size_t maskIndex : pending) {
        if (maskIndex >= m_document->comparisonMaskCount() ||
            maskIndex >= static_cast<std::size_t>(m_maskList->count())) {
            continue;
        }
        QListWidgetItem* item = m_maskList->item(static_cast<int>(maskIndex));
        if (item && item->data(Qt::UserRole).toInt() == static_cast<int>(maskIndex)) {
            item->setIcon(previewIcon(renderMaskPreviewImage(m_document->comparisonMask(maskIndex)),
                                      QSize(160, 42)));
        }
    }
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
        return m_document->hasDynamicPixels(frameIndex);
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
    for (const QString& problem : frameReferenceProblemTexts(*frameIndex)) {
        toolTip << QStringLiteral("%1.").arg(problem);
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
        m_pasteMode.cancel();
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
    selectSingleRow(*m_frameList, nextRow);
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
    updateSectionControls();
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
    updateStructureControls();
    updateSameFrameControls();
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
    updateStructureControls();
}

MainWindow::FrameEditCount MainWindow::editSelectedFramesAttribute(
    edit::FrameAttribute attribute,
    uint32_t value)
{
    FrameEditCount count;
    if (!m_document) {
        return count;
    }
    // Each frame gets its own command, but the views are refreshed once for all of them, as
    // refreshAfterEdit() does for one: with every frame selected, a refresh per frame would
    // cost the number of frames squared.
    std::vector<std::size_t> changedFrames;
    const auto refreshViews = [this, &changedFrames]() {
        if (changedFrames.empty()) {
            return;
        }
        m_preview->update();
        updateFrameMetadataControls();
        updateFrameListItems(changedFrames);
        updateFrameStatusText();
        if (m_playbackTimer && m_playbackTimer->isActive()) {
            m_playbackTimer->setInterval(playbackIntervalForCurrentFrame());
        }
        updateProjectHeaderStatus();
    };
    try {
        for (std::size_t frameIndex : selectedFrameIndices()) {
            if (attribute == edit::FrameAttribute::BackgroundId &&
                !m_document->backgroundData(frameIndex)) {
                ++count.skipped;
                continue;
            }
            const uint32_t oldValue =
                edit::frameAttributeValue(*m_document, frameIndex, attribute);
            if (oldValue == value) {
                continue;
            }
            auto command = std::make_unique<edit::FrameAttributeEditCommand>(frameIndex,
                                                                             attribute,
                                                                             oldValue,
                                                                             value);
            command->apply(*m_document);
            const edit::HistoryKey key = edit::historyKey(command->target());
            m_undoHistories.push(key, std::move(command));
            changedFrames.push_back(frameIndex);
            ++count.changed;
        }
    } catch (...) {
        refreshViews();
        throw;
    }
    refreshViews();
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
        // A sprite meant for an empty slot goes to the frame's first empty slot: libserum's
        // sprite list ends at the first empty slot, so none may come before a used one.
        int assigned = 0;
        int moved = 0;
        const std::optional<std::size_t> currentFrame = currentFrameIndex();
        std::optional<std::size_t> currentSlot;
        for (std::size_t frameIndex : frameIndices) {
            const std::size_t slot = m_document->spriteSlotForAssignment(frameIndex, *slotIndex);
            const std::vector<document::FrameSpriteReference>& references =
                m_document->frameSpriteReferences(frameIndex);
            document::FrameSpriteReference reference =
                slot < references.size() ? references[slot] : document::FrameSpriteReference{};
            if (slot >= references.size()) {
                // A frame without a slot list: the whole frame, as for a new frame.
                const render::Size size = m_document->originalFrameSize();
                reference.maxX = static_cast<uint16_t>(size.width - 1);
                reference.maxY = static_cast<uint16_t>(size.height - 1);
            }
            reference.spriteId = static_cast<uint8_t>(spriteIndex);
            std::vector<document::FrameSpriteReference> before = references;
            m_document->setFrameSpriteReference(frameIndex, slot, reference);
            pushEdit(edit::frameSpriteSlotsCommand(*m_document, frameIndex, std::move(before)));
            updateFrameListItem(frameIndex);
            ++assigned;
            moved += slot != *slotIndex ? 1 : 0;
            if (currentFrame && frameIndex == *currentFrame) {
                currentSlot = slot;
            }
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        if (currentSlot) {
            m_spritePanel->setSelectedSlotIndex(*currentSlot);
            updateSpriteControls();
        }
        updateFrameStatusText();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Assigned sprite %1 to slot %2 on %3 frame(s)%4%5")
                .arg(spriteIndex)
                .arg(static_cast<int>(*slotIndex))
                .arg(assigned)
                .arg(moved > 0 ? QStringLiteral(" (on %1 of them to the first empty slot, "
                                                "before which no slot may stay empty)")
                                     .arg(moved)
                               : QString())
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
    std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!m_document || !frameIndex || !m_spritePanel) {
        statusBar()->showMessage(QStringLiteral("Select a frame first"), 2500);
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
        spriteIndex ? QStringLiteral("Replace sprite %1 with the captured pixels, or create a new "
                                     "sprite from them.")
                          .arg(static_cast<int>(*spriteIndex))
                    : QStringLiteral("Create a new sprite from the captured pixels."));
    QPushButton* replaceButton =
        confirm.addButton(QStringLiteral("Replace Current"), QMessageBox::AcceptRole);
    replaceButton->setEnabled(spriteIndex.has_value());
    QPushButton* createButton =
        confirm.addButton(QStringLiteral("Create New"), QMessageBox::ActionRole);
    createButton->setEnabled(m_document->spriteCount() < document::kMaxSprites);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(spriteIndex ? replaceButton : createButton);
    confirm.exec();
    if (confirm.clickedButton() == createButton) {
        // The new sprite is its own undo step, the capture into it another.
        spriteIndex = addNamedSprite();
        if (!spriteIndex) {
            return;
        }
    } else if (confirm.clickedButton() != replaceButton) {
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
        edit::SpriteState spriteBefore = edit::spriteState(*m_document, *spriteIndex);
        std::vector<document::FrameSpriteReference> slotsBefore =
            m_document->frameSpriteReferences(*frameIndex);
        // As for Assign: an empty slot behind the first empty one is not used.
        const std::size_t targetSlot = m_document->spriteSlotForAssignment(*frameIndex, *slotIndex);
        try {
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
            m_document->setSpriteDetectionArea(*spriteIndex,
                                               0,
                                               document::SpriteDetectionArea{
                                                   0,
                                                   0,
                                                   static_cast<uint16_t>(captureWidth),
                                                   static_cast<uint16_t>(captureHeight),
                                               });
            // The cRP names the frame a sprite was taken from, as the Win32 editor does.
            m_document->setSpriteSourceFrame(*spriteIndex, static_cast<uint32_t>(*frameIndex));
            m_document->setFrameSpriteReference(*frameIndex,
                                                targetSlot,
                                                document::FrameSpriteReference{
                                                    static_cast<uint8_t>(*spriteIndex),
                                                    static_cast<uint16_t>(captureMinX),
                                                    static_cast<uint16_t>(captureMinY),
                                                    static_cast<uint16_t>(captureMaxX),
                                                    static_cast<uint16_t>(captureMaxY),
                                                });
        } catch (...) {
            m_document->restoreSprite(*spriteIndex, spriteBefore.sprite);
            m_document->setSpriteSourceFrame(*spriteIndex, spriteBefore.sourceFrame);
            m_document->restoreFrameSpriteReferences(*frameIndex, slotsBefore);
            throw;
        }
        // One undo step in the history of the frame the capture was made on, also when its
        // slots stay as they were.
        std::vector<std::unique_ptr<edit::EditCommand>> commands;
        commands.push_back(std::make_unique<edit::FrameSpriteSlotsCommand>(
            *frameIndex, std::move(slotsBefore), m_document->frameSpriteReferences(*frameIndex)));
        commands.push_back(
            edit::spriteStateCommand(*m_document, *spriteIndex, std::move(spriteBefore)));
        pushEdit(edit::groupCommands(std::move(commands)));
        if (!selectedPixels.empty()) {
            m_canvasEdit.clearCaptureSelection();
        }
        setSpriteDependentViewsDirty();
        applyPendingSpriteDependentViews();
        refreshComponentBrowsers();
        updateSpriteControls();
        if (targetSlot != *slotIndex) {
            m_spritePanel->setSelectedSlotIndex(targetSlot);
            updateSpriteControls();
        }
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
    const std::optional<std::size_t> currentFrame = currentFrameIndex();
    if (!m_document || frameIndices.empty() || !currentFrame || !m_spritePanel) {
        return;
    }

    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    const std::vector<document::FrameSpriteReference>& currentReferences =
        m_document->frameSpriteReferences(*currentFrame);
    if (!slotIndex || *slotIndex >= currentReferences.size() ||
        currentReferences[*slotIndex].spriteId == document::kNoSpriteId) {
        updateSpriteControls();
        return;
    }

    // As the Win32 editor's IDC_DELSPRITE2: the sprite of the current frame's selected slot
    // leaves every selected frame that has it, from the last slot that holds it, and the
    // slots behind move up so that the list keeps no empty slot before a used one.
    const uint8_t spriteId = currentReferences[*slotIndex].spriteId;
    try {
        int changed = 0;
        int skipped = 0;
        for (std::size_t frameIndex : frameIndices) {
            const std::vector<document::FrameSpriteReference>& references =
                m_document->frameSpriteReferences(frameIndex);
            std::optional<std::size_t> last;
            for (std::size_t slot = 0; slot < references.size(); ++slot) {
                if (references[slot].spriteId == spriteId) {
                    last = slot;
                }
            }
            if (!last) {
                ++skipped;
                continue;
            }
            std::vector<document::FrameSpriteReference> before = references;
            m_document->clearFrameSpriteReference(frameIndex, *last);
            pushEdit(edit::frameSpriteSlotsCommand(*m_document, frameIndex, std::move(before)));
            updateFrameListItem(frameIndex);
            ++changed;
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            QStringLiteral("Removed sprite %1 from %2")
                .arg(static_cast<int>(spriteId))
                .arg(frameEditCountText(changed, skipped,
                                        QStringLiteral("without that sprite"))),
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
            std::vector<document::FrameSpriteReference> before = references;
            m_document->setFrameSpriteReference(frameIndex, *slotIndex, updated);
            pushEdit(edit::frameSpriteSlotsCommand(*m_document, frameIndex, std::move(before)));
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
        const std::size_t sprite = static_cast<std::size_t>(spriteIndex);
        edit::SpriteState before = edit::spriteState(*m_document, sprite);
        m_document->setSpriteName(sprite, name.toStdString());
        pushEdit(edit::spriteStateCommand(*m_document, sprite, std::move(before)));
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
        edit::SpriteState before =
            edit::spriteState(*m_document, static_cast<std::size_t>(spriteIndex));
        m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                           areaIndex,
                                           document::SpriteDetectionArea{
                                               static_cast<uint16_t>(x),
                                               static_cast<uint16_t>(y),
                                               static_cast<uint16_t>(width),
                                               static_cast<uint16_t>(height),
                                           });
        pushEdit(edit::spriteStateCommand(
            *m_document, static_cast<std::size_t>(spriteIndex), std::move(before)));
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
        edit::SpriteState before =
            edit::spriteState(*m_document, static_cast<std::size_t>(spriteIndex));
        m_document->setSpriteDetectionArea(static_cast<std::size_t>(spriteIndex),
                                           areaIndex,
                                           document::SpriteDetectionArea{
                                               0xffff,
                                               0xffff,
                                               0xffff,
                                               0xffff,
                                           });
        pushEdit(edit::spriteStateCommand(
            *m_document, static_cast<std::size_t>(spriteIndex), std::move(before)));
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
        const std::optional<uint16_t> picked =
            ColorPickerDialog::getColor(oldRgb565, this, QStringLiteral("Dynamic Color"));
        if (!picked) {
            return;
        }

        const uint16_t newRgb565 = *picked;
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
        const std::optional<uint16_t> picked =
            ColorPickerDialog::getColor(oldRgb565, this, QStringLiteral("Dynamic Shadow Color"));
        if (!picked) {
            return;
        }

        const uint16_t newRgb565 = *picked;
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
            document::CompactArray<uint16_t> before =
                m_document->colorRotations(frameIndex, m_activeResolution);
            m_document->setColorRotationLength(frameIndex,
                                               m_activeResolution,
                                               slotIndex,
                                               storedLength);
            pushEdit(edit::colorRotationsCommand(
                *m_document, frameIndex, m_activeResolution, std::move(before)));
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
            document::CompactArray<uint16_t> before =
                m_document->colorRotations(frameIndex, m_activeResolution);
            m_document->setColorRotationDelayMs(frameIndex,
                                                m_activeResolution,
                                                slotIndex,
                                                storedDelayMs);
            pushEdit(edit::colorRotationsCommand(
                *m_document, frameIndex, m_activeResolution, std::move(before)));
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
    const std::optional<uint16_t> picked =
        ColorPickerDialog::getColor(oldColor, this, QStringLiteral("Color Rotation Color"));
    if (!picked) {
        return;
    }
    const uint16_t newRgb565 = *picked;
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
                document::CompactArray<uint16_t> before =
                    m_document->colorRotations(selectedFrameIndex, m_activeResolution);
                m_document->setColorRotationColor(selectedFrameIndex,
                                                  m_activeResolution,
                                                  slotIndex,
                                                  colorIndex,
                                                  newRgb565);
                pushEdit(edit::colorRotationsCommand(
                    *m_document, selectedFrameIndex, m_activeResolution, std::move(before)));
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
            document::CompactArray<uint16_t> before =
                m_document->colorRotations(frameIndex, m_activeResolution);
            m_document->clearColorRotationSlot(frameIndex, m_activeResolution, slotIndex);
            pushEdit(edit::colorRotationsCommand(
                *m_document, frameIndex, m_activeResolution, std::move(before)));
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
    const std::optional<uint16_t> picked =
        ColorPickerDialog::getColor(oldColor, this, QStringLiteral("Palette Color"));
    if (!picked) {
        return;
    }
    const uint16_t newRgb565 = *picked;
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
    updateStructureControls();

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
    context.drawColorMode = m_drawColorMode;
    context.dynamicSet = activeDynamicSet();
    context.activePlane = m_activeResolution;
    context.includeBackgroundLayer = m_framePanel && m_framePanel->isBackgroundLayerChecked();
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
    outputs.pickEditColorSlot = [this](uint16_t rgb565) {
        pickEditColorSlot(rgb565);
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

PasteModeOutputs MainWindow::pasteModeOutputs()
{
    PasteModeOutputs outputs;
    outputs.commit = [this](std::unique_ptr<edit::EditCommand> command) {
        commitEdit(std::move(command));
    };
    outputs.commitsFinished = [this]() { updateUndoRedoActions(); };
    outputs.showStatus = [this](const QString& text, int timeoutMs) {
        statusBar()->showMessage(text, timeoutMs);
    };
    outputs.showOverlay = [this](std::optional<CanvasPasteOverlay> overlay) {
        if (m_preview) {
            m_preview->setPasteOverlay(std::move(overlay));
        }
    };
    outputs.activeChanged = [this](bool active) {
        if (!m_preview) {
            return;
        }
        if (active) {
            m_preview->setHoverHandler([this](render::CanvasHit hit) { m_pasteMode.follow(hit); });
            m_preview->setFocus(Qt::OtherFocusReason);
        } else {
            m_preview->setHoverHandler(nullptr);
        }
    };
    outputs.exportImage = [](const QImage& image) {
        if (QClipboard* clipboard = QGuiApplication::clipboard()) {
            clipboard->setImage(image);
        }
    };
    return outputs;
}

EditHelpersOutputs MainWindow::editHelpersOutputs()
{
    EditHelpersOutputs outputs;
    outputs.commit = [this](std::unique_ptr<edit::EditCommand> command) {
        commitEdit(std::move(command));
    };
    outputs.commitsFinished = [this]() { updateUndoRedoActions(); };
    outputs.showStatus = [this](const QString& text, int timeoutMs) {
        statusBar()->showMessage(text, timeoutMs);
    };
    return outputs;
}

void MainWindow::createSelectionActions(QMenu* editMenu)
{
    m_editMenuActions = new EditMenuActions(editMenu, this);
    EditMenuActions& actions = *m_editMenuActions;
    actions.onCopy = [this]() {
        if (isSpriteCanvasActive()) {
            copySpriteToClipboard();
            return;
        }
        if (canUseFrameCanvasHelpers()) {
            m_pasteMode.copy(canvasEditContext(), m_canvasEdit.selection());
            updateEditMenuActions();
        }
    };
    actions.onPaste = [this]() {
        if (isSpriteCanvasActive()) {
            pasteClipboardIntoSprite();
            return;
        }
        startPaste(edit::PasteMode::All);
    };
    actions.onPasteWhereOriginalsMatch = [this]() {
        startPaste(edit::PasteMode::WhereOriginalsMatch);
    };
    actions.onPasteDynamicMaskOnly = [this]() { startPaste(edit::PasteMode::DynamicMaskOnly); };
    actions.onSelectAll = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.selectAll(canvasEditContext());
        }
    };
    actions.onSelectNone = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.clearSelection(canvasEditContext());
            statusBar()->showMessage(QStringLiteral("Selection cleared"), 1500);
        }
    };
    actions.onInvertSelection = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.invertSelection(canvasEditContext());
        }
    };
    actions.onSelectByColor = [this]() { armCanvasClick(PendingCanvasClick::SelectByColor); };
    actions.onSelectBackgroundMask = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.selectBackgroundMask(canvasEditContext());
        }
    };
    actions.onMirrorHorizontally = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.mirrorSelection(canvasEditContext(), true, false);
        }
    };
    actions.onMirrorVertically = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.mirrorSelection(canvasEditContext(), false, true);
        }
    };
    actions.onAutoSelect = [this]() { showAutoSelectDialog(); };
    actions.onAutoCopy = [this]() { showAutoCopyDialog(); };
    actions.onScrollingFill = [this]() { runScrollingAutoFill(); };
    actions.onRecolor = [this]() { armCanvasClick(PendingCanvasClick::Recolor); };
    actions.onBlackOriginalToBlack = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_editHelpers.blackOriginalToBlack(canvasEditContext(), m_canvasEdit.selection());
        }
    };
    actions.onRemoveDynamicZones = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_editHelpers.removeDynamicContent(canvasEditContext(), m_canvasEdit.selection());
        }
    };
    actions.onAssignDynamicSet = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_editHelpers.assignDynamicSet(canvasEditContext(), m_canvasEdit.selection());
        }
    };
}

void MainWindow::updateEditMenuActions()
{
    if (m_editMenuActions) {
        m_editMenuActions->setState(m_document.has_value(), m_pasteMode.clipboard() != nullptr);
    }
}

bool MainWindow::canUseFrameCanvasHelpers()
{
    if (!m_document || !currentFrameIndex()) {
        statusBar()->showMessage(QStringLiteral("No frame"), 2000);
        return false;
    }
    if (isSpriteCanvasActive()) {
        statusBar()->showMessage(
            QStringLiteral("On the sprite canvas only Copy and Paste work: the selection tools "
                           "work on the frame canvas"),
            3000);
        return false;
    }
    if (m_runtimeView.isEnabled()) {
        statusBar()->showMessage(QStringLiteral("Nothing can be edited in the Runtime view"),
                                 3000);
        return false;
    }
    return true;
}

render::Resolution MainWindow::spriteCanvasPlane(std::size_t spriteIndex) const
{
    // As the sprite canvas and its undo histories: SD for a sprite without HD data.
    if (isHdOnlyProject()) {
        return render::Resolution::Hd;
    }
    if (m_activeResolution == render::Resolution::Hd && m_document &&
        m_document->hasHdSprite(spriteIndex)) {
        return render::Resolution::Hd;
    }
    return render::Resolution::Sd;
}

void MainWindow::copySpriteToClipboard()
{
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!m_document || !spriteIndex) {
        return;
    }
    try {
        edit::CopyBuffer buffer =
            edit::copySprite(*m_document, *spriteIndex, spriteCanvasPlane(*spriteIndex));
        const QString text = QStringLiteral("Copied sprite %1 (%2x%3)")
                                 .arg(static_cast<int>(*spriteIndex))
                                 .arg(buffer.box.width)
                                 .arg(buffer.box.height);
        m_pasteMode.setClipboard(std::move(buffer));
        updateEditMenuActions();
        statusBar()->showMessage(text, 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QStringLiteral("Not copied: %1").arg(QString::fromUtf8(ex.what())),
                                 4000);
    }
}

void MainWindow::pasteClipboardIntoSprite()
{
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    const edit::CopyBuffer* buffer = m_pasteMode.clipboard();
    if (!m_document || !spriteIndex) {
        return;
    }
    if (!buffer) {
        statusBar()->showMessage(QStringLiteral("Nothing has been copied"), 2500);
        return;
    }
    try {
        recordEdit(edit::pasteIntoSprite(
            *m_document, *spriteIndex, *buffer, spriteCanvasPlane(*spriteIndex), false, false));
        setSpriteDependentViewsDirty();
        updateSpriteControls();
        statusBar()->showMessage(QStringLiteral("Pasted into sprite %1")
                                     .arg(static_cast<int>(*spriteIndex)),
                                 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QStringLiteral("Not pasted: %1").arg(QString::fromUtf8(ex.what())),
                                 4000);
    }
}

void MainWindow::startPaste(edit::PasteMode mode)
{
    if (!canUseFrameCanvasHelpers()) {
        return;
    }
    armCanvasClick(PendingCanvasClick::None);
    m_canvasEdit.cancelFreeGradient();
    m_pasteMode.begin(canvasEditContext(), mode);
}

void MainWindow::showAutoSelectDialog()
{
    if (!canUseFrameCanvasHelpers()) {
        return;
    }
    AutoSelectDialog dialog(this);
    dialog.setPattern(m_autoSelectPattern);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    m_autoSelectPattern = dialog.pattern();
    m_canvasEdit.selectStripes(canvasEditContext(), m_autoSelectPattern);
}

void MainWindow::showAutoCopyDialog()
{
    if (!canUseFrameCanvasHelpers()) {
        return;
    }
    const CanvasEditContext context = canvasEditContext();
    const edit::PlaneSelection* selection = m_canvasEdit.selection();
    const QString refusal = EditHelpersController::autoCopyRefusal(context, selection);
    if (!refusal.isEmpty()) {
        statusBar()->showMessage(refusal, 4000);
        return;
    }
    std::optional<AutoCopyPlan> plan;
    const std::vector<std::size_t> selectedOthers = [&context]() {
        std::vector<std::size_t> frames;
        for (std::size_t frameIndex : context.selectedFrames) {
            if (frameIndex != *context.currentFrame) {
                frames.push_back(frameIndex);
            }
        }
        return frames;
    }();
    AutoCopyDialog dialog(
        !selectedOthers.empty(),
        [&](bool selectedOnly) {
            plan = m_editHelpers.planAutoCopy(
                context, *selection, selectedOnly ? selectedOthers : std::vector<std::size_t>{});
            QString text =
                QStringLiteral("%1 pixels selected. %2 frame(s) show the same there, %3 of them "
                               "would change.")
                    .arg(static_cast<qulonglong>(selection->count()))
                    .arg(static_cast<qulonglong>(plan->scan.matches.size()))
                    .arg(static_cast<qulonglong>(plan->changingFrames.size()));
            if (plan->scan.withoutPlane > 0) {
                text += QStringLiteral(" %1 more without a picture on this plane are left alone.")
                            .arg(static_cast<qulonglong>(plan->scan.withoutPlane));
            }
            if (plan->scan.hashCollisions > 0) {
                text += QStringLiteral(" %1 frame(s) had the same hash but other pixels.")
                            .arg(static_cast<qulonglong>(plan->scan.hashCollisions));
            }
            return text;
        },
        this);
    if (dialog.exec() != QDialog::Accepted || !plan) {
        return;
    }
    if (plan->changingFrames.empty()) {
        statusBar()->showMessage(QStringLiteral("Auto copy: nothing to change"), 2500);
        return;
    }
    m_editHelpers.applyAutoCopy(context, *selection, *plan);
}

void MainWindow::runScrollingAutoFill()
{
    if (!canUseFrameCanvasHelpers()) {
        return;
    }
    const CanvasEditContext context = canvasEditContext();
    const QString refusal = EditHelpersController::scrollFillRefusal(context);
    if (!refusal.isEmpty()) {
        statusBar()->showMessage(refusal, 4000);
        return;
    }
    ScrollFillPlan plan = m_editHelpers.planScrollFill(context);
    if (plan.lastFrame < plan.firstFrame + 2) {
        statusBar()->showMessage(
            QStringLiteral("Select the frames of the scrolling, the current one among them: the "
                           "frames between the first and the last are filled"),
            5000);
        return;
    }
    std::size_t fromFirst = 0;
    std::size_t fromLast = 0;
    for (const edit::ScrollFillFrame& frame : plan.frames) {
        fromFirst += frame.fromFirst ? 1 : 0;
        fromLast += frame.fromLast ? 1 : 0;
    }
    QMessageBox question(this);
    question.setIcon(QMessageBox::Question);
    question.setWindowTitle(QStringLiteral("Scrolling Auto Fill"));
    question.setText(QStringLiteral("Fill the %1 frame(s) between frame %2 and frame %3?")
                         .arg(static_cast<qulonglong>(plan.frames.size()))
                         .arg(static_cast<qulonglong>(plan.firstFrame))
                         .arg(static_cast<qulonglong>(plan.lastFrame)));
    question.setInformativeText(
        QStringLiteral("%1 scroll from the first frame, %2 from the last; %3 would change.")
            .arg(static_cast<qulonglong>(fromFirst))
            .arg(static_cast<qulonglong>(fromLast))
            .arg(static_cast<qulonglong>(plan.changingCount())));
    question.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    question.setDefaultButton(QMessageBox::Ok);
    if (question.exec() != QMessageBox::Ok) {
        return;
    }
    m_editHelpers.applyScrollFill(context, std::move(plan));
}

void MainWindow::armCanvasClick(PendingCanvasClick click)
{
    m_pendingCanvasClick = click;
    if (click == PendingCanvasClick::None) {
        return;
    }
    if (!canUseFrameCanvasHelpers()) {
        m_pendingCanvasClick = PendingCanvasClick::None;
        return;
    }
    m_pasteMode.cancel();
    statusBar()->showMessage(click == PendingCanvasClick::SelectByColor
                                 ? QStringLiteral("Click a pixel to select every pixel of its "
                                                  "color (Esc cancels)")
                                 : QStringLiteral("Click a pixel to recolor every pixel of its "
                                                  "color (Esc cancels)"),
                             5000);
    if (m_preview) {
        m_preview->setFocus(Qt::OtherFocusReason);
    }
}

bool MainWindow::interceptCanvasPress(render::CanvasHit hit, Qt::KeyboardModifiers modifiers)
{
    if (m_pasteMode.isActive()) {
        m_pasteMode.follow(hit);
        m_pasteMode.stamp(canvasEditContext(), modifiers);
        return true;
    }
    if (m_pendingCanvasClick == PendingCanvasClick::None) {
        return false;
    }
    const PendingCanvasClick click = m_pendingCanvasClick;
    m_pendingCanvasClick = PendingCanvasClick::None;
    if (click == PendingCanvasClick::SelectByColor) {
        m_canvasEdit.selectByColor(canvasEditContext(),
                                   hit,
                                   modifiers.testFlag(Qt::ShiftModifier));
    } else {
        m_editHelpers.recolor(canvasEditContext(), hit);
    }
    return true;
}

bool MainWindow::selectsWithPress() const
{
    // Ctrl, which is Cmd on macOS; there the Control key (Meta to Qt) works too, as a plain
    // Control+click may be a right click to the system.
    const Qt::KeyboardModifiers modifiers = m_preview ? m_preview->pressModifiers()
                                                      : Qt::KeyboardModifiers();
    return (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) &&
           CanvasEditController::canEditSelection(canvasEditContext());
}

bool MainWindow::selectsNonBlackOnly() const
{
    return m_nonBlackKeyDown || (m_editMenuActions && m_editMenuActions->nonBlackOnly());
}

void MainWindow::colorizedCanvasClicked(render::CanvasHit hit, bool erase)
{
    const CanvasEditContext context = canvasEditContext();
    const Qt::KeyboardModifiers modifiers =
        m_preview ? m_preview->pressModifiers() : Qt::KeyboardModifiers();
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    if (selectsWithPress()) {
        if (alt) {
            // Win32: Alt+Ctrl+click with the fill tool selects every pixel of the color.
            if (m_drawingTool == DrawingTool::FloodFill) {
                m_canvasEdit.selectByColor(context, hit, erase);
            }
            return;
        }
        m_canvasEdit.editSelectionAtPoint(context, hit, erase, selectsNonBlackOnly());
        return;
    }
    if (alt && m_drawingTool == DrawingTool::FloodFill &&
        context.layer == CanvasEditLayer::FramePixels) {
        // Win32: Alt+click with the fill tool recolors every pixel of the color.
        m_editHelpers.recolor(context, hit);
        return;
    }
    m_canvasEdit.colorizedPixelClicked(context, hit, erase);
}

void MainWindow::setActiveDrawColor(uint16_t rgb565)
{
    m_activeDrawColor = rgb565;
    m_selectedEditColor.reset();
    if (m_editColorBar) {
        m_editColorBar->setSelectedSlot(std::nullopt);
    }
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

void MainWindow::editFavoriteColor(std::size_t favoriteIndex)
{
    if (favoriteIndex >= m_favoriteColors.size()) {
        return;
    }
    const std::optional<uint16_t> picked = ColorPickerDialog::getColor(
        m_favoriteColors[favoriteIndex], this, QStringLiteral("Favorite Color"));
    if (picked) {
        setFavoriteDrawColor(favoriteIndex, *picked);
    }
}

void MainWindow::selectEditColorSlot(std::size_t slot)
{
    if (!m_document || slot >= m_document->colorCount() ||
        slot >= document::kEditColorCount) {
        return;
    }
    setActiveDrawColor(m_document->editColor(slot));
    m_selectedEditColor = slot;
    if (m_editColorBar) {
        m_editColorBar->setSelectedSlot(slot);
    }
}

void MainWindow::editEditColor(std::size_t slot)
{
    if (!m_document || slot >= m_document->colorCount()) {
        return;
    }
    const std::optional<uint16_t> picked = ColorPickerDialog::getColor(
        m_document->editColor(slot),
        this,
        QStringLiteral("Edit Color %1").arg(static_cast<int>(slot)));
    if (picked) {
        setEditColor(slot, *picked);
    }
}

void MainWindow::setEditColor(std::size_t slot, uint16_t rgb565)
{
    if (!m_document || slot >= m_document->colorCount()) {
        return;
    }
    commitEditorColors(edit::withEditColor(m_document->colorSetTables(), slot, rgb565),
                       QStringLiteral("Edit color %1 is 0x%2")
                           .arg(static_cast<int>(slot))
                           .arg(rgb565, 4, 16, QLatin1Char('0')));
}

void MainWindow::setDrawColorMode(edit::DrawColorMode mode)
{
    m_drawColorMode = mode;
    if (m_editColorBar) {
        m_editColorBar->setDrawColorMode(mode);
    }
    if (m_document) {
        const uint32_t stored = m_document->editorMetadata().drawColorMode;
        // A stored 2 (gradient) reads as single color and is kept until the mode changes.
        if (edit::drawColorModeFromStored(stored) != mode) {
            m_document->setDrawColorMode(edit::storedDrawColorMode(mode));
        }
    }
    statusBar()->showMessage(mode == edit::DrawColorMode::ColorSetReplace
                                 ? QStringLiteral("Drawing replaces each original shade by its "
                                                  "edit color")
                                 : QStringLiteral("Drawing with a single color"),
                             2000);
}

void MainWindow::commitEditorColors(document::ColorSetTables after, const QString& statusText)
{
    if (!m_document) {
        return;
    }
    try {
        std::unique_ptr<edit::EditCommand> command =
            edit::makeEditorColorsCommand(*m_document, std::move(after));
        if (!command) {
            return;
        }
        commitEdit(std::move(command));
        updateUndoRedoActions();
        statusBar()->showMessage(statusText, 2500);
    } catch (const std::exception& ex) {
        updateEditColorControls(false);
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::storeColorSet(std::size_t setIndex)
{
    if (!m_document) {
        return;
    }
    commitEditorColors(edit::withStoredColorSet(m_document->colorSetTables(),
                                                setIndex,
                                                m_document->colorCount()),
                       QStringLiteral("Stored the edit colors in color set %1")
                           .arg(static_cast<int>(setIndex)));
}

void MainWindow::loadColorSet(std::size_t setIndex)
{
    if (!m_document) {
        return;
    }
    commitEditorColors(edit::withLoadedColorSet(m_document->colorSetTables(),
                                                setIndex,
                                                m_document->colorCount()),
                       QStringLiteral("Loaded color set %1 into the edit colors")
                           .arg(static_cast<int>(setIndex)));
}

void MainWindow::deactivateColorSet(std::size_t setIndex)
{
    if (!m_document) {
        return;
    }
    commitEditorColors(
        edit::withColorSetActive(m_document->colorSetTables(), setIndex, false),
        QStringLiteral("Deactivated color set %1").arg(static_cast<int>(setIndex)));
}

void MainWindow::renameColorSet(std::size_t setIndex, const QString& name)
{
    if (!m_document) {
        return;
    }
    commitEditorColors(
        edit::withColorSetName(m_document->colorSetTables(), setIndex, name.toStdString()),
        QStringLiteral("Renamed color set %1").arg(static_cast<int>(setIndex)));
}

void MainWindow::copyEditColorsToDynamicSet()
{
    if (!m_document) {
        return;
    }
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    try {
        std::vector<std::unique_ptr<edit::EditCommand>> commands =
            edit::makeEditColorsToDynamicSetCommands(
                *m_document, frameIndices, dynamicPlane(), activeDynamicSet());
        const std::size_t changedFrames = commands.size();
        for (std::unique_ptr<edit::EditCommand>& command : commands) {
            commitEdit(std::move(command));
        }
        updateUndoRedoActions();
        statusBar()->showMessage(
            QStringLiteral("Copied the edit colors to dynamic set %1 of %2 frame(s)")
                .arg(static_cast<int>(activeDynamicSet()))
                .arg(static_cast<int>(changedFrames)),
            2500);
    } catch (const std::exception& ex) {
        updateDynamicColorControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::copyDynamicSetToEditColors()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }
    try {
        commitEditorColors(edit::withEditColorsFromDynamicSet(
                               *m_document, *frameIndex, dynamicPlane(), activeDynamicSet()),
                           QStringLiteral("Copied dynamic set %1 to the edit colors")
                               .arg(static_cast<int>(activeDynamicSet())));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::pickEditColorSlot(uint16_t rgb565)
{
    if (!m_document) {
        return;
    }
    const std::optional<std::size_t> slot = edit::matchingEditColorSlot(
        m_document->editorMetadata().editColors, m_document->colorCount(), rgb565);
    if (!slot) {
        statusBar()->showMessage(
            QStringLiteral("No edit color is 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')), 2500);
        return;
    }
    selectEditColorSlot(*slot);
    statusBar()->showMessage(
        QStringLiteral("Selected edit color %1 (0x%2)")
            .arg(static_cast<int>(*slot))
            .arg(rgb565, 4, 16, QLatin1Char('0')),
        2500);
}

void MainWindow::updateEditColorControls(bool projectChanged)
{
    const document::ProjectDocument* document = m_document ? &*m_document : nullptr;
    if (m_editColorBar) {
        m_editColorBar->refresh(document);
        m_editColorBar->setDrawColorMode(m_drawColorMode);
        m_editColorBar->setSelectedSlot(m_selectedEditColor);
    }
    if (m_colorSetPanel) {
        m_colorSetPanel->refresh(document, projectChanged);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_preview && event->type() == QEvent::KeyRelease) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_W && !keyEvent->isAutoRepeat()) {
            m_nonBlackKeyDown = false;
        }
    }
    if (watched == m_preview && event->type() == QEvent::KeyPress && m_document) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (m_pasteMode.handleKey(canvasEditContext(), keyEvent->key(), keyEvent->modifiers())) {
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape &&
            m_pendingCanvasClick != PendingCanvasClick::None) {
            armCanvasClick(PendingCanvasClick::None);
            statusBar()->showMessage(QStringLiteral("Cancelled"), 1500);
            return true;
        }
        if (keyEvent->key() == Qt::Key_W && !keyEvent->isAutoRepeat()) {
            m_nonBlackKeyDown = true;
        }
        const std::optional<std::size_t> slot = editColorSlotForKeyEvent(
            *keyEvent, m_document->colorCount(), currentNativeKeyCodes());
        if (slot) {
            selectEditColorSlot(*slot);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
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

void MainWindow::pushEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const edit::HistoryKey key = edit::historyKey(command->target());
    m_undoHistories.push(key, std::move(command));
    updateUndoRedoActions();
}

bool MainWindow::confirmDeletion(const QString& title,
                                 const QString& question,
                                 const QString& consequences)
{
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(title);
    confirm.setText(question);
    confirm.setInformativeText(consequences);
    QPushButton* deleteButton =
        confirm.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    confirm.setDefaultButton(confirm.addButton(QMessageBox::Cancel));
    confirm.exec();
    return confirm.clickedButton() == deleteButton;
}

void MainWindow::deleteSelectedFrames()
{
    deleteFrames(selectedFrameIndices(), QString());
}

void MainWindow::deleteSelectedFramesShorterThan()
{
    const std::vector<std::size_t> selected = selectedFrameIndices();
    if (!m_document || selected.empty()) {
        statusBar()->showMessage(QStringLiteral("No frame selected"), 2500);
        return;
    }
    bool accepted = false;
    const int durationMs = QInputDialog::getInt(
        this,
        QStringLiteral("Delete Short Frames"),
        QStringLiteral("Delete the selected frames that are shorter than (ms):"),
        static_cast<int>(legacy::kSkipFrameDurationMs),
        1,
        600000,
        1,
        &accepted);
    if (!accepted) {
        return;
    }
    const std::vector<std::size_t> shortFrames =
        edit::framesShorterThan(*m_document, selected, static_cast<uint32_t>(durationMs));
    if (shortFrames.empty()) {
        statusBar()->showMessage(
            QStringLiteral("None of the %1 selected frame(s) is shorter than %2 ms")
                .arg(static_cast<int>(selected.size()))
                .arg(durationMs),
            4000);
        return;
    }
    deleteFrames(shortFrames,
                 QStringLiteral("%1 of the %2 selected frame(s) are shorter than %3 ms.")
                     .arg(static_cast<int>(shortFrames.size()))
                     .arg(static_cast<int>(selected.size()))
                     .arg(durationMs));
}

void MainWindow::deleteFrames(const std::vector<std::size_t>& frameIndices, const QString& note)
{
    if (!m_document || frameIndices.empty()) {
        statusBar()->showMessage(QStringLiteral("No frame selected"), 2500);
        return;
    }
    const int count = static_cast<int>(frameIndices.size());
    const int total = static_cast<int>(m_document->frameCount());
    if (count >= total) {
        statusBar()->showMessage(
            QStringLiteral("Not deleted: all %1 frame(s) would go, and a project keeps at "
                           "least one frame")
                .arg(total),
            5000);
        return;
    }
    QStringList consequences;
    if (!note.isEmpty()) {
        consequences << note;
    }
    consequences << QStringLiteral("Frames: %1. %2 frame(s) remain. Sections and the source "
                                   "frames of sprites follow. The deletion can be undone.")
                        .arg(frameRangesText(frameIndices))
                        .arg(total - count);
    if (!confirmDeletion(QStringLiteral("Delete Frames"),
                         count == 1 ? QStringLiteral("Delete frame %1?")
                                          .arg(static_cast<qulonglong>(frameIndices.front()))
                                    : QStringLiteral("Delete %1 frames?").arg(count),
                         consequences.join(QChar(' ')))) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteFrames(*m_document, frameIndices));
        showStructureEditStatus(
            QStringLiteral("Deleted %1 frame(s), %2 remain").arg(count).arg(total - count),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::addSprite()
{
    addNamedSprite();
}

std::optional<std::size_t> MainWindow::addNamedSprite()
{
    if (!m_document) {
        return std::nullopt;
    }
    if (m_document->spriteCount() >= document::kMaxSprites) {
        statusBar()->showMessage(
            QStringLiteral("Not added: a project holds at most %1 sprites")
                .arg(static_cast<int>(document::kMaxSprites)),
            4000);
        return std::nullopt;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
                             this,
                             QStringLiteral("New Sprite"),
                             QStringLiteral("Name of the new sprite (up to 31 characters):"),
                             QLineEdit::Normal,
                             QString(),
                             &accepted)
                             .trimmed();
    if (!accepted) {
        return std::nullopt;
    }
    const std::string problem = edit::spriteNameProblem(*m_document, name.toStdString());
    if (!problem.empty()) {
        statusBar()->showMessage(
            QStringLiteral("Not added: %1").arg(QString::fromStdString(problem)), 4000);
        return std::nullopt;
    }
    try {
        // The cRP records the frame a sprite was made on, as the Win32 editor does.
        recordStructureEdit(
            edit::addSprite(*m_document, name.toStdString(), currentFrameIndex().value_or(0)));
        showStructureEditStatus(
            QStringLiteral("Added sprite %1 \"%2\", %3 sprite(s) now")
                .arg(static_cast<int>(m_document->spriteCount() - 1))
                .arg(name)
                .arg(static_cast<int>(m_document->spriteCount())),
            4000);
        return m_document->spriteCount() - 1;
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return std::nullopt;
    }
}

void MainWindow::deleteSelectedSprite()
{
    const std::optional<std::size_t> spriteIndex = currentSpriteIndex();
    if (!m_document || !spriteIndex) {
        statusBar()->showMessage(QStringLiteral("No sprite selected"), 2500);
        return;
    }
    const int useCount = static_cast<int>(m_document->spriteUseCount(*spriteIndex));
    const QString name = QString::fromStdString(m_document->sprite(*spriteIndex).name);
    if (!confirmDeletion(
            QStringLiteral("Delete Sprite"),
            name.isEmpty() ? QStringLiteral("Delete sprite %1?").arg(static_cast<int>(*spriteIndex))
                           : QStringLiteral("Delete sprite %1 \"%2\"?")
                                 .arg(static_cast<int>(*spriteIndex))
                                 .arg(name),
            QStringLiteral("%1 Sprites behind it get a number one lower. The deletion can be "
                           "undone.")
                .arg(useCount > 0 ? QStringLiteral("It is removed from the sprite slots of %1 "
                                                   "frame(s).")
                                        .arg(useCount)
                                  : QStringLiteral("No frame uses it.")))) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteSprite(*m_document, *spriteIndex));
        showStructureEditStatus(
            QStringLiteral("Deleted sprite %1%2; %3 sprite(s) remain")
                .arg(static_cast<int>(*spriteIndex))
                .arg(useCount > 0 ? QStringLiteral(", removed from %1 frame(s)").arg(useCount)
                                  : QString())
                .arg(static_cast<int>(m_document->spriteCount())),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::addBackgroundFromCurrentFrame()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        statusBar()->showMessage(QStringLiteral("Select a frame first"), 2500);
        return;
    }
    if (m_document->backgroundCount() >= document::kMaxBackgrounds) {
        statusBar()->showMessage(
            QStringLiteral("Not added: a project holds at most %1 backgrounds")
                .arg(static_cast<int>(document::kMaxBackgrounds)),
            4000);
        return;
    }
    if (isHdOnlyProject() && !m_document->hasHdFrame(*frameIndex)) {
        statusBar()->showMessage(
            QStringLiteral("Not added: frame %1 has no %2 frame to make a background of")
                .arg(static_cast<int>(*frameIndex))
                .arg(nativePlaneText(*m_document)),
            5000);
        return;
    }
    try {
        recordStructureEdit(edit::addBackgroundFromFrame(*m_document, *frameIndex));
        showStructureEditStatus(
            QStringLiteral("Added background %1 from frame %2, %3 background(s) now")
                .arg(static_cast<int>(m_document->backgroundCount() - 1))
                .arg(static_cast<int>(*frameIndex))
                .arg(static_cast<int>(m_document->backgroundCount())),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::deleteSelectedBackground()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !backgroundIndex) {
        statusBar()->showMessage(QStringLiteral("No background selected"), 2500);
        return;
    }
    const int useCount = static_cast<int>(m_document->backgroundUseCount(*backgroundIndex));
    if (!confirmDeletion(
            QStringLiteral("Delete Background"),
            QStringLiteral("Delete background %1?").arg(static_cast<int>(*backgroundIndex)),
            QStringLiteral("%1 Backgrounds behind it get a number one lower. The deletion can "
                           "be undone.")
                .arg(useCount > 0 ? QStringLiteral("%1 frame(s) use it and will have no "
                                                   "background.")
                                        .arg(useCount)
                                  : QStringLiteral("No frame uses it.")))) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteBackground(*m_document, *backgroundIndex));
        showStructureEditStatus(
            QStringLiteral("Deleted background %1%2; %3 background(s) remain")
                .arg(static_cast<int>(*backgroundIndex))
                .arg(useCount > 0 ? QStringLiteral(", %1 frame(s) have no background now")
                                        .arg(useCount)
                                  : QString())
                .arg(static_cast<int>(m_document->backgroundCount())),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::addComparisonMask()
{
    if (!m_document) {
        return;
    }
    if (m_document->comparisonMaskCount() >= document::kMaxComparisonMasks) {
        statusBar()->showMessage(
            QStringLiteral("Not added: a project holds at most %1 comparison masks")
                .arg(static_cast<int>(document::kMaxComparisonMasks)),
            4000);
        return;
    }
    try {
        recordStructureEdit(edit::addComparisonMask(*m_document));
        showStructureEditStatus(
            QStringLiteral("Added comparison mask %1, %2 mask(s) now; no frame uses it yet")
                .arg(static_cast<int>(m_document->comparisonMaskCount() - 1))
                .arg(static_cast<int>(m_document->comparisonMaskCount())),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::deleteSelectedComparisonMask()
{
    if (!m_document || !m_selectedMaskIndex ||
        *m_selectedMaskIndex >= m_document->comparisonMaskCount()) {
        statusBar()->showMessage(QStringLiteral("No comparison mask selected"), 2500);
        return;
    }
    const std::size_t maskIndex = *m_selectedMaskIndex;
    std::vector<std::size_t> users;
    for (std::size_t frameIndex = 0; frameIndex < m_document->frameCount(); ++frameIndex) {
        if (m_document->comparisonMaskId(frameIndex) == maskIndex) {
            users.push_back(frameIndex);
        }
    }
    // A mask in use decides how its frames are identified, so that case is spelled out.
    const QString consequences =
        users.empty()
            ? QStringLiteral("No frame uses it. Masks behind it get a number one lower. The "
                             "deletion can be undone.")
            : QStringLiteral("%1 frame(s) use it (%2). They will be compared without a mask, "
                             "which changes how the runtime identifies them. Masks behind it "
                             "get a number one lower. The deletion can be undone.")
                  .arg(static_cast<int>(users.size()))
                  .arg(frameRangesText(users));
    if (!confirmDeletion(
            QStringLiteral("Delete Comparison Mask"),
            QStringLiteral("Delete comparison mask %1?").arg(static_cast<int>(maskIndex)),
            consequences)) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteComparisonMask(*m_document, maskIndex));
        showStructureEditStatus(
            QStringLiteral("Deleted comparison mask %1%2; %3 mask(s) remain")
                .arg(static_cast<int>(maskIndex))
                .arg(users.empty() ? QString()
                                   : QStringLiteral(", %1 frame(s) have no mask now")
                                         .arg(static_cast<int>(users.size())))
                .arg(static_cast<int>(m_document->comparisonMaskCount())),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::recordStructureEdit(std::unique_ptr<edit::StructureCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const edit::StructureCommand& applied = *command;
    m_droppedStructureSteps = m_undoHistories.push(edit::structureHistoryKey(), std::move(command));
    refreshAfterStructureEdit(applied);
    updateUndoRedoActions();
}

void MainWindow::showStructureEditStatus(const QString& message, int timeoutMs)
{
    if (m_droppedStructureSteps == 0) {
        statusBar()->showMessage(message, timeoutMs);
        return;
    }
    // The deleted records of old steps no longer fit the undo memory; say so with the result.
    statusBar()->showMessage(
        QStringLiteral("%1 (undo keeps at most %2 MB of deleted records: the %3 oldest add or "
                       "delete steps can no longer be undone)")
            .arg(message)
            .arg(static_cast<qulonglong>(m_undoHistories.structureMemoryLimit() / (1024 * 1024)))
            .arg(static_cast<qulonglong>(m_droppedStructureSteps)),
        std::max(timeoutMs, 8000));
    m_droppedStructureSteps = 0;
}

void MainWindow::refreshAfterStructureEdit(const edit::StructureCommand& command)
{
    if (!m_document) {
        return;
    }
    setPlaybackRunning(false);
    m_pasteMode.cancel();
    m_canvasEdit.cancelFreeGradient();
    m_canvasEdit.clearSelection(canvasEditContext());
    // Everything that shows a sprite is refreshed below.
    m_spriteDependentViewsDirty = false;
    if (m_spritePanel) {
        m_spritePanel->setApplySpriteEditsEnabled(false);
    }

    const std::size_t first = command.recordIndices().front();
    const bool present = command.recordsPresent();
    // The added or restored record, or the one that took the place of a deleted one; the
    // last one if the deleted record was the last.
    const auto neighbour = [first](std::size_t count) -> std::optional<std::size_t> {
        return count == 0 ? std::nullopt : std::optional<std::size_t>(std::min(first, count - 1));
    };
    std::optional<std::size_t> selectedBackground;
    switch (command.recordKind()) {
        case edit::RecordKind::Frame:
            // The capture selection belongs to a frame number.
            m_canvasEdit.clearCaptureSelection();
            refreshFrameList(std::nullopt);
            selectFrameNear(*neighbour(m_document->frameCount()));
            if (present && m_frameList && command.recordIndices().size() > 1) {
                // All restored frames that are listed; told to the window once, below.
                QSignalBlocker blocker(m_frameList);
                for (int row = 0; row < m_frameList->count(); ++row) {
                    QListWidgetItem* item = m_frameList->item(row);
                    const std::size_t frameIndex = item->data(Qt::UserRole).toULongLong();
                    if (std::binary_search(command.recordIndices().begin(),
                                           command.recordIndices().end(),
                                           frameIndex)) {
                        item->setSelected(true);
                    }
                }
            }
            break;
        case edit::RecordKind::Sprite: {
            const std::optional<std::size_t> sprite = neighbour(m_document->spriteCount());
            m_selectedSpriteIndex = sprite ? static_cast<int>(*sprite) : -1;
            break;
        }
        case edit::RecordKind::Background:
            selectedBackground = neighbour(m_document->backgroundCount());
            break;
        case edit::RecordKind::ComparisonMask:
            m_selectedMaskIndex = neighbour(m_document->comparisonMaskCount());
            break;
    }
    if (command.recordKind() != edit::RecordKind::Frame) {
        refreshFrameListItems();
    }
    refreshComponentBrowsers();
    updateBackgroundControls();
    if (selectedBackground && m_backgroundComboBox) {
        const int comboIndex =
            m_backgroundComboBox->findData(static_cast<int>(*selectedBackground));
        if (comboIndex >= 0) {
            m_backgroundComboBox->setCurrentIndex(comboIndex);
        }
    }
    updateFrameMetadataControls();
    updateColorRotationControls();
    updateSpriteControls();
    updatePaintTargetMode();
    if (m_preview) {
        m_preview->update();
    }
    if (m_spriteEditor) {
        m_spriteEditor->update();
    }
    updateFrameStatusText();
    updatePlaybackControls();
    updateProjectHeaderStatus();
}

void MainWindow::refreshFrameListItems()
{
    if (!m_document || !m_frameList) {
        return;
    }
    int row = 0;
    bool sameFrames = true;
    for (std::size_t frameIndex = 0; sameFrames && frameIndex < m_document->frameCount();
         ++frameIndex) {
        if (!frameMatchesFilter(frameIndex)) {
            continue;
        }
        const QListWidgetItem* item = row < m_frameList->count() ? m_frameList->item(row) : nullptr;
        sameFrames = item && item->data(Qt::UserRole).toULongLong() == frameIndex;
        ++row;
    }
    if (!sameFrames || row != m_frameList->count()) {
        const std::optional<std::size_t> current = currentFrameIndex();
        refreshFrameList(std::nullopt);
        if (current) {
            selectFrameNear(*current);
        }
        return;
    }
    for (row = 0; row < m_frameList->count(); ++row) {
        QListWidgetItem* item = m_frameList->item(row);
        const std::size_t frameIndex = item->data(Qt::UserRole).toULongLong();
        item->setText(frameListItemText(frameIndex));
        item->setToolTip(frameListItemToolTip(frameIndex));
        m_frameThumbnails->invalidate(item);
    }
}

void MainWindow::selectFrameNear(std::size_t frameIndex)
{
    if (!m_frameList || m_frameList->count() == 0) {
        return;
    }
    // The rows are in frame order: the first row at or behind the frame.
    int low = 0;
    int high = m_frameList->count();
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (m_frameList->item(middle)->data(Qt::UserRole).toULongLong() < frameIndex) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    // Also when the row is current already: a multi-selection left from before is dropped.
    selectSingleRow(*m_frameList, std::min(low, m_frameList->count() - 1));
}

void MainWindow::updateStructureControls()
{
    const bool hasDocument = m_document.has_value();
    const bool canDeleteFrames =
        hasDocument && m_document->frameCount() > 1 && !selectedFrameIndices().empty();
    if (m_deleteFramesAction) {
        m_deleteFramesAction->setEnabled(canDeleteFrames);
    }
    if (m_deleteFramesListAction) {
        m_deleteFramesListAction->setEnabled(canDeleteFrames);
    }
    if (m_deleteShortFramesAction) {
        m_deleteShortFramesAction->setEnabled(canDeleteFrames);
    }
    RecordListPageState state;
    state.hasProject = hasDocument;
    if (m_maskPage) {
        state.recordCount = hasDocument ? m_document->comparisonMaskCount() : 0;
        state.maxRecordCount = document::kMaxComparisonMasks;
        state.hasSelection = hasDocument && m_selectedMaskIndex &&
                             *m_selectedMaskIndex < m_document->comparisonMaskCount();
        m_maskPage->refresh(state);
    }
    if (m_backgroundPage) {
        state.recordCount = hasDocument ? m_document->backgroundCount() : 0;
        state.maxRecordCount = document::kMaxBackgrounds;
        state.hasSource = currentFrameIndex().has_value();
        state.hasSelection = selectedBackgroundIndex().has_value();
        m_backgroundPage->refresh(state);
    }
    if (m_spritePage) {
        state.recordCount = hasDocument ? m_document->spriteCount() : 0;
        state.maxRecordCount = document::kMaxSprites;
        state.hasSource = true;
        state.hasSelection = currentSpriteIndex().has_value();
        m_spritePage->refresh(state);
    }
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
            // The thumbnail shows the zones, and the list's "Dyn" follows them.
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameListItem(frameIndex);
                updateInactiveFrameMarker(frameIndex);
            }
            [[fallthrough]];
        case edit::EditKind::ComparisonMask:
        case edit::EditKind::BackgroundMask:
            m_preview->update();
            updateProjectHeaderStatus();
            if (target.kind == edit::EditKind::ComparisonMask) {
                scheduleMaskIconUpdate(target.index);
                // What is the same as the current frame follows its mask.
                updateSameFrameControls();
            }
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
            // Sprite slots, a capture and color rotations are frame changes too.
            setSpriteDependentViewsDirty();
            updateSpriteControls();
            updateColorRotationControls();
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameListItem(frameIndex);
            }
            updateFrameStatusText();
            if (m_playbackTimer && m_playbackTimer->isActive()) {
                m_playbackTimer->setInterval(playbackIntervalForCurrentFrame());
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::EditorColors:
            updateEditColorControls(false);
            if (m_selectedEditColor) {
                selectEditColorSlot(*m_selectedEditColor);
            }
            updateProjectHeaderStatus();
            break;
        case edit::EditKind::ProjectStructure:
            if (const auto* structure = dynamic_cast<const edit::StructureCommand*>(&command)) {
                refreshAfterStructureEdit(*structure);
            }
            break;
        case edit::EditKind::Sections:
            // The rows of the frames that start a section carry its name.
            for (std::size_t frame = 0; frame < m_document->frameCount(); ++frame) {
                updateFrameListItemText(frame);
            }
            updateSectionControls();
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
    // Paste, auto copy and the helpers change the dynamic zones and sets with the pixels.
    if (const auto* content = dynamic_cast<const edit::FrameContentCommand*>(&command);
        content && content->changes().changesDynamicData()) {
        updateFrameListItem(content->frameIndex());
        updateInactiveFrameMarker(content->frameIndex());
        updateDynamicColorControls();
        updateDynamicShadowControls();
        m_preview->update();
    }
    if (m_runtimeView.isEnabled()) {
        m_preview->update();
    }
}

std::vector<edit::HistoryKey> MainWindow::currentHistoryKeys() const
{
    if (!m_document) {
        return {};
    }
    const render::Resolution sd = render::Resolution::Sd;
    // Adding and deleting records belongs to every context.
    std::vector<edit::HistoryKey> keys{edit::structureHistoryKey(), edit::sectionsHistoryKey()};
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
        keys.push_back(edit::editorColorsHistoryKey());
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
        // Sprite capture and detection areas are edited beside the frame.
        if (const std::optional<std::size_t> spriteIndex = currentSpriteIndex()) {
            keys.push_back({edit::EditKind::SpriteAttributes, *spriteIndex, sd});
        }
        return keys;
    }
    if (isDynamicMaskModeActive()) {
        if (frameIndex) {
            keys.push_back({edit::EditKind::DynamicMask, *frameIndex, dynamicPlane()});
            keys.push_back({edit::EditKind::DynamicColor, *frameIndex, dynamicPlane()});
            keys.push_back({edit::EditKind::DynamicShadow, *frameIndex, dynamicPlane()});
        }
        keys.push_back(edit::editorColorsHistoryKey());
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
    // The selected sprite's name and detection areas are edited beside the frame.
    if (const std::optional<std::size_t> spriteIndex = currentSpriteIndex()) {
        keys.push_back({edit::EditKind::SpriteAttributes, *spriteIndex, sd});
    }
    keys.push_back(edit::editorColorsHistoryKey());
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
        // The command could not run and stays where it was (UndoHistories leaves it on its
        // stack), and the document is unchanged.
        updateUndoRedoActions();
        statusBar()->showMessage(
            QStringLiteral("Undo failed, the step stays in the history: %1")
                .arg(QString::fromUtf8(ex.what())),
            6000);
        return;
    }
    if (!command) {
        return;
    }
    refreshAfterEdit(*command);
    updateUndoRedoActions();
    if (const auto* structure = dynamic_cast<const edit::StructureCommand*>(command)) {
        statusBar()->showMessage(
            QStringLiteral("Undo: %1").arg(structureCommandText(*structure)), 2500);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Undo") + editKindStatusSuffix(*command, showsHdAndSd()),
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
        // The command could not run and stays where it was (UndoHistories leaves it on its
        // stack), and the document is unchanged.
        updateUndoRedoActions();
        statusBar()->showMessage(
            QStringLiteral("Redo failed, the step stays in the history: %1")
                .arg(QString::fromUtf8(ex.what())),
            6000);
        return;
    }
    if (!command) {
        return;
    }
    refreshAfterEdit(*command);
    updateUndoRedoActions();
    if (const auto* structure = dynamic_cast<const edit::StructureCommand*>(command)) {
        statusBar()->showMessage(
            QStringLiteral("Redo: %1").arg(structureCommandText(*structure)), 2500);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Redo") + editKindStatusSuffix(*command, showsHdAndSd()),
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
    if (QListWidgetItem* item = frameListItem(frameIndex)) {
        m_frameThumbnails->invalidate(item);
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
    if (QListWidgetItem* item = frameListItem(frameIndex)) {
        item->setText(frameListItemText(frameIndex));
        item->setToolTip(frameListItemToolTip(frameIndex));
        return;
    }
    refreshFrameList(frameIndex);
}

void MainWindow::updateFrameListItems(const std::vector<std::size_t>& frameIndices)
{
    if (!m_document) {
        return;
    }
    for (std::size_t frameIndex : frameIndices) {
        if (frameIndex < m_document->frameCount() &&
            (!frameMatchesFilter(frameIndex) || !frameListItem(frameIndex))) {
            // The list changes: one rebuild instead of one per frame.
            refreshFrameList(currentFrameIndex());
            return;
        }
    }
    for (std::size_t frameIndex : frameIndices) {
        updateFrameListItem(frameIndex);
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
    if (QListWidgetItem* item = frameListItem(frameIndex)) {
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
