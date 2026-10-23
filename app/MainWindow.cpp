#include "MainWindow.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>

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
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QCursor>
#include <QAbstractSpinBox>
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
#include <QPainter>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollBar>
#include <QSplitter>
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

#include "AppInfo.h"
#include "Appearance.h"
#include "EditColorKeys.h"
#include "EditMenuActions.h"
#include "FramePreviewWidget.h"
#include "FrameImageUtils.h"
#include "FlowLayout.h"
#include "FrameListWidget.h"
#include "GifExport.h"
#include "HandbookWindow.h"
#include "FrameThumbnail.h"
#include "LazyListIcons.h"
#include "ThumbnailCaptions.h"
#include "EditorPlugins.h"
#include "KeyNames.h"
#include "MovieImport.h"
#include "PluginLogWindow.h"
#include "PupTesterWindow.h"
#include "VideoClip.h"
#include "vpx/VpxHost.h"
#include "ListSelection.h"
#include "PlaneLabels.h"
#include "SpriteEditorWidget.h"
#include "Translations.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/ColorPickerDialog.h"
#include "dialogs/SettingsDialog.h"
#include "dialogs/DumpImportDialog.h"
#include "dialogs/HelperLineDialog.h"
#include "dialogs/SceneDialog.h"
#include "ImageImport.h"
#include "dialogs/ImageImportDialog.h"
#include "dialogs/FrameTesterDialog.h"
#include "dialogs/ProjectTaskDialog.h"
#include "dialogs/SelectionDialogs.h"
#include "edit/EditBatch.h"
#include "edit/FrameContentCommand.h"
#include "edit/FrameTags.h"
#include "edit/ColorSetEdits.h"
#include "document/Upscale.h"
#include "edit/HelperLines.h"
#include "edit/SourceFrame.h"
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
#include "panels/HelperLinesPanel.h"
#include "panels/InspectorForm.h"
#include "dialogs/ProjectSettingsDialog.h"
#include "panels/SceneSettingsPanel.h"
#include "panels/ScenesPanel.h"
#include "panels/FrameLightsPanel.h"
#include "panels/LightEffectPanel.h"
#include "panels/LightEffectsPanel.h"
#include "SpeakerLightView.h"
#include "runtime/SpeakerLightPlayer.h"
#include "panels/SectionBar.h"
#include "panels/SpritePanel.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"
#include "platform/Utf8Path.h"
#include "runtime/CromcExport.h"
#include "edit/LicenseEdits.h"
#include "runtime/Scenes.h"
#include "legacy/LegacyProjectSerializer.h"
#include "WindowGeometry.h"

namespace colorizing::app {

namespace {

constexpr int kMaxRecentProjects = 8;
constexpr char kRecentProjectsSettingsKey[] = "recentProjects";
constexpr int kExportDmdPixelScale = 8;
constexpr QSize kFrameThumbnailSize{160, 84};
// A frame of the strip: the thumbnail, two lines of text below it, and room around.
constexpr QSize kFrameStripCellSize{176, 128};
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
    button->setToolTip(QCoreApplication::translate("MainWindow", "Draw color 0x%1")
                           .arg(rgb565, 4, 16, QLatin1Char('0')));
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

// The mask over an original frame of its size: the frame in gray, the pixels the mask hides
// tinted in the mask color, so that the mask that fits a frame is easy to find.
QImage renderMaskOverFrameImage(const document::IndexedFrame& mask,
                                const document::IndexedFrame& original,
                                uint32_t colorCount)
{
    QImage image(mask.size.width, mask.size.height, QImage::Format_ARGB32);
    const QColor tint = comparisonMaskColor();
    const int maxShade = std::max<int>(1, static_cast<int>(colorCount) - 1);
    for (int y = 0; y < mask.size.height; ++y) {
        for (int x = 0; x < mask.size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.size.width) +
                static_cast<std::size_t>(x);
            const int shade = std::min<int>(original.pixels[index], maxShade);
            const int gray = 30 + shade * 200 / maxShade;
            if (mask.pixels[index] == 0) {
                image.setPixelColor(x, y, QColor(gray, gray, gray));
            } else {
                // Two thirds mask color, one third the frame.
                image.setPixelColor(x, y,
                                    QColor((tint.red() * 2 + gray) / 3,
                                           (tint.green() * 2 + gray) / 3,
                                           (tint.blue() * 2 + gray) / 3));
            }
        }
    }
    return image;
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
                                                  : comparisonMaskColor());
        }
    }
    return image;
}

// What "inactive" on a frame means, see legacy::isFrameSkippedAtRuntime().
QString inactiveFrameToolTip()
{
    return QCoreApplication::translate(
        "MainWindow", "inactive: shorter than 15 ms and not colorized \u2014 the runtime skips it");
}

QPixmap previewPixmap(const QImage& image, QSize targetSize);

QIcon previewIcon(const QImage& image, QSize targetSize)
{
    return QIcon(previewPixmap(image, targetSize));
}

// A sprite or background thumbnail: the SD picture, with the frame list's HD badge when the
// record has HD data.
QIcon componentIcon(const QImage& image, QSize targetSize, bool hdBadge)
{
    QPixmap pixmap = previewPixmap(image, targetSize);
    if (hdBadge) {
        drawHdBadge(pixmap);
    }
    return QIcon(pixmap);
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

// The editor file this editor saves beside a .cROM (or .cRZ): "<name>.cRPX". The Win32
// editor's "<name>.cRP" is only read.
QString editorFileFor(const QString& cromPath)
{
    return siblingProjectFile(cromPath, QStringLiteral("cRPX"));
}

bool isCrpxPath(const QString& path)
{
    return path.endsWith(QStringLiteral(".cRPX"), Qt::CaseInsensitive);
}

// After a save: renames the Win32 editor's "<name>.cRP" beside the saved .cROM to
// "<name>.cRP.bak". The Win32 editor opens a .cROM with the .cRP of the same name, so it
// cannot open a project saved here any more. Returns the renamed file, or nothing.
QString retireWin32EditorFile(const QString& cromPath)
{
    const QString crpPath = siblingProjectFile(cromPath, QStringLiteral("cRP"));
    if (!QFileInfo(crpPath).isFile()) {
        return {};
    }
    const QString backupPath = crpPath + QStringLiteral(".bak");
    QFile::remove(backupPath);
    return QFile::rename(crpPath, backupPath) ? crpPath : QString();
}

// After a save: renames a "<name>.cROM" beside the saved .cRZ to "<name>.cROM.bak". The project
// is saved as its .cRZ only, so the .cROM holds an earlier state, which must not be opened by
// mistake. Returns the renamed file, or nothing.
QString retireLooseCrom(const QString& crzPath)
{
    const QString cromPath = siblingProjectFile(crzPath, QStringLiteral("cROM"));
    if (!QFileInfo(cromPath).isFile()) {
        return {};
    }
    const QString backupPath = cromPath + QStringLiteral(".bak");
    QFile::remove(backupPath);
    return QFile::rename(cromPath, backupPath) ? cromPath : QString();
}

QString editKindStatusSuffix(const edit::EditCommand& command, bool showsHdAndSd)
{
    switch (command.target().kind) {
        case edit::EditKind::FramePixels:
            break;
        case edit::EditKind::ComparisonMask:
            return QCoreApplication::translate("MainWindow", " mask edit");
        case edit::EditKind::DynamicMask:
            return QCoreApplication::translate("MainWindow", " dynamic mask edit");
        case edit::EditKind::DynamicColor:
            return QCoreApplication::translate("MainWindow", " dynamic color edit");
        case edit::EditKind::DynamicShadow:
            return QCoreApplication::translate("MainWindow", " dynamic shadow edit");
        case edit::EditKind::BackgroundMask:
            return QCoreApplication::translate("MainWindow", " background mask edit");
        case edit::EditKind::BackgroundImage:
            return QCoreApplication::translate("MainWindow", " background edit");
        case edit::EditKind::SpriteImage:
            return QCoreApplication::translate("MainWindow", " sprite edit");
        case edit::EditKind::Palette:
            return QCoreApplication::translate("MainWindow", " palette edit");
        case edit::EditKind::FrameAttributes:
            return QCoreApplication::translate("MainWindow", " frame change");
        case edit::EditKind::SpriteAttributes:
            return showsHdAndSd && dynamic_cast<const edit::SpriteHdCommand*>(&command)
                       ? QCoreApplication::translate("MainWindow", " sprite HD change")
                       : QCoreApplication::translate("MainWindow", " sprite change");
        case edit::EditKind::ProjectStructure:
            break;
        case edit::EditKind::EditorColors:
            return QCoreApplication::translate("MainWindow", " edit colors and color sets change");
        case edit::EditKind::Sections:
            return QCoreApplication::translate("MainWindow", " section change");
        case edit::EditKind::HelperLines:
            return QCoreApplication::translate("MainWindow", " helper line change");
    }
    return QString();
}

// "add scene 1001": what a scene edit does when it is applied.
QString sceneEditText(const edit::SceneEditCommand& command)
{
    switch (command.kind()) {
        case edit::SceneEditCommand::Kind::Scaling:
            return QCoreApplication::translate("MainWindow", "change the scaling settings");
        case edit::SceneEditCommand::Kind::LibserumTarget:
            return QCoreApplication::translate("MainWindow", "change the libserum target");
        case edit::SceneEditCommand::Kind::AddScene:
            return QCoreApplication::translate("MainWindow", "add scene %1").arg(command.sceneId());
        case edit::SceneEditCommand::Kind::ChangeScene:
            return QCoreApplication::translate("MainWindow", "change scene %1")
                .arg(command.sceneId());
        case edit::SceneEditCommand::Kind::RemoveScene:
            return QCoreApplication::translate("MainWindow", "delete scene %1")
                .arg(command.sceneId());
        case edit::SceneEditCommand::Kind::DeleteSceneFrames:
            return command.sceneId() != 0
                       ? QCoreApplication::translate(
                             "MainWindow", "delete frames of scene %1 and renumber the rest")
                             .arg(command.sceneId())
                       : QCoreApplication::translate(
                             "MainWindow", "delete frames of scenes and renumber the rest");
    }
    return QString();
}

// "delete 3 frame(s)", "add sprite 4": what a structure command does when it is applied.
QString structureCommandText(const edit::StructureCommand& command)
{
    const int index = static_cast<int>(command.recordIndices().front());
    const bool adds = command.addsRecords();
    switch (command.recordKind()) {
        case edit::RecordKind::Frame: {
            const int count = static_cast<int>(command.recordIndices().size());
            return adds ? QCoreApplication::translate("MainWindow", "add %1 frame(s)").arg(count)
                        : QCoreApplication::translate("MainWindow", "delete %1 frame(s)")
                              .arg(count);
        }
        case edit::RecordKind::Sprite:
            return adds ? QCoreApplication::translate("MainWindow", "add sprite %1").arg(index)
                        : QCoreApplication::translate("MainWindow", "delete sprite %1").arg(index);
        case edit::RecordKind::Background:
            return adds ? QCoreApplication::translate("MainWindow", "add background %1").arg(index)
                        : QCoreApplication::translate("MainWindow", "delete background %1")
                              .arg(index);
        case edit::RecordKind::ComparisonMask:
            return adds ? QCoreApplication::translate("MainWindow", "add comparison mask %1")
                              .arg(index)
                        : QCoreApplication::translate("MainWindow", "delete comparison mask %1")
                              .arg(index);
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
    QString text = QCoreApplication::translate("MainWindow", "%1 frame(s)").arg(changed);
    if (skipped > 0) {
        text += QCoreApplication::translate("MainWindow", ", %1 skipped").arg(skipped);
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
    setWindowTitle(kApplicationName);
    resize(1000, 700);
    m_packTimer.setInterval(100);
    connect(&m_packTimer, &QTimer::timeout, this, [this]() { packIdleBlocks(); });
    m_packTimer.start();
    m_frameRestTimer.setSingleShot(true);
    m_frameRestTimer.setInterval(250);
    connect(&m_frameRestTimer, &QTimer::timeout, this, [this]() {
        updateFrameRestViews();
        // The same-frames count of the status line, which the first question about a rule
        // costs a pass over the originals for; then it is kept.
        if (const std::optional<std::size_t> frameIndex = currentFrameIndex();
            m_document && frameIndex) {
            (void)m_sameFrameFinder.sameFrameCount(
                *m_document, *frameIndex, edit::comparisonRuleOf(*m_document, *frameIndex));
            updateFrameStatusText();
        }
        // A scene frame or not: the Inspector's sections follow.
        updateInspectorSections();
        noteCanvasPlace(true);
    });
    createActions();
    createCentralView();
    EditorPlugins::Window pluginWindow;
    pluginWindow.parent = this;
    pluginWindow.menu = m_pluginsMenu;
    pluginWindow.document = [this]() -> const document::ProjectDocument* {
        return m_document ? &*m_document : nullptr;
    };
    pluginWindow.editableDocument = [this]() -> document::ProjectDocument* {
        return m_document ? &*m_document : nullptr;
    };
    pluginWindow.recordEdit = [this](std::unique_ptr<edit::EditCommand> command) {
        recordPluginEdit(std::move(command));
    };
    pluginWindow.currentFrame = [this]() { return currentFrameIndex(); };
    pluginWindow.selectedFrames = [this]() { return selectedFrameIndices(); };
    pluginWindow.showFrames = [this](std::size_t current,
                                     const std::vector<std::size_t>& selected) {
        showFramesForPlugin(current, selected);
    };
    pluginWindow.showStatus = [this](const QString& text) {
        statusBar()->showMessage(text, 5000);
    };
    m_plugins = std::make_unique<EditorPlugins>(std::move(pluginWindow));
    // Where the mouse is on the canvas, to measure and to find a place again on another frame.
    m_positionLabel = new QLabel(this);
    m_positionLabel->setToolTip(
        tr("The pixel under the mouse on the canvas: on the original or the colorized frame, "
           "in that frame's pixels; while dragging also the size of the dragged area"));
    statusBar()->addPermanentWidget(m_positionLabel);
    m_timeLabel = new QLabel(this);
    m_timeLabel->setToolTip(
        tr("Time spent on the project (saved with it) and in this session, counted while you "
           "work on it: the clocks stop after a minute without input"));
    statusBar()->addPermanentWidget(m_timeLabel);
    m_clockTimer.setInterval(1000);
    connect(&m_clockTimer, &QTimer::timeout, this, &MainWindow::updateTimeLabel);
    // Every window's input counts as activity for the clocks (eventFilter()).
    QApplication::instance()->installEventFilter(this);
    m_preview->setHelperLineMoveHandler([this](std::size_t index, uint32_t position, bool finished) {
        if (!m_document || index >= m_document->helperLines().size()) {
            return;
        }
        const document::HelperLine& line = m_document->helperLines()[index];
        if (!finished) {
            statusBar()->showMessage(
                tr("Helper line %1: %2 %3").arg(QString::fromStdString(line.name))
                    .arg(line.vertical ? tr("column") : tr("row"))
                    .arg(position));
            return;
        }
        if (position == line.position) {
            statusBar()->clearMessage();
            return;
        }
        std::vector<document::HelperLine> lines = m_document->helperLines();
        lines[index].position = position;
        changeHelperLines(std::move(lines), tr("Moved the helper line"));
    });
    m_preview->setPositionHandler(
        [this](std::optional<render::CanvasHit> hit, std::optional<render::CanvasHit> dragStart) {
            showCanvasPosition(hit, dragStart);
        });
    if (const QStringList errors = m_plugins->loadEnabled(); !errors.isEmpty()) {
        statusBar()->showMessage(errors.join(QStringLiteral("; ")), 8000);
    }
    setStatusText(tr("No project loaded"));
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
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    // Help: the handbook, in a window of its own that stays open beside the editor.
    // The menu of the editor's plugins, shown while one has entries (EditorPlugins).
    m_pluginsMenu = menuBar()->addMenu(tr("&Plugins"));
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* handbookAction = helpMenu->addAction(tr("&Handbook"));
    handbookAction->setShortcut(QKeySequence::HelpContents);
    connect(handbookAction, &QAction::triggered, this, [this]() {
        if (!m_handbookWindow) {
            m_handbookWindow = new HandbookWindow(this);
        }
        m_handbookWindow->show();
        m_handbookWindow->raise();
        m_handbookWindow->activateWindow();
    });
    // On macOS it goes to the application menu.
    QAction* aboutAction = helpMenu->addAction(tr("&About %1").arg(kApplicationName));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        AboutDialog dialog(this);
        dialog.exec();
    });
    // What the VPX plugins (DMD output) log, to find out why a display stays dark.
    QAction* pluginLogAction = helpMenu->addAction(tr("&Plugin Log"));
    pluginLogAction->setToolTip(tr("What the DMD output and the other plugins log"));
    connect(pluginLogAction, &QAction::triggered, this, [this]() {
        if (!m_pluginLogWindow) {
            m_pluginLogWindow = new PluginLogWindow(this);
        }
        m_pluginLogWindow->show();
        m_pluginLogWindow->raise();
        m_pluginLogWindow->activateWindow();
    });

    QAction* newFromDumpAction =
        fileMenu->addAction(tr("&New Project From Dump..."));
    newFromDumpAction->setToolTip(tr("Create a project of the frames of a frame dump"));
    connect(newFromDumpAction, &QAction::triggered, this, [this]() {
        newProjectFromDump();
    });

    QAction* openAction = fileMenu->addAction(tr("&Open Project..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() {
        openProject();
    });

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, [this]() {
        saveProject();
    });

    QAction* saveAsAction = fileMenu->addAction(tr("Save &As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        saveProjectAs();
    });

    fileMenu->addSeparator();
    m_projectSettingsAction = fileMenu->addAction(tr("Pro&ject Settings..."));
    m_projectSettingsAction->setToolTip(
        tr("Settings of the whole colorization: the libserum it is made for, upscaling"));
    m_projectSettingsAction->setMenuRole(QAction::NoRole);
    connect(m_projectSettingsAction, &QAction::triggered, this, [this]() {
        openProjectSettings();
    });
    fileMenu->addSeparator();

    QAction* exportCromcAction = fileMenu->addAction(tr("Export .cRO&Mc..."));
    exportCromcAction->setToolTip(
        tr("Write the project's .cROMc as libserum generates it on a machine"));
    connect(exportCromcAction, &QAction::triggered, this, [this]() {
        exportCromc();
    });

    // The GIF export, one entry per layout. Shift held on the first adds the originals, as the
    // Win32 editor's Shift+click on its GIF button did.
    QMenu* gifMenu = fileMenu->addMenu(tr("Export Animated &GIF"));
    gifMenu->setToolTipsVisible(true);
    QAction* exportGifAction = gifMenu->addAction(tr("&Colorized Frames..."));
    exportGifAction->setToolTip(tr(
        "Write the selected frames, with their colour rotations, as an animated GIF, on the "
        "plane the canvas shows"));
    connect(exportGifAction, &QAction::triggered, this, [this]() {
        exportAnimatedGif(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier),
                          false);
    });
    QAction* exportGifOriginalsAction = gifMenu->addAction(tr("With &Originals..."));
    exportGifOriginalsAction->setToolTip(
        tr("Each original frame above its colorized picture"));
    connect(exportGifOriginalsAction, &QAction::triggered, this,
            [this]() { exportAnimatedGif(true, false); });
    m_exportGifBothPlanesAction = gifMenu->addAction(tr("&Both Planes..."));
    m_exportGifBothPlanesAction->setToolTip(
        tr("The 128x32 picture above the 256x64 one, as the Win32 editor's GIF"));
    connect(m_exportGifBothPlanesAction, &QAction::triggered, this,
            [this]() { exportAnimatedGif(false, true); });
    m_exportGifAllAction = gifMenu->addAction(tr("Originals And Both &Planes..."));
    m_exportGifAllAction->setToolTip(
        tr("The original frame, the 128x32 picture and the 256x64 picture, one above the "
           "other"));
    connect(m_exportGifAllAction, &QAction::triggered, this,
            [this]() { exportAnimatedGif(true, true); });
    // Both planes only in a project that has them.
    connect(gifMenu, &QMenu::aboutToShow, this, [this]() {
        const bool twoPlanes = m_document &&
                               m_document->geometry().layout() == render::PlaneLayout::Ordinary &&
                               !m_document->editsOriginalPlaneOnly();
        m_exportGifBothPlanesAction->setEnabled(twoPlanes);
        m_exportGifAllAction->setEnabled(twoPlanes);
    });

    QAction* exportFrameAction = fileMenu->addAction(tr("Export Current &Frame PNG..."));
    exportFrameAction->setToolTip(tr("Export the current frame as libserum renders it"));
    connect(exportFrameAction, &QAction::triggered, this, [this]() {
        exportCurrentFramePng();
    });

    QAction* importFrameImageAction =
        fileMenu->addAction(tr("Import Image Into Selected &Frames..."));
    connect(importFrameImageAction, &QAction::triggered, this, [this]() {
        importImageIntoSelectedFrames();
    });
    QAction* importClipboardAction =
        fileMenu->addAction(tr("Import Image From the C&lipboard..."));
    importClipboardAction->setToolTip(
        tr("The picture on the clipboard into the selection of the selected frames"));
    connect(importClipboardAction, &QAction::triggered, this, [this]() {
        importImageFromClipboard();
    });
    QAction* importMovieAction =
        fileMenu->addAction(tr("Import Mo&vie Clip Into Selected Frames..."));
    importMovieAction->setToolTip(
        tr("A picture of a movie clip for each selected frame, from a part of the clip's "
           "picture"));
    connect(importMovieAction, &QAction::triggered, this, [this]() {
        importMovieIntoSelectedFrames();
    });

    m_addFramesFromDumpAction = fileMenu->addAction(tr("A&dd Frames From Dump..."));
    m_addFramesFromDumpAction->setToolTip(
        tr("Add the frames of a frame dump that the project does not have yet"));
    m_addFramesFromDumpAction->setEnabled(false);
    connect(m_addFramesFromDumpAction, &QAction::triggered, this, [this]() {
        addFramesFromDump();
    });

    m_recentProjectsMenu = fileMenu->addMenu(tr("Open &Recent"));
    refreshRecentProjectsMenu();

    fileMenu->addSeparator();
    // On macOS Qt moves it to the application menu, by its role.
    QAction* settingsAction = fileMenu->addAction(tr("Se&ttings..."));
    settingsAction->setShortcut(QKeySequence::Preferences);
    settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(settingsAction, &QAction::triggered, this,
            [this]() { editSettings(SettingsDialog::Page::Appearance); });
    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = editMenu->addAction(tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        undo();
    });

    m_redoAction = editMenu->addAction(tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        redo();
    });

    // Undo and Redo above keep to the frame (or record) shown; these go through the whole
    // project, most recent first, and show the frame they change.
    m_undoAnywhereAction = editMenu->addAction(tr("Undo Last Edit &Anywhere"));
    m_undoAnywhereAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z));
    m_undoAnywhereAction->setShortcutContext(Qt::ApplicationShortcut);
    m_undoAnywhereAction->setToolTip(
        tr("Undo the most recent edit of the project, on whichever frame or record it was "
           "made, and go to that frame"));
    m_undoAnywhereAction->setEnabled(false);
    connect(m_undoAnywhereAction, &QAction::triggered, this, [this]() { undo(true); });
    m_redoAnywhereAction = editMenu->addAction(tr("Redo Last Edit A&nywhere"));
    m_redoAnywhereAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_Z));
    m_redoAnywhereAction->setShortcutContext(Qt::ApplicationShortcut);
    m_redoAnywhereAction->setToolTip(
        tr("Redo the edit undone most recently anywhere in the project, and go to its frame"));
    m_redoAnywhereAction->setEnabled(false);
    connect(m_redoAnywhereAction, &QAction::triggered, this, [this]() { redo(true); });

    editMenu->addSeparator();
    m_deleteFramesAction = editMenu->addAction(tr("&Delete Frames..."));
    m_deleteFramesAction->setToolTip(tr("Delete the selected frames"));
    m_deleteFramesAction->setEnabled(false);
    connect(m_deleteFramesAction, &QAction::triggered, this, [this]() {
        deleteSelectedFrames();
    });
    // The Win32 editor's "To Source Frame": a fake frame no dump has, painted in the original
    // colors, added behind the last frame.
    m_sourceFrameAction = editMenu->addAction(tr("New &Frame From Original Colors"));
    m_sourceFrameAction->setToolTip(
        tr("Add a frame whose original is the current frame's picture, painted in the plain "
           "original colors only (\"To Source Frame\" in the Win32 editor)"));
    m_sourceFrameAction->setEnabled(false);
    connect(m_sourceFrameAction, &QAction::triggered, this, [this]() { addSourceFrame(); });
    m_deleteShortFramesAction =
        editMenu->addAction(tr("Delete Frames &Shorter Than..."));
    m_deleteShortFramesAction->setToolTip(
        tr("Delete the selected frames that are shorter than a duration"));
    m_deleteShortFramesAction->setEnabled(false);
    connect(m_deleteShortFramesAction, &QAction::triggered, this, [this]() {
        deleteSelectedFramesShorterThan();
    });
    m_moveFramesUpAction = editMenu->addAction(tr("Move Frames &Earlier"));
    m_moveFramesUpAction->setToolTip(
        tr("Move the selected frames before the frame shown before them (%1 in the frame "
           "strip)")
            .arg(nativeShortcut(QStringLiteral("Alt+Left"))));
    m_moveFramesUpAction->setEnabled(false);
    connect(m_moveFramesUpAction, &QAction::triggered, this, [this]() { moveSelectedFramesUp(); });
    m_moveFramesDownAction = editMenu->addAction(tr("Move Frames &Later"));
    m_moveFramesDownAction->setToolTip(
        tr("Move the selected frames behind the frame shown after them (%1 in the frame "
           "strip)")
            .arg(nativeShortcut(QStringLiteral("Alt+Right"))));
    m_moveFramesDownAction->setEnabled(false);
    connect(m_moveFramesDownAction, &QAction::triggered, this,
            [this]() { moveSelectedFramesDown(); });
    m_moveFramesToAction = editMenu->addAction(tr("&Move Frames To..."));
    m_moveFramesToAction->setToolTip(
        tr("Move the selected frames before a frame number, or to the end"));
    m_moveFramesToAction->setEnabled(false);
    connect(m_moveFramesToAction, &QAction::triggered, this, [this]() { moveSelectedFramesTo(); });
    editMenu->addSeparator();
    m_ambientLightingAction =
        editMenu->addAction(tr("Ambient Lighting For Frames Without Speaker Lights..."));
    m_ambientLightingAction->setToolTip(
        tr("Speaker lights 1 and 2 from the colors of the left and right part of every frame "
           "that does nothing to the speaker lights yet"));
    m_ambientLightingAction->setEnabled(false);
    connect(m_ambientLightingAction, &QAction::triggered, this,
            [this]() { addAmbientLightingToFramesWithoutLights(); });
    m_updateAmbientLightingAction = editMenu->addAction(tr("Update Ambient Lighting"));
    m_updateAmbientLightingAction->setToolTip(
        tr("Set every speaker light the ambient lighting chose to what the frame's colors give "
           "now; lights you chose yourself stay"));
    m_updateAmbientLightingAction->setEnabled(false);
    connect(m_updateAmbientLightingAction, &QAction::triggered, this,
            [this]() { updateAmbientLighting(); });
    m_frameLightMarksTimer.setSingleShot(true);
    m_frameLightMarksTimer.setInterval(150);
    connect(&m_frameLightMarksTimer, &QTimer::timeout, this,
            [this]() { updateFrameLightControls(); });
    createSelectionActions(editMenu);

    QAction* frameTesterAction = viewMenu->addAction(tr("Frame &Tester..."));
    frameTesterAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    frameTesterAction->setToolTip(tr(
        "Feed the selected frames or a frame dump to libserum with the whole project loaded"));
    connect(frameTesterAction, &QAction::triggered, this, [this]() { openFrameTester(); });
    QAction* pupTesterAction = viewMenu->addAction(tr("&PuP Tester..."));
    pupTesterAction->setToolTip(
        tr("Play the project's PuP pack with the PUP plugin: the shown frames' triggers go to "
           "it as on a machine"));
    connect(pupTesterAction, &QAction::triggered, this, [this]() {
        if (!m_pupTesterWindow) {
            m_pupTesterWindow = new PupTesterWindow(this);
            m_pupTesterWindow->setProject(m_document ? &*m_document : nullptr);
        }
        m_pupTesterWindow->show();
        m_pupTesterWindow->raise();
        m_pupTesterWindow->activateWindow();
    });
    m_speakerPreviewAction = viewMenu->addAction(tr("&Speaker Light Preview"));
    m_speakerPreviewAction->setCheckable(true);
    m_speakerPreviewAction->setChecked(loadSpeakerLightPreviewOptions().visible);
    m_speakerPreviewAction->setToolTip(
        tr("Show the speaker lights beside the canvas and in the frame tester; their shape, "
           "LEDs and channels are in Settings -> Appearance"));
    connect(m_speakerPreviewAction, &QAction::toggled, this,
            [this](bool checked) { setSpeakerLightPreviewShown(checked); });
    viewMenu->addSeparator();

    m_dmdOutputAction = viewMenu->addAction(tr("Output To &DMD"));
    m_dmdOutputAction->setCheckable(true);
    m_dmdOutputAction->setIconText(QStringLiteral("DMD"));
    m_dmdOutputAction->setEnabled(DmdOutput::isAvailable());
    m_dmdOutputAction->setToolTip(
        DmdOutput::isAvailable()
            ? tr("Show the canvas picture on a ZeDMD, Pixelcade, PIN2DMD or dmdserver")
            : tr("No DMD output: this build has no DMDUtil plugin"));
    connect(m_dmdOutputAction, &QAction::toggled, this, [this](bool on) {
        setDmdOutputEnabled(on);
    });
    QAction* dmdSettingsAction = viewMenu->addAction(tr("DMD Output &Settings..."));
    dmdSettingsAction->setEnabled(DmdOutput::isAvailable());
    connect(dmdSettingsAction, &QAction::triggered, this,
            [this]() { editSettings(SettingsDialog::Page::DmdOutput); });
    viewMenu->addSeparator();
    // The canvas zoom; the colorized and the original picture show the same part. While a
    // sprite is shown in the canvas's place, its zoom.
    QAction* zoomInAction = viewMenu->addAction(tr("Zoom &In"));
    zoomInAction->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(zoomInAction, &QAction::triggered, this, [this]() {
        if (isSpriteCanvasActive() && m_spriteEditor) {
            m_spriteEditor->zoomIn();
        } else if (m_preview) {
            m_preview->zoomIn();
        }
    });
    QAction* zoomOutAction = viewMenu->addAction(tr("Zoom &Out"));
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, [this]() {
        if (isSpriteCanvasActive() && m_spriteEditor) {
            m_spriteEditor->zoomOut();
        } else if (m_preview) {
            m_preview->zoomOut();
        }
    });
    QAction* zoomFitAction = viewMenu->addAction(tr("Show &Whole Frame"));
    zoomFitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomFitAction, &QAction::triggered, this, [this]() {
        if (isSpriteCanvasActive() && m_spriteEditor) {
            m_spriteEditor->resetZoom();
        } else if (m_preview) {
            m_preview->resetZoom();
        }
    });
    viewMenu->addSeparator();

    m_helperLinesShownAction = viewMenu->addAction(tr("Show &Helper Lines"));
    m_helperLinesShownAction->setCheckable(true);
    m_helperLinesShownAction->setChecked(
        QSettings().value(QStringLiteral("view/helperLinesShown"), true).toBool());
    m_helperLinesShownAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    m_helperLinesShownAction->setToolTip(
        tr("Show the checked helper lines on the canvas, or hide them all"));
    connect(m_helperLinesShownAction, &QAction::toggled, this, [this](bool shown) {
        QSettings().setValue(QStringLiteral("view/helperLinesShown"), shown);
        if (m_preview) {
            m_preview->setHelperLinesShown(shown);
        }
        if (m_helperLinesPanel) {
            m_helperLinesPanel->setLinesShown(shown);
        }
    });
    applyHelperLinesShown();

    m_runtimeViewAction = viewMenu->addAction(tr("&Runtime View"));
    // What the toolbar button of the action shows.
    m_runtimeViewAction->setIconText(tr("Runtime"));
    m_runtimeViewAction->setCheckable(true);
    m_runtimeViewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_runtimeViewAction->setShortcutContext(Qt::ApplicationShortcut);
    m_runtimeViewAction->setToolTip(
        tr("Show the current frame as libserum renders it; editing is off meanwhile"));
    connect(m_runtimeViewAction, &QAction::toggled, this, [this](bool checked) {
        setRuntimeViewEnabled(checked);
    });

    auto* rowsGroup = new QActionGroup(this);
    viewMenu->addSeparator();
    m_runtimeRows32Action = viewMenu->addAction(tr("Runtime Plane: &32 Rows"));
    m_runtimeRows32Action->setCheckable(true);
    m_runtimeRows32Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    m_runtimeRows32Action->setShortcutContext(Qt::ApplicationShortcut);
    m_runtimeRows32Action->setToolTip(tr("What a display with 32 rows gets"));
    m_runtimeRows64Action = viewMenu->addAction(tr("Runtime Plane: &64 Rows"));
    m_runtimeRows64Action->setCheckable(true);
    m_runtimeRows64Action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_6));
    m_runtimeRows64Action->setShortcutContext(Qt::ApplicationShortcut);
    m_runtimeRows64Action->setToolTip(tr("What a display with 64 rows gets"));
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
    m_frameFilterComboBox->addItem(tr("All frames"),
                                   static_cast<int>(FrameFilter::All));
    m_frameFilterComboBox->addItem(tr("HD frames"),
                                   static_cast<int>(FrameFilter::Hd));
    m_frameFilterComboBox->addItem(tr("With background"),
                                   static_cast<int>(FrameFilter::Background));
    m_frameFilterComboBox->addItem(tr("With dynamic"),
                                   static_cast<int>(FrameFilter::Dynamic));
    m_frameFilterComboBox->addItem(tr("With sprites"),
                                   static_cast<int>(FrameFilter::Sprites));
    m_frameFilterComboBox->addItem(tr("With rotation"),
                                   static_cast<int>(FrameFilter::Rotations));
    m_frameFilterComboBox->addItem(tr("With triggers"),
                                   static_cast<int>(FrameFilter::Triggers));
    m_frameFilterComboBox->setItemData(
        m_frameFilterComboBox->count() - 1,
        tr("The frames with a trigger: a PUP trigger, a scene or the monochrome fallback"),
        Qt::ToolTipRole);
    m_frameFilterComboBox->addItem(tr("Inactive"),
                                   static_cast<int>(FrameFilter::Inactive));
    m_frameFilterComboBox->setItemData(m_frameFilterComboBox->count() - 1,
                                       inactiveFrameToolTip(),
                                       Qt::ToolTipRole);
    m_frameFilterComboBox->addItem(tr("Using the selected asset"),
                                   static_cast<int>(FrameFilter::UsesSelectedAsset));
    m_frameFilterComboBox->setItemData(
        m_frameFilterComboBox->count() - 1,
        tr("The frames that use the mask, background or sprite selected in the section used "
           "last on the left, that start the selected scene (also as background) or the "
           "selected light effect; follows the selection and the section"),
        Qt::ToolTipRole);
    m_frameFilterComboBox->setEnabled(false);
    m_frameList = new FrameListWidget(central);
    m_frameList->onMoveRequested = [this](int row) { moveSelectedFramesBeforeRow(row); };
    m_frameList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frameList->setIconSize(kFrameThumbnailSize);
    // One row of frames, left to right, each with its text below the thumbnail; frames are
    // dragged to another place.
    m_frameList->useStripLayout(kFrameStripCellSize);
    // One row of frames high.
    m_frameList->setMinimumHeight(kFrameStripCellSize.height() +
                                  m_frameList->horizontalScrollBar()->sizeHint().height() +
                                  2 * m_frameList->frameWidth() + 4);
    m_frameList->setMaximumHeight(m_frameList->minimumHeight());
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
    // Alt+Left and Alt+Right (also Alt+Up and Alt+Down) in the strip only, as Delete above.
    auto* moveUpListAction = new QAction(m_moveFramesUpAction->text(), m_frameList);
    moveUpListAction->setShortcuts(
        {QKeySequence(Qt::ALT | Qt::Key_Left), QKeySequence(Qt::ALT | Qt::Key_Up)});
    moveUpListAction->setShortcutContext(Qt::WidgetShortcut);
    connect(moveUpListAction, &QAction::triggered, this, [this]() { moveSelectedFramesUp(); });
    auto* moveDownListAction = new QAction(m_moveFramesDownAction->text(), m_frameList);
    moveDownListAction->setShortcuts(
        {QKeySequence(Qt::ALT | Qt::Key_Right), QKeySequence(Qt::ALT | Qt::Key_Down)});
    moveDownListAction->setShortcutContext(Qt::WidgetShortcut);
    connect(moveDownListAction, &QAction::triggered, this,
            [this]() { moveSelectedFramesDown(); });
    m_frameList->addAction(moveUpListAction);
    m_frameList->addAction(moveDownListAction);
    m_frameList->addAction(m_moveFramesToAction);
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
    // Masks and backgrounds can be dragged onto the canvas, see the canvas's drop handler.
    m_maskList->setDragEnabled(true);
    m_maskList->setDragDropMode(QAbstractItemView::DragOnly);
    m_maskList->setDefaultDropAction(Qt::CopyAction);
    m_backgroundList = new QListWidget(central);
    m_backgroundList->setDragEnabled(true);
    m_backgroundList->setDragDropMode(QAbstractItemView::DragOnly);
    m_backgroundList->setDefaultDropAction(Qt::CopyAction);
    m_backgroundList->setIconSize(QSize(160, 42));
    m_backgroundList->setResizeMode(QListView::Adjust);
    m_backgroundList->setUniformItemSizes(false);
    m_backgroundList->setMinimumWidth(230);
    m_spriteList = new QListWidget(central);
    m_spriteList->setIconSize(QSize(160, 42));
    m_spriteList->setResizeMode(QListView::Adjust);
    m_spriteList->setUniformItemSizes(false);
    m_spriteList->setMinimumWidth(230);
    // The caption below each thumbnail, as in the frame strip.
    for (QListWidget* list : {m_maskList, m_backgroundList, m_spriteList}) {
        showCaptionsBelowThumbnails(list);
    }
    m_preview = new FramePreviewWidget(central);
    m_preview->setRuntimeView(&m_runtimeView);
    m_spriteEditor = new SpriteEditorWidget(central);
    m_spriteEditor->setColorSetReplace(m_drawColorMode == edit::DrawColorMode::ColorSetReplace);
    m_canvasStack = new QStackedWidget(central);
    m_canvasStack->addWidget(m_preview);
    m_canvasStack->addWidget(m_spriteEditor);

    layout->addWidget(m_statusLabel);

    // The row above the canvas: groups of controls that wrap onto another row when the window
    // is too narrow for them, each group kept together; the frame status takes the rest.
    auto* toolbar = new QWidget(central);
    auto* toolbarFlow = new FlowLayout(toolbar, 12, 4);
    QHBoxLayout* toolbarLayout = nullptr;
    const auto startToolbarGroup = [&toolbar, &toolbarFlow, &toolbarLayout]() {
        auto* group = new QWidget(toolbar);
        toolbarLayout = new QHBoxLayout(group);
        toolbarLayout->setContentsMargins(0, 0, 0, 0);
        toolbarFlow->addWidget(group);
    };
    startToolbarGroup();
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
    m_comparisonMaskOverlayButton->setText(tr("Mask"));
    m_comparisonMaskOverlayButton->setToolTip(tr("Show and edit comparison mask"));
    m_comparisonMaskOverlayButton->setCheckable(true);
    m_comparisonMaskOverlayButton->setEnabled(false);
    m_dynamicMaskOverlayButton = new QToolButton(central);
    m_dynamicMaskOverlayButton->setText(tr("Dynamic"));
    m_dynamicMaskOverlayButton->setToolTip(tr("Show dynamic mask overlay"));
    m_dynamicMaskOverlayButton->setCheckable(true);
    m_dynamicMaskOverlayButton->setEnabled(false);
    m_backgroundMaskOverlayButton = new QToolButton(central);
    m_backgroundMaskOverlayButton->setText(tr("Background"));
    m_backgroundMaskOverlayButton->setToolTip(tr("Show and edit background mask"));
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
    // Which pixels Spot Common marks is the author's choice for every project.
    m_sameFramesPanel->setSpotMarks(
        QSettings().value(QStringLiteral("sameFrames/spotCommonMarksCommon"), false).toBool()
            ? SpotMarks::Common
            : SpotMarks::Differing);
    m_sameFramesPanel->onSpotMarksChanged = [this](SpotMarks marks) {
        QSettings().setValue(QStringLiteral("sameFrames/spotCommonMarksCommon"),
                             marks == SpotMarks::Common);
        updateSpotCommonOverlay();
    };
    m_sameFramesPanel->onCommonToMaskRequested = [this]() { writeCommonPixelsToMask(); };
    m_sameFramesPanel->onCommonToNewMaskRequested = [this]() { writeCommonPixelsToNewMask(); };
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
    m_framePanel->onBackgroundIdEdited = [this](int backgroundId) {
        setCurrentFrameBackgroundId(backgroundId);
    };
    m_framePanel->onTriggerEdited = [this](document::TriggerKind kind, uint32_t triggerId) {
        setSelectedFramesTrigger(kind, triggerId);
    };
    m_framePanel->onDurationEdited = [this](int durationMs) {
        setCurrentFrameDurationMs(durationMs);
    };
    m_framePanel->onTagsEdited = [this](const QString& text) { setSelectedFramesTags(text); };
    m_framePanel->onBackgroundLayerToggled = [this](bool checked) {
        if (m_preview) {
            m_preview->setBackgroundLayerEnabled(checked);
        }
        sendDmdPicture();
    };
    m_framePanel->onSpritesLayerToggled = [this](bool checked) {
        if (m_preview) {
            m_preview->setSpritesLayerEnabled(checked);
        }
        sendDmdPicture();
    };
    m_framePanel->onImportImageRequested = [this]() {
        importImageIntoSelectedFrames();
    };
    m_framePanel->onCreateHdRequested = [this]() { createCurrentFrameHd(); };
    m_framePanel->onRemoveHdRequested = [this]() { removeCurrentFrameHd(); };
    m_backgroundComboBox = new QComboBox(central);
    m_backgroundComboBox->setToolTip(tr("Loaded background"));
    m_backgroundComboBox->setEnabled(false);
    m_backgroundComboBox->setVisible(false);
    m_backgroundPanel = new BackgroundPanel(central);
    m_backgroundPanel->onAssignRequested = [this]() {
        assignSelectedBackgroundToFrames();
    };
    m_backgroundPanel->onRemoveRequested = [this]() {
        setCurrentFrameBackgroundId(-1);
        updateBackgroundControls();
    };
    m_backgroundPanel->onEditRequested = [this](bool editing) {
        if (editing) {
            editAssignedBackground(true);
            return;
        }
        // Back to the frame, as Edit Sprite does when it is clicked again.
        editFrameInsteadOfBackground();
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
    m_backgroundPanel->onCreateHdRequested = [this]() { createSelectedBackgroundHd(); };
    m_backgroundPanel->onRemoveHdRequested = [this]() { removeSelectedBackgroundHd(); };
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
    m_spritePanel->onOutlinesToggled = [this](bool checked) {
        if (m_preview) {
            m_preview->setSpriteOutlinesEnabled(checked);
        }
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
    m_spritePanel->onShapeModeToggled = [this](bool shapeMode) {
        const std::optional<std::size_t> sprite = currentSpriteIndex();
        if (!m_document || !sprite) {
            return;
        }
        edit::SpriteState before = edit::spriteState(*m_document, *sprite);
        m_document->setSpriteShapeMode(*sprite, shapeMode);
        recordEdit(edit::spriteStateCommand(*m_document, *sprite, std::move(before)));
        statusBar()->showMessage(shapeMode ? tr("Sprite %1 is found by its shape")
                                                 .arg(static_cast<int>(*sprite))
                                           : tr("Sprite %1 is found by its shades")
                                                 .arg(static_cast<int>(*sprite)),
                                 2500);
    };
    m_spritePanel->onDynamicPaintToggled = [this](bool enabled) {
        if (m_spriteEditor) {
            m_spriteEditor->setDynamicPaintMode(enabled);
        }
        if (enabled) {
            statusBar()->showMessage(
                tr("Paint dynamic zones on the sprite canvas (Edit Sprite), on the "
                   "%1 sprite")
                    .arg(m_document && m_document->dynamicPlane() == render::Resolution::Hd
                             ? nativePlaneText(*m_document)
                             : QStringLiteral("128x32")),
                4000);
        }
    };
    m_spritePanel->onDynamicColorClicked = [this](std::size_t shade) {
        const std::optional<std::size_t> sprite = currentSpriteIndex();
        if (!m_document || !sprite) {
            return;
        }
        const std::size_t set = m_spritePanel->dynamicSet();
        const std::optional<uint16_t> picked = pickColor(
            m_document->spriteDynamicColor(*sprite, set, shade), tr("Sprite Dynamic Color"));
        if (!picked) {
            return;
        }
        edit::SpriteState before = edit::spriteState(*m_document, *sprite);
        m_document->setSpriteDynamicColor(*sprite, set, shade, *picked);
        recordEdit(edit::spriteStateCommand(*m_document, *sprite, std::move(before)));
    };
    m_spritePanel->onEditColorsToDynamicSet = [this]() {
        const std::optional<std::size_t> sprite = currentSpriteIndex();
        if (!m_document || !sprite) {
            return;
        }
        const std::size_t set = m_spritePanel->dynamicSet();
        edit::SpriteState before = edit::spriteState(*m_document, *sprite);
        for (std::size_t shade = 0; shade < m_document->colorCount(); ++shade) {
            m_document->setSpriteDynamicColor(*sprite, set, shade, m_document->editColor(shade));
        }
        recordEdit(edit::spriteStateCommand(*m_document, *sprite, std::move(before)));
        statusBar()->showMessage(tr("Copied the edit colors to sprite set %1")
                                     .arg(static_cast<int>(set) + 1),
                                 2500);
    };
    m_spritePanel->onDynamicSetToEditColors = [this]() {
        const std::optional<std::size_t> sprite = currentSpriteIndex();
        if (!m_document || !sprite) {
            return;
        }
        const std::size_t set = m_spritePanel->dynamicSet();
        std::vector<uint16_t> colors;
        for (std::size_t shade = 0; shade < m_document->colorCount(); ++shade) {
            colors.push_back(m_document->spriteDynamicColor(*sprite, set, shade));
        }
        commitEditorColors(edit::withEditColors(m_document->colorSetTables(), colors),
                           tr("Copied sprite set %1 to the edit colors")
                               .arg(static_cast<int>(set) + 1));
    };
    m_spritePanel->onCreateHdRequested = [this]() { createCurrentSpriteHd(); };
    m_spritePanel->onRemoveHdRequested = [this]() { removeCurrentSpriteHd(); };
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
    // Back and forward through what the canvas showed, as in a web browser.
    m_historyBackButton = new QToolButton(central);
    m_historyBackButton->setText(QStringLiteral("<"));
    m_historyBackButton->setToolTip(
        tr("Back to what the canvas showed before: a frame, a background or a sprite (%1)")
            .arg(QKeySequence(QKeySequence::Back).toString(QKeySequence::NativeText)));
    m_historyBackButton->setShortcut(QKeySequence::Back);
    m_historyForwardButton = new QToolButton(central);
    m_historyForwardButton->setText(QStringLiteral(">"));
    m_historyForwardButton->setToolTip(
        tr("Forward again (%1)")
            .arg(QKeySequence(QKeySequence::Forward).toString(QKeySequence::NativeText)));
    m_historyForwardButton->setShortcut(QKeySequence::Forward);
    connect(m_historyBackButton, &QToolButton::clicked, this, [this]() { goBackOrForward(true); });
    connect(m_historyForwardButton, &QToolButton::clicked, this,
            [this]() { goBackOrForward(false); });
    toolbarLayout->addWidget(m_historyBackButton);
    toolbarLayout->addWidget(m_historyForwardButton);
    updateHistoryButtons();
    startToolbarGroup();
    toolbarLayout->addWidget(m_sdButton);
    toolbarLayout->addWidget(m_hdButton);
    toolbarLayout->addWidget(m_comparisonMaskOverlayButton);
    toolbarLayout->addWidget(m_dynamicMaskOverlayButton);
    toolbarLayout->addWidget(m_backgroundMaskOverlayButton);
    startToolbarGroup();

    const std::vector<std::pair<DrawingTool, QString>> tools{
        {DrawingTool::Point, tr("Point")},
        {DrawingTool::Line, tr("Line")},
        {DrawingTool::Rectangle, tr("Rect")},
        {DrawingTool::FilledRectangle, tr("Fill Rect")},
        {DrawingTool::Circle, tr("Circle")},
        {DrawingTool::FilledCircle, tr("Fill Circle")},
        {DrawingTool::Ellipse, tr("Ellipse")},
        {DrawingTool::FilledEllipse, tr("Fill Ellipse")},
        {DrawingTool::PaletteGradient, tr("Grad")},
        {DrawingTool::Selection, tr("Sel")},
        {DrawingTool::ColorPicker, tr("Pick")},
        {DrawingTool::FloodFill, tr("Flood")},
    };
    for (const auto& toolOption : tools) {
        const DrawingTool tool = toolOption.first;
        const QString label = toolOption.second;
        QToolButton* button = makeToolButton(central,
                                             label,
                                             tr("%1 tool").arg(label),
                                             static_cast<int>(tool));
        toolbarLayout->addWidget(button);
        m_toolButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this, tool]() {
            setDrawingTool(tool);
        });
    }
    startToolbarGroup();

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
                    QAction* editAction = menu.addAction(tr("Edit Color..."));
                    if (menu.exec(button->mapToGlobal(position)) == editAction) {
                        editFavoriteColor(colorIndex);
                    }
                });
    }
    m_activeColorButton = new QToolButton(central);
    m_activeColorButton->setFixedSize(34, 24);
    m_activeColorButton->setEnabled(false);
    m_activeColorButton->setToolTip(tr("Active draw color"));
    toolbarLayout->addWidget(m_activeColorButton);
    startToolbarGroup();
    m_playbackButton = new QToolButton(central);
    m_playbackButton->setText(tr("Play"));
    m_playbackButton->setToolTip(tr("Play visible frames"));
    m_playbackButton->setCheckable(true);
    m_playbackButton->setEnabled(false);
    m_loopPlaybackButton = new QToolButton(central);
    m_loopPlaybackButton->setText(tr("Loop"));
    m_loopPlaybackButton->setToolTip(tr("Loop playback at the end of the visible frame list"));
    m_loopPlaybackButton->setCheckable(true);
    m_loopPlaybackButton->setChecked(true);
    m_loopPlaybackButton->setEnabled(false);
    m_useProjectTimingCheckBox = new QCheckBox(QStringLiteral("cRP"), central);
    m_useProjectTimingCheckBox->setToolTip(
        tr("Use per-frame durations loaded from the .cRP file"));
    m_useProjectTimingCheckBox->setChecked(true);
    m_useProjectTimingCheckBox->setEnabled(false);
    m_playbackIntervalSpinBox = new QSpinBox(central);
    m_playbackIntervalSpinBox->setRange(20, 5000);
    m_playbackIntervalSpinBox->setSingleStep(10);
    m_playbackIntervalSpinBox->setSuffix(QStringLiteral(" ms"));
    m_playbackIntervalSpinBox->setValue(100);
    m_playbackIntervalSpinBox->setToolTip(tr("Playback frame interval"));
    m_playbackIntervalSpinBox->setEnabled(false);
    toolbarLayout->addWidget(m_playbackButton);
    toolbarLayout->addWidget(m_loopPlaybackButton);
    toolbarLayout->addWidget(m_useProjectTimingCheckBox);
    toolbarLayout->addWidget(m_playbackIntervalSpinBox);
    // The same action as View -> Runtime View, where it can be seen.
    m_runtimeViewButton = new QToolButton(central);
    m_runtimeViewButton->setDefaultAction(m_runtimeViewAction);
    toolbarLayout->addWidget(m_runtimeViewButton);
    // The same action as View -> Output To DMD: on, libdmdutil looks for displays and the
    // display follows the canvas; off, it lets them go.
    auto* dmdOutputButton = new QToolButton(central);
    dmdOutputButton->setDefaultAction(m_dmdOutputAction);
    toolbarLayout->addWidget(dmdOutputButton);
    startToolbarGroup();
    // The canvas zoom: out, the zoom (a click shows the whole frame), in.
    auto* zoomOutButton = new QToolButton(central);
    zoomOutButton->setText(QStringLiteral("-"));
    zoomOutButton->setToolTip(
        tr("Zoom out (%1)").arg(nativeShortcut(QStringLiteral("Ctrl+-"))));
    connect(zoomOutButton, &QToolButton::clicked, this, [this]() {
        if (spriteCanvasShown()) {
            m_spriteEditor->zoomOut();
        } else {
            m_preview->zoomOut();
        }
    });
    m_zoomButton = new QToolButton(central);
    m_zoomButton->setText(QStringLiteral("100%"));
    //: %1: the shortcut, %2: Ctrl (Cmd on macOS)
    m_zoomButton->setToolTip(tr("Canvas zoom; a click shows the whole frame (%1). %2 with the "
                                "mouse wheel or a pinch zooms at the mouse.")
                                 .arg(nativeShortcut(QStringLiteral("Ctrl+0")), ctrlKeyName()));
    connect(m_zoomButton, &QToolButton::clicked, this, [this]() {
        if (spriteCanvasShown()) {
            m_spriteEditor->resetZoom();
        } else {
            m_preview->resetZoom();
        }
    });
    auto* zoomInButton = new QToolButton(central);
    zoomInButton->setText(QStringLiteral("+"));
    zoomInButton->setToolTip(
        tr("Zoom in (%1)").arg(nativeShortcut(QStringLiteral("Ctrl++"))));
    connect(zoomInButton, &QToolButton::clicked, this, [this]() {
        if (spriteCanvasShown()) {
            m_spriteEditor->zoomIn();
        } else {
            m_preview->zoomIn();
        }
    });
    toolbarLayout->addWidget(zoomOutButton);
    toolbarLayout->addWidget(m_zoomButton);
    toolbarLayout->addWidget(zoomInButton);
    // The zoom of the canvas that is shown: the frame canvas or the sprite canvas.
    m_preview->onViewChanged = [this]() { updateZoomButton(); };
    if (m_spriteEditor) {
        m_spriteEditor->onZoomChanged = [this]() { updateZoomButton(); };
    }
    connect(m_canvasStack, &QStackedWidget::currentChanged, this, [this]() {
        endSpacePanning();
        updateZoomButton();
        updateInspectorSections();
    });
    // The rest of the last row, whatever its text: a long one (the identification hint) is
    // cut instead of widening the window; a row of its own when too little is left.
    m_frameStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toolbarFlow->addWidget(m_frameStatusLabel);
    toolbarFlow->setStretchLastItem(true, 200);
    layout->addWidget(toolbar);
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
    m_leftBrowserTabs = new SectionStack(central);
    m_leftBrowserTabs->setMinimumWidth(180);
    // The frames run along the bottom of the window (under the left sidebar and the canvas), as
    // in the Win32 editor: on the left of
    // the strip the sections and the frame filter, then the frames from left to right.
    auto* frameStrip = new QWidget(central);
    auto* frameStripLayout = new QHBoxLayout(frameStrip);
    frameStripLayout->setContentsMargins(0, 0, 0, 0);
    auto* frameStripControls = new QWidget(frameStrip);
    frameStripControls->setFixedWidth(250);
    auto* frameListLayout = new QVBoxLayout(frameStripControls);
    frameListLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionBar = new SectionBar(frameStripControls);
    m_sectionBar->onSectionChosen = [this](std::size_t section) {
        if (m_document) {
            goToFrame(edit::sections(*m_document).at(section).firstFrame);
        }
    };
    m_sectionBar->onNameRequested = [this]() { nameSectionAtCurrentFrame(); };
    m_sectionBar->onDeleteRequested = [this]() { deleteCurrentSection(); };
    m_sectionBar->onOrderRequested = [this]() { orderSections(); };
    frameListLayout->addWidget(m_sectionBar);
    frameListLayout->addWidget(m_frameFilterComboBox);
    // Out of a scene's timeline in one step, however many of its frames were visited.
    m_leaveSceneButton = new QToolButton(central);
    m_leaveSceneButton->setText(tr("Leave Scene"));
    m_leaveSceneButton->setToolTip(
        tr("Back to the frames and the frame shown before the scene's frames were listed"));
    m_leaveSceneButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_leaveSceneButton->setVisible(false);
    connect(m_leaveSceneButton, &QToolButton::clicked, this, [this]() { leaveSceneTimeline(); });
    frameListLayout->addWidget(m_leaveSceneButton);
    frameListLayout->addStretch(1);
    frameStripLayout->addWidget(frameStripControls);
    frameStripLayout->addWidget(m_frameList, 1);
    m_maskPage = new RecordListPage(m_maskList,
                                    tr("Add an empty comparison mask"),
                                    tr("Delete the selected comparison mask"),
                                    m_leftBrowserTabs);
    m_maskPage->onNewRequested = [this]() { addComparisonMask(); };
    m_maskPage->onDeleteRequested = [this]() { deleteSelectedComparisonMask(); };
    m_backgroundPage = new RecordListPage(
        m_backgroundList,
        tr("Add a background with the picture of the current frame"),
        tr("Delete the selected background"),
        m_leftBrowserTabs);
    m_backgroundPage->onNewRequested = [this]() { addBackgroundFromCurrentFrame(); };
    m_backgroundPage->onDeleteRequested = [this]() { deleteSelectedBackground(); };
    m_spritePage = new RecordListPage(m_spriteList,
                                      tr("Add an empty sprite"),
                                      tr("Delete the selected sprite"),
                                      m_leftBrowserTabs);
    m_spritePage->onNewRequested = [this]() { addSprite(); };
    m_spritePage->onDeleteRequested = [this]() { deleteSelectedSprite(); };
    // Open together, the lists share the height.
    m_leftBrowserTabs->addTab(m_maskPage, QStringLiteral("Masks"),
                              tr("Comparison Masks"));
    m_leftBrowserTabs->addTab(m_backgroundPage, QStringLiteral("Backgrounds"), tr("Backgrounds"));
    m_leftBrowserTabs->addTab(m_spritePage, QStringLiteral("Sprites"), tr("Sprites"));
    // The scenes, as records of the project beside the others; a selected scene is the asset
    // of "Using the selected asset".
    m_scenesPanel = new ScenesPanel(m_leftBrowserTabs);
    m_scenesPanel->onAddRequested = [this]() { addScene(); };
    m_scenesPanel->onDeleteRequested = [this](uint16_t sceneId) { deleteScene(sceneId); };
    m_scenesPanel->onOpenRequested = [this](uint16_t sceneId) { openScene(sceneId); };
    m_scenesPanel->onPressed = [this]() { m_leftBrowserTabs->markCurrent(m_scenesPanel); };
    m_scenesPanel->onSelectionChanged = [this]() { refreshSelectedAssetFilter(); };
    m_leftBrowserTabs->addTab(m_scenesPanel, QStringLiteral("Scenes"), tr("Scenes"));
    m_lightEffectsPanel = new LightEffectsPanel(m_leftBrowserTabs);
    m_lightEffectsPanel->onAddRequested = [this]() { addLightEffect(); };
    m_lightEffectsPanel->onDeleteRequested = [this](uint16_t effectId) {
        deleteLightEffect(effectId);
    };
    m_lightEffectsPanel->onPressed = [this]() {
        m_leftBrowserTabs->markCurrent(m_lightEffectsPanel);
    };
    m_lightEffectsPanel->onSelectionChanged = [this]() {
        updateLightEffectSection();
        refreshSelectedAssetFilter();
    };
    m_leftBrowserTabs->addTab(m_lightEffectsPanel, QStringLiteral("LightEffects"),
                              tr("Light Effects"));
    // What not every colorization uses; the author can hide them (the headers' context menu).
    for (const QString& key : {QStringLiteral("Scenes"), QStringLiteral("LightEffects")}) {
        for (int index = 0; index < m_leftBrowserTabs->count(); ++index) {
            if (m_leftBrowserTabs->tabText(index) == key) {
                m_leftBrowserTabs->setSectionOptional(index, true);
            }
        }
    }
    m_leftBrowserTabs->restoreOpenSections(QStringLiteral("assets"), {QStringLiteral("Masks")});
    // The asset of "Using the selected asset" is that of the list used last: opened, or
    // clicked in.
    m_leftBrowserTabs->onCurrentChanged = [this](int) { refreshSelectedAssetFilter(); };
    for (QListWidget* list : {m_maskList, m_backgroundList, m_spriteList}) {
        connect(list, &QListWidget::itemPressed, this, [this, list]() {
            m_leftBrowserTabs->markCurrent(list == m_maskList         ? m_maskPage
                                           : list == m_backgroundList ? m_backgroundPage
                                                                      : m_spritePage);
        });
    }
    auto* inspector = new QWidget(central);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    // The inspector's topics in sections that open and close, any number at a time.
    auto* inspectorTabs = new SectionStack(inspector);
    m_inspectorSections = inspectorTabs;

    inspectorTabs->addTab(m_framePanel, QStringLiteral("Frame"), tr("Frame"));

    // The mask of the frame and, below it, the comparison tools; they scroll together.
    auto* maskPage = new NarrowPage(inspectorTabs);
    auto* maskPageLayout = new QVBoxLayout(maskPage);
    maskPageLayout->setContentsMargins(0, 0, 0, 0);
    maskPageLayout->setSpacing(0);
    maskPageLayout->addWidget(m_maskPanel);
    maskPageLayout->addWidget(m_sameFramesPanel);
    inspectorTabs->addTab(maskPage, QStringLiteral("Mask"), tr("Comparison Mask"));
    inspectorTabs->onCurrentChanged = [this](int) { updateSameFrameControls(); };

    // The background and the sprite: on the frame canvas what ties them to the frames, while
    // one is edited the record itself (updateInspectorSections()).
    // The settings of the scene of the frame on the canvas, shown only then
    // (updateSceneSettingsSection()).
    m_sceneSettingsPanel = new SceneSettingsPanel(inspectorTabs);
    m_sceneSettingsPanel->onApplyRequested = [this](uint16_t sceneId,
                                                    const document::SceneDefinition& scene) {
        changeScene(sceneId, scene);
    };
    inspectorTabs->addTab(m_sceneSettingsPanel, QStringLiteral("SceneSettings"), tr("Scene"));
    // Hidden until a scene frame comes (it opens then).
    inspectorTabs->setSectionHidden(inspectorTabs->count() - 1, true);
    inspectorTabs->addTab(m_backgroundPanel->framePage(), QStringLiteral("BG"),
                          tr("Frame Background"));
    inspectorTabs->addTab(m_spritePanel->framePage(), QStringLiteral("Sprite"),
                          tr("Frame Sprites"));
    inspectorTabs->addTab(m_backgroundPanel->recordPage(), QStringLiteral("BGRecord"),
                          tr("Background"));
    inspectorTabs->addTab(m_spritePanel->recordPage(), QStringLiteral("SpriteRecord"),
                          tr("Sprite"));
    m_backgroundPanel->hide();
    m_spritePanel->hide();

    m_dynamicPanel = new DynamicPanel(central);
    m_dynamicPanel->onDynamicSetChanged = [this](int setIndex) {
        dynamicSetChanged(setIndex);
    };
    m_dynamicPanel->onColorClicked = [this](std::size_t originalColorIndex) {
        editDynamicColor(originalColorIndex);
    };
    m_dynamicPanel->onColorDropped = [this](std::size_t originalColorIndex, uint16_t rgb565) {
        editDynamicColor(originalColorIndex, rgb565);
    };
    m_dynamicPanel->onShadowDirectionToggled = [this](uint8_t bit, bool enabled) {
        setDynamicShadowDirectionBit(bit, enabled);
    };
    m_dynamicPanel->onShadowColorClicked = [this]() {
        editDynamicShadowColor();
    };
    m_dynamicPanel->onOriginalOnlyToggled = [this](bool originalOnly) {
        setDynamicSetOriginalOnly(originalOnly);
    };
    inspectorTabs->addTab(m_dynamicPanel, QStringLiteral("Dyn"), tr("Dynamic Sets"));

    m_frameLightsPanel = new FrameLightsPanel(central);
    m_frameLightsPanel->onChannelChanged = [this](std::size_t channel,
                                                  const document::LightAction& action) {
        setSelectedFramesLight(channel, action);
    };
    m_frameLightsPanel->onSuggestRequested = [this]() { suggestSelectedFramesLights(); };
    m_lightEffectPanel = new LightEffectPanel(central);
    m_lightEffectPanel->onApplyRequested = [this](const document::LightEffect& effect) {
        applyLightEffect(effect);
    };

    inspectorTabs->addTab(m_palettePanel, QStringLiteral("Palette"), tr("Palette"));
    inspectorTabs->addTab(m_colorRotationPanel, QStringLiteral("Rot"),
                          tr("Color Rotations"));

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
    inspectorTabs->addTab(m_colorSetPanel, QStringLiteral("Sets"), tr("Color Sets"));
    m_helperLinesPanel = new HelperLinesPanel(central);
    m_helperLinesPanel->onAddRequested = [this](bool vertical) { addHelperLine(vertical); };
    m_helperLinesPanel->onLinesShownChanged = [this](bool shown) {
        if (m_helperLinesShownAction) {
            m_helperLinesShownAction->setChecked(shown);
        }
    };
    applyHelperLinesShown();
    m_helperLinesPanel->onEditRequested = [this](std::size_t line) { editHelperLine(line); };
    m_helperLinesPanel->onDeleteRequested = [this](std::size_t line) {
        if (!m_document || line >= m_document->helperLines().size()) {
            return;
        }
        std::vector<document::HelperLine> lines = m_document->helperLines();
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(line));
        changeHelperLines(std::move(lines), tr("Deleted the helper line"));
    };
    m_helperLinesPanel->onVisibilityChanged = [this](std::size_t line, bool visible) {
        if (!m_document || line >= m_document->helperLines().size()) {
            return;
        }
        std::vector<document::HelperLine> lines = m_document->helperLines();
        lines[line].visible = visible;
        changeHelperLines(std::move(lines),
                          visible ? tr("Showing the helper line") : tr("Hid the helper line"));
    };
    inspectorTabs->addTab(m_helperLinesPanel, QStringLiteral("HelperLines"), tr("Helper Lines"));
    // The speaker lights last: a topic of their own, after the frame's pictures.
    inspectorTabs->addTab(m_frameLightsPanel, QStringLiteral("Lights"), tr("Speaker Lights"));
    inspectorTabs->addTab(m_lightEffectPanel, QStringLiteral("LightEffect"), tr("Light Effect"));
    // The action buttons of every section alike (InspectorForm.h).
    for (QWidget* page : std::initializer_list<QWidget*>{
             m_framePanel, m_maskPanel, m_backgroundPanel->framePage(),
             m_backgroundPanel->recordPage(), m_spritePanel->framePage(),
             m_spritePanel->recordPage(), m_dynamicPanel,
             m_palettePanel, m_colorRotationPanel, m_colorSetPanel, m_sameFramesPanel,
             m_helperLinesPanel}) {
        if (page) {
            fillButtonRows(page->layout());
        }
    }

    inspectorLayout->addWidget(inspectorTabs, 1);
    for (const QString& key : {QStringLiteral("Rot"), QStringLiteral("Sets"),
                               QStringLiteral("HelperLines"), QStringLiteral("Lights"),
                               QStringLiteral("LightEffect")}) {
        for (int index = 0; index < inspectorTabs->count(); ++index) {
            if (inspectorTabs->tabText(index) == key) {
                inspectorTabs->setSectionOptional(index, true);
            }
        }
    }
    inspectorTabs->restoreOpenSections(QStringLiteral("inspector"), {QStringLiteral("Frame")});
    updateInspectorSections();
    inspector->setMinimumWidth(200);
    // The two sidebars can be made wider or narrower; their widths are kept.
    auto* contentSplitter = new QSplitter(Qt::Horizontal, central);
    contentSplitter->setChildrenCollapsible(false);
    // The left sidebar: the filter of its lists above them.
    auto* assets = new QWidget(central);
    auto* assetsLayout = new QVBoxLayout(assets);
    assetsLayout->setContentsMargins(0, 0, 0, 0);
    assetsLayout->setSpacing(2);
    m_currentFrameAssetsCheckBox = new QCheckBox(tr("Used by the current frame"), assets);
    m_currentFrameAssetsCheckBox->setToolTip(
        tr("List only the comparison mask, the background and the sprites of the current "
           "frame"));
    m_currentFrameAssetsCheckBox->setChecked(
        QSettings().value(QStringLiteral("layout/assetsOfCurrentFrame"), false).toBool());
    connect(m_currentFrameAssetsCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue(QStringLiteral("layout/assetsOfCurrentFrame"), checked);
        applyAssetFilter();
    });
    assetsLayout->addWidget(m_currentFrameAssetsCheckBox);
    assetsLayout->addWidget(m_leftBrowserTabs, 1);
    assets->setMinimumWidth(m_leftBrowserTabs->minimumWidth());
    contentSplitter->addWidget(assets);
    // The speaker light preview beside the canvas (View -> Speaker Light Preview), centred on
    // the colorized frame; the splitter's handles size the speakers.
    auto* canvasRow = new QSplitter(Qt::Horizontal, central);
    canvasRow->setChildrenCollapsible(false);
    m_speakerLights = new SpeakerLightPair(canvasRow);
    canvasRow->addWidget(m_speakerLights->left());
    canvasRow->addWidget(m_canvasStack);
    canvasRow->addWidget(m_speakerLights->right());
    canvasRow->setStretchFactor(0, 0);
    canvasRow->setStretchFactor(1, 1);
    canvasRow->setStretchFactor(2, 0);
    {
        // Both speakers as wide as stored (or 96 pixels), the canvas the rest.
        const int speakerWidth = std::clamp(
            QSettings().value(QStringLiteral("layout/speakerWidth"), 96).toInt(), 32, 600);
        canvasRow->setSizes({speakerWidth, 1000, speakerWidth});
    }
    connect(canvasRow, &QSplitter::splitterMoved, this, [canvasRow](int, int handle) {
        // The handle dragged sizes both speakers alike.
        QList<int> sizes = canvasRow->sizes();
        const int width = handle == 1 ? sizes[0] : sizes[2];
        const int total = sizes[0] + sizes[1] + sizes[2];
        sizes = {width, std::max(1, total - 2 * width), width};
        const QSignalBlocker blocker(canvasRow);
        canvasRow->setSizes(sizes);
        QSettings().setValue(QStringLiteral("layout/speakerWidth"), width);
    });
    m_speakerLights->setOptions(loadSpeakerLightPreviewOptions());
    m_speakerLights->setCenterSource([this]() -> std::optional<int> {
        if (!m_preview || !m_preview->isVisible()) {
            return std::nullopt;
        }
        for (const FramePreviewWidget::RegionView& view : m_preview->regionViews()) {
            if (view.region.region == render::CanvasRegion::ColorizedFrame) {
                const double frameCenter = view.origin.y() + view.contentSize.height() / 2.0;
                const double y = std::clamp(frameCenter, view.viewport.top(),
                                            view.viewport.bottom());
                return m_preview->mapToGlobal(QPoint(0, static_cast<int>(y))).y();
            }
        }
        return std::nullopt;
    });
    contentSplitter->addWidget(canvasRow);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    // The right sidebar runs the full height; the frame strip runs under the left sidebar and
    // the canvas.
    auto* mainColumn = new QWidget(central);
    auto* mainColumnLayout = new QVBoxLayout(mainColumn);
    mainColumnLayout->setContentsMargins(0, 0, 0, 0);
    mainColumnLayout->addWidget(contentSplitter, 1);
    mainColumnLayout->addWidget(frameStrip, 0);
    auto* outerSplitter = new QSplitter(Qt::Horizontal, central);
    outerSplitter->setChildrenCollapsible(false);
    outerSplitter->addWidget(mainColumn);
    outerSplitter->addWidget(inspector);
    outerSplitter->setStretchFactor(0, 1);
    outerSplitter->setStretchFactor(1, 0);
    // Kept as the widths of the left sidebar, the canvas and the right sidebar.
    const QList<int> storedWidths =
        [](const QVariantList& stored) {
            QList<int> widths;
            for (const QVariant& width : stored) {
                widths << width.toInt();
            }
            return widths;
        }(QSettings().value(QStringLiteral("layout/sidebarWidths")).toList());
    const QList<int> widths =
        storedWidths.size() == 3 ? storedWidths : QList<int>{260, 700, 280};
    contentSplitter->setSizes({widths[0], widths[1]});
    outerSplitter->setSizes({widths[0] + widths[1], widths[2]});
    const auto storeWidths = [contentSplitter, outerSplitter]() {
        const QList<int> inner = contentSplitter->sizes();
        const QList<int> outer = outerSplitter->sizes();
        QSettings().setValue(QStringLiteral("layout/sidebarWidths"),
                             QVariantList{inner.value(0), inner.value(1), outer.value(1)});
    };
    connect(contentSplitter, &QSplitter::splitterMoved, this, storeWidths);
    connect(outerSplitter, &QSplitter::splitterMoved, this, storeWidths);
    contentLayout->addWidget(outerSplitter, 1);
    layout->addLayout(contentLayout, 1);

    connect(m_frameList, &QListWidget::currentRowChanged, this, [this](int row) {
        selectFrame(row);
    });
    // While a sprite or a background is edited, a double click on a frame (its first click has
    // made it the current frame) goes back to editing the frame.
    connect(m_frameList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (spriteCanvasShown()) {
            leaveSpriteCanvas();
            statusBar()->showMessage(tr("Editing the frame"), 1500);
        }
        if (activePaintTarget() == PaintTarget::Background) {
            editFrameInsteadOfBackground();
        }
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
        refreshSelectedAssetFilter();
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
        refreshSelectedAssetFilter();
    });
    // A double click opens the background or the sprite on the canvas for editing (its first
    // click has selected it), as the Inspector's Edit and Sprite Canvas do.
    connect(m_backgroundList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        editAssignedBackground();
    });
    connect(m_spriteList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        if (!m_spritePanel || m_spritePanel->isSpriteCanvasChecked()) {
            return;
        }
        m_spritePanel->setSpriteCanvasChecked(true);
        if (m_spritePanel->onSpriteCanvasToggled) {
            m_spritePanel->onSpriteCanvasToggled(true);
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
        refreshSelectedAssetFilter();
    });
    connect(m_frameFilterComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                refreshFrameList(currentFrameIndex());
                // Another timeline: a place of the back/forward history.
                noteCanvasPlace(true);
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
                // Another background while one is edited: another canvas content.
                noteCanvasPlace();
            });
    m_preview->setPixelClickedHandler([this](render::CanvasHit hit, bool erase) {
        colorizedCanvasClicked(hit, erase);
    });
    // A mask or background dropped onto the canvas is assigned to the selected frames, as its
    // Assign button does (one undo step); a scene becomes their trigger, as the Frame
    // section's Scene field makes it.
    m_preview->setDropHandler([this](QObject* source, bool drop) {
        if (m_document && source && m_scenesPanel && source == m_scenesPanel->dragSource()) {
            const std::optional<uint16_t> scene = m_scenesPanel->selectedScene();
            if (!scene || selectedFrameIndices().empty()) {
                return false;
            }
            if (!drop) {
                statusBar()->showMessage(
                    tr("Drop to let the %1 selected frame(s) start scene %2")
                        .arg(static_cast<int>(selectedFrameIndices().size()))
                        .arg(*scene),
                    2000);
                return true;
            }
            setSelectedFramesTrigger(document::TriggerKind::Scene, *scene);
            return true;
        }
        if (!m_document || selectedFrameIndices().empty() || !source ||
            (source != m_maskList && source != m_backgroundList)) {
            return false;
        }
        auto* list = static_cast<QListWidget*>(source);
        const QListWidgetItem* item = list->currentItem();
        const int index = item ? item->data(Qt::UserRole).toInt() : -1;
        const bool mask = source == m_maskList;
        if (index < 0 ||
            static_cast<std::size_t>(index) >=
                (mask ? m_document->comparisonMaskCount() : m_document->backgroundCount())) {
            return false;
        }
        const int frames = static_cast<int>(selectedFrameIndices().size());
        if (!drop) {
            statusBar()->showMessage(
                tr("Drop to assign %1 %2 to the %3 selected frame(s)")
                    .arg(mask ? tr("comparison mask") : tr("background"))
                    .arg(index)
                    .arg(frames),
                2000);
            return true;
        }
        if (mask) {
            m_selectedMaskIndex = static_cast<std::size_t>(index);
            assignSelectedMaskToFrames();
        } else {
            const int comboIndex = m_backgroundComboBox->findData(index);
            if (comboIndex < 0) {
                return false;
            }
            m_backgroundComboBox->setCurrentIndex(comboIndex);
            assignSelectedBackgroundToFrames();
        }
        return true;
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
    // The pixel a click would paint, under the mouse before the click. Not while the next click
    // does something else: paste mode, a pending Select By Color or Recolor click, Ctrl/Cmd
    // (or Control on macOS) held for selecting.
    m_preview->setPointerHandler([this](std::optional<render::CanvasHit> hit) {
        const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
        if (m_pasteMode.isActive() || m_pendingCanvasClick != PendingCanvasClick::None ||
            (modifiers & (Qt::ControlModifier | Qt::MetaModifier))) {
            hit.reset();
        }
        m_canvasEdit.pointerMoved(canvasEditContext(), hit,
                                  modifiers.testFlag(Qt::ShiftModifier));
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
                    tr("Updated sprite %1 detection area %2")
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
            if (m_spriteEditor->isDynamicPaintMode()) {
                paintSpriteDynamic(spriteIndex, {render::Point{x, y}}, erase);
                return;
            }
            paintSpriteColorizedPixel(spriteIndex, resolution, x, y, erase);
        });
    m_spriteEditor->setColorizedPixelsEditedHandler(
        [this](std::size_t spriteIndex,
               render::Resolution resolution,
               const std::vector<render::Point>& pixels,
               bool erase) {
            if (m_spriteEditor->isDynamicPaintMode()) {
                paintSpriteDynamic(spriteIndex, pixels, erase);
                return;
            }
            paintSpriteColorizedPixels(spriteIndex, resolution, pixels, erase);
        });
    m_spriteEditor->setColorizedPixelPickedHandler(
        [this](std::size_t spriteIndex, render::Resolution resolution, int x, int y) {
            pickSpriteColorizedPixel(spriteIndex, resolution, x, y);
        });
    m_spriteEditor->setColorizedFloodFillHandler(
        [this](std::size_t spriteIndex, render::Resolution resolution, int x, int y) {
            if (m_spriteEditor->isDynamicPaintMode()) {
                statusBar()->showMessage(
                    tr("Dynamic zones are painted with the point and shape tools"),
                    3000);
                return;
            }
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
    if (!confirmClosingProject(tr("Quit"))) {
        event->ignore();
        return;
    }
    setPlaybackRunning(false);
    // A write still running is stopped; the autosave files it was replacing stay as they were.
    m_autosaver->cancelAndWait();
    setDmdOutputEnabled(false);
    event->accept();
}

void MainWindow::openProject()
{
    if (!confirmClosingProject(tr("Open Project"))) {
        return;
    }
    const QString cromPath = QFileDialog::getOpenFileName(
        this,
        tr("Open cROM or cRZ"),
        QString(),
        tr("Serum colorization (*.cROM *.cRom *.crom *.cRZ *.crz *.CRZ);;"
           "All files (*)"));
    if (cromPath.isEmpty()) {
        return;
    }

    // The project's editor file: the .cRPX this editor saves, or the .cRP of a project the
    // Win32 editor saved last.
    QString crpPath = editorFileFor(cromPath);
    if (!QFileInfo::exists(crpPath)) {
        crpPath = siblingProjectFile(cromPath, QStringLiteral("cRP"));
    }
    if (!QFileInfo::exists(crpPath)) {
        crpPath = QFileDialog::getOpenFileName(
            this,
            tr("Open matching cRPX or cRP"),
            QFileInfo(cromPath).dir().absolutePath(),
            tr("Serum editor project (*.cRPX *.cRP);;All files (*)"));
        if (crpPath.isEmpty()) {
            return;
        }
    }

    loadProject(cromPath, crpPath);
}

void MainWindow::openRecentProject(const QString& entry)
{
    if (!confirmClosingProject(tr("Open Recent Project"))) {
        return;
    }
    QString cromPath = recentProjectCromPath(entry);
    QString crpPath = recentProjectCrpPath(entry);
    if (cromPath.isEmpty() || crpPath.isEmpty()) {
        statusBar()->showMessage(tr("Recent project entry is incomplete"), 4000);
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
    // A Win32 project has its .cRPX beside the .cRP since it was saved here.
    if (!isCrpxPath(crpPath) && QFileInfo::exists(editorFileFor(cromPath))) {
        crpPath = editorFileFor(cromPath);
    }
    loadProject(cromPath, crpPath);
}

bool MainWindow::isUntitledProject() const
{
    return m_document && (m_currentCromPath.isEmpty() || m_currentCrpPath.isEmpty());
}

void MainWindow::updateWindowTitle()
{
    const QString& application = kApplicationName;
    if (!m_document) {
        setWindowTitle(application);
    } else if (isUntitledProject()) {
        setWindowTitle(tr("%1 (not saved yet) \u2014 %2")
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
    // A 192x64 project loses the 96x32 pictures its files still hold.
    const QString lost = discardedExtraPlaneWarning(m_document->discardedExtraPlane());
    if (!lost.isEmpty()) {
        QMessageBox question(this);
        question.setIcon(QMessageBox::Warning);
        question.setWindowTitle(tr("Save Project"));
        question.setText(lost);
        QPushButton* saveButton =
            question.addButton(tr("Save Without Them"), QMessageBox::DestructiveRole);
        QPushButton* cancelButton = question.addButton(QMessageBox::Cancel);
        question.setDefaultButton(cancelButton);
        question.setEscapeButton(cancelButton);
        question.exec();
        if (question.clickedButton() != saveButton) {
            statusBar()->showMessage(tr("Not saved"), 3000);
            return false;
        }
    }
    applyPendingSpriteDependentViews();

    // The files are written on a worker thread (runProjectTask) from a snapshot that shares
    // every block with the document (copy on write), so the save copies no pixels. Nothing
    // can change the document meanwhile: the UI handles no input until the save has ended.
    // The .cRZ and the .cRPX; no .cROM.
    const legacy::LegacyProjectPaths paths{
        {},
        m_currentCrpPath.toStdString(),
        m_currentCromPath.toStdString(),
    };
    legacy::LegacySaveResult result;
    {
        document::ProjectSnapshot snapshot = m_document->toSnapshot();
        // The time spent on it goes into the file, not into the project: counting does not
        // change the project.
        snapshot.editorMetadata.timeElapsedMs = projectTimeMs();
        legacy::LegacyIoProgress progress;
        const std::optional<std::string> failure =
            runProjectTask(this, tr("Saving Project"), progress, [&]() {
                result = legacy::LegacyProjectFileWriter().save(paths, snapshot, &progress);
            });
        if (failure) {
            result = legacy::makeSaveFailure(*failure);
        }
    }

    if (result.cancelled) {
        statusBar()->showMessage(
            tr("Save cancelled: the project files on disk are unchanged"), 4000);
        return false;
    }
    if (!result.ok) {
        QMessageBox::critical(this,
                              tr("Save Project Failed"),
                              QString::fromStdString(result.error.message));
        statusBar()->showMessage(tr("Save failed"), 4000);
        return false;
    }

    m_document->markClean();
    m_document->forgetDiscardedExtraPlane();
    updateProjectHeaderStatus();
    QStringList retired;
    for (const QString& file : {retireWin32EditorFile(m_currentCromPath),
                                retireLooseCrom(m_currentCromPath)}) {
        if (!file.isEmpty()) {
            retired << QFileInfo(file).fileName();
        }
    }
    if (!retired.isEmpty()) {
        rememberRecentProject(m_currentCromPath, m_currentCrpPath);
    }
    statusBar()->showMessage(
        retired.isEmpty()
            ? tr("Project saved")
            : tr("Project saved; renamed to .bak, as they are out of date: %1")
                  .arg(retired.join(QStringLiteral(", "))),
        retired.isEmpty() ? 3000 : 8000);
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
                                                  QStringLiteral(".cRZ"));
    } else {
        const QFileInfo currentCromInfo(m_currentCromPath);
        proposedPath = currentCromInfo.dir().absoluteFilePath(
            currentCromInfo.completeBaseName() + QStringLiteral("_copy.cRZ"));
    }
    QString targetCromPath = QFileDialog::getSaveFileName(
        this,
        untitled ? tr("Save New Project As") : tr("Save Project As"),
        proposedPath,
        tr("Serum colorization (*.cRZ);;All files (*)"));
    if (targetCromPath.isEmpty()) {
        return false;
    }
    if (!isCrzPath(targetCromPath)) {
        targetCromPath += QStringLiteral(".cRZ");
    }

    const QFileInfo targetCromInfo(targetCromPath);
    const QString targetCrpPath =
        targetCromInfo.dir().absoluteFilePath(targetCromInfo.completeBaseName() +
                                              QStringLiteral(".cRPX"));
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
        // Another folder may have another pup.csv.
        updateRuntimeSceneFile();
        statusBar()->showMessage(untitled ? tr("Project saved as %1")
                                                .arg(targetCromInfo.fileName())
                                          : tr("Project saved as copy"),
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
    question.setText(tr("The current project has changes that are not saved."));
    question.setInformativeText(
        isUntitledProject()
            ? tr("Save asks where to save the new project first. Discard Changes "
                 "loses them.")
            : tr("Save writes them first. Discard Changes loses them."));
    QPushButton* saveButton = question.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton* discardButton =
        question.addButton(tr("Discard Changes"), QMessageBox::DestructiveRole);
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
    if (!confirmClosingProject(tr("New Project From Dump"))) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Choose Frame Dump For New Project"),
        m_dumpDirectory,
        tr("Frame dumps (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    DumpImportDialog dialog(DumpImportDialog::Mode::NewProject, nullptr, this);
    keepWindowGeometry(&dialog, QStringLiteral("dumpImport"));
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
        // A new project keeps its scenes and scaling settings itself.
        m_document->setColorizationSettings(document::ColorizationSettings{});
        m_document->markUnsaved();
        m_currentCromPath.clear();
        m_currentCrpPath.clear();
        m_undoHistories.clear();
        m_canvasHistory.clear();
        updateHistoryButtons();
        startProjectClock();
        updateRuntimeSceneFile();
        m_pasteMode.clearClipboard();
        updateUndoRedoActions();
        if (m_saveAction) {
            m_saveAction->setEnabled(true);
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this,
                              tr("New Project Failed"),
                              QString::fromUtf8(ex.what()));
        return;
    }
    refreshProjectSummary();
    statusBar()->showMessage(
        tr("New project %1, not saved yet: %2")
            .arg(QString::fromStdString(m_document->romName()), summary),
        8000);
}

void MainWindow::addFramesFromDump()
{
    if (!m_document) {
        statusBar()->showMessage(tr("No project loaded"), 2500);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Choose Frame Dump To Add"),
        m_dumpDirectory,
        tr("Frame dumps (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    // The dialog's worker reads the document: nothing may change it meanwhile.
    setPlaybackRunning(false);
    applyPendingSpriteDependentViews();
    DumpImportDialog dialog(DumpImportDialog::Mode::AddFrames, &*m_document, this);
    keepWindowGeometry(&dialog, QStringLiteral("dumpImport"));
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
        showStructureEditStatus(tr("Frames added: %1; %2 frames now")
                                     .arg(summary)
                                     .arg(static_cast<qulonglong>(m_document->frameCount())),
                                 8000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setDmdOutputEnabled(bool enabled)
{
    if (!DmdOutput::isAvailable()) {
        return;
    }
    if (m_dmdOutputAction && m_dmdOutputAction->isChecked() != enabled) {
        const QSignalBlocker blocker(m_dmdOutputAction);
        m_dmdOutputAction->setChecked(enabled);
    }
    if (!enabled) {
        m_dmdTimer.stop();
        m_dmdOutput.stop();
        m_dmdPictureKey.reset();
        m_runtimeView.setOtherRows(std::nullopt);
        return;
    }
    if (!m_dmdTimer.isActive()) {
        m_dmdTimer.setInterval(33);
        // A member function, not a lambda: Qt refuses a unique connection to a lambda (with
        // only a warning), and the timer then sent no picture at all.
        connect(&m_dmdTimer, &QTimer::timeout, this, &MainWindow::sendDmdPicture,
                Qt::UniqueConnection);
    }
    const DmdOutputSettings settings = loadDmdOutputSettings();
    m_dmdPicture = settings.picture;
    m_runtimeView.setOtherRows(dmdRuntimeRows());
    if (!m_dmdOutput.start(settings)) {
        setDmdOutputEnabled(false);
        statusBar()->showMessage(
            tr("DMD output failed: %1 (more in Help -> Plugin Log)")
                .arg(m_dmdOutput.lastMessage()),
            8000);
        return;
    }
    m_dmdPictureKey.reset();
    m_dmdTimer.start();
    statusBar()->showMessage(tr("DMD output: looking for displays"), 3000);
    // libdmdutil looks in the background and logs what it found.
    QTimer::singleShot(4000, this, [this]() {
        if (!m_dmdOutput.isRunning()) {
            return;
        }
        // The last line libdmdutil logged may be about a display it did not find while another
        // one was found; the displays found are named instead.
        const std::vector<QString> found = m_dmdOutput.foundDisplays();
        if (found.empty()) {
            vpx::Host::log(vpx::Host::kLogInfo,
                           "DMD output: no display reported after 4 seconds. For a ZeDMD, "
                           "switch on \"ZeDMD debug output\" in the DMD Output settings and "
                           "restart the output to see its search.");
        }
        QStringList names;
        for (const QString& line : found) {
            names << line;
        }
        statusBar()->showMessage(
            found.empty()
                ? tr("DMD output on, but no display found (a PIN2DMD is not reported); see "
                     "Help -> Plugin Log")
                : tr("DMD output: %1").arg(names.join(QStringLiteral("; "))),
            8000);
    });
}

void MainWindow::editSettings(SettingsDialog::Page page)
{
    SettingsDialog dialog(loadTheme(), m_dmdRenderOptions, loadDmdOutputSettings(),
                          DmdOutput::isAvailable(), this);
    dialog.setLanguage(loadLanguage());
    dialog.setSpeakerLightPreview(loadSpeakerLightPreviewOptions());
    {
        std::vector<SettingsDialog::PluginChoice> plugins;
        for (const EditorPlugins::Plugin& plugin : EditorPlugins::available()) {
            plugins.push_back({plugin.id, plugin.name, plugin.description, plugin.version,
                               plugin.builtIn, plugin.builtInUse});
        }
        dialog.setPlugins(plugins, EditorPlugins::enabledInSettings());
    }
    dialog.showPage(page);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (m_plugins && dialog.enabledPlugins() != EditorPlugins::enabledInSettings()) {
        const QStringList errors = m_plugins->setEnabled(dialog.enabledPlugins());
        if (!errors.isEmpty()) {
            QMessageBox::warning(this, tr("Plugins"), errors.join(QChar('\n')));
        }
    }
    if (dialog.language() != loadLanguage()) {
        saveLanguage(dialog.language());
        QMessageBox::information(this, tr("Language"),
                                 tr("The language changes at the next start."));
    }
    if (dialog.theme() != loadTheme()) {
        saveTheme(dialog.theme());
        applyTheme(dialog.theme());
    }
    m_dmdRenderOptions = dialog.canvasOptions();
    saveDmdRenderOptions(m_dmdRenderOptions);
    saveSpeakerLightPreviewOptions(dialog.speakerLightPreview());
    if (m_speakerPreviewAction) {
        const QSignalBlocker blocker(m_speakerPreviewAction);
        m_speakerPreviewAction->setChecked(dialog.speakerLightPreview().visible);
    }
    if (m_speakerLights) {
        m_speakerLights->setOptions(dialog.speakerLightPreview());
        updateFrameLightControls();
    }
    updateDmdRenderOptions();
    saveDmdOutputSettings(dialog.dmdOutputSettings());
    if (m_dmdOutput.isRunning()) {
        setDmdOutputEnabled(true);
    }
}

std::optional<runtime::PlaneRequest> MainWindow::dmdRuntimeRows() const
{
    switch (m_dmdPicture) {
        case DmdOutputSettings::Picture::Native:
            return runtime::PlaneRequest::Rows32;
        case DmdOutputSettings::Picture::Extra:
            return runtime::PlaneRequest::Rows64;
        case DmdOutputSettings::Picture::Canvas:
            break;
    }
    return std::nullopt;
}

bool MainWindow::dmdPrefersRows64() const
{
    if (const std::optional<runtime::PlaneRequest> rows = dmdRuntimeRows()) {
        return *rows == runtime::PlaneRequest::Rows64;
    }
    return m_activeResolution == render::Resolution::Hd;
}

void MainWindow::sendDmdPicture()
{
    if (!m_dmdOutput.isRunning()) {
        return;
    }
    if (m_dmdPictureOverride) {
        if (const std::optional<document::Rgb565Frame> picture = m_dmdPictureOverride()) {
            m_dmdOutput.show(*picture);
        }
        m_dmdPictureKey.reset();
        return;
    }
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        // The plugin looks for the displays when it gets its first picture: without a frame
        // to show, the displays are found and kept black.
        document::Rgb565Frame black;
        black.size = {128, 32};
        black.pixels = document::CompactArray<uint16_t>(128 * 32, 0);
        m_dmdOutput.show(black);
        m_dmdPictureKey.reset();
        return;
    }
    render::Resolution plane = isHdOnlyProject()                       ? render::Resolution::Hd
                               : m_document->editsOriginalPlaneOnly() ? render::Resolution::Sd
                                                                      : m_activeResolution;
    // The plane the settings ask for, in a project that has both.
    if (m_document->geometry().layout() == render::PlaneLayout::Ordinary &&
        !m_document->editsOriginalPlaneOnly()) {
        if (m_dmdPicture == DmdOutputSettings::Picture::Native) {
            plane = render::Resolution::Sd;
        } else if (m_dmdPicture == DmdOutputSettings::Picture::Extra) {
            plane = render::Resolution::Hd;
        }
    }
    if (plane == render::Resolution::Hd && !m_document->hasHdFrame(*frameIndex)) {
        plane = render::Resolution::Sd;
    }
    const bool runtime = m_runtimeView.isEnabled();
    const bool background = !m_framePanel || m_framePanel->isBackgroundLayerChecked();
    const bool sprites = !m_framePanel || m_framePanel->isSpritesLayerChecked();
    // The same picture as last time: nothing to compose (playback steps the frame, edits the
    // revision; the Runtime view's colour rotations change its picture without either).
    const DmdPictureKey key{m_document->revision(), *frameIndex, plane, runtime, background,
                            sprites};
    if (!runtime && m_dmdPictureKey && *m_dmdPictureKey == key) {
        return;
    }
    m_dmdPictureKey = key;
    if (runtime) {
        const std::optional<runtime::PlaneRequest> rows = dmdRuntimeRows();
        if (const document::Rgb565Frame* picture =
                rows ? m_runtimeView.image(*m_document, *frameIndex, *rows)
                     : m_runtimeView.image(*m_document, *frameIndex)) {
            m_dmdOutput.show(*picture);
        }
        return;
    }
    m_dmdOutput.show(
        composeResolvedFrame(*m_document, *frameIndex, plane, background, nullptr, sprites));
}

void MainWindow::exportAnimatedGif(bool withOriginals, bool bothPlanes)
{
    if (!m_document) {
        statusBar()->showMessage(tr("No project loaded"), 2500);
        return;
    }
    std::vector<std::size_t> frames = selectedFrameIndices();
    std::sort(frames.begin(), frames.end());
    if (frames.empty()) {
        statusBar()->showMessage(tr("Select the frames to export"), 2500);
        return;
    }
    GifExportOptions options;
    options.plane = isHdOnlyProject() ? render::Resolution::Hd
                    : m_document->editsOriginalPlaneOnly() ? render::Resolution::Sd
                                                           : m_activeResolution;
    options.includeBackground = !m_framePanel || m_framePanel->isBackgroundLayerChecked();
    options.includeSprites = !m_framePanel || m_framePanel->isSpritesLayerChecked();
    options.includeOriginal = withOriginals;
    options.bothPlanes = bothPlanes;
    // The Win32 editor's style: 5 picture pixels per frame pixel on every plane.
    options.pixelScale = kGifPixelScale;

    const QDir untitledDirectory(m_dumpDirectory.isEmpty() ? QDir::homePath()
                                                           : m_dumpDirectory);
    const QString base =
        isUntitledProject()
            ? untitledDirectory.absoluteFilePath(QString::fromStdString(m_document->romName()))
            : QFileInfo(m_currentCromPath).absoluteDir().absoluteFilePath(
                  QFileInfo(m_currentCromPath).completeBaseName());
    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export Animated GIF"),
        QStringLiteral("%1_%2-%3.gif")
            .arg(base)
            .arg(static_cast<qulonglong>(frames.front()))
            .arg(static_cast<qulonglong>(frames.back())),
        tr("Animated GIF (*.gif);;All files (*)"));
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
            runProjectTask(this, tr("Exporting Animated GIF"), progress, [&]() {
                std::ofstream stream(platform::pathFromUtf8(partial.toStdString()),
                                     std::ios::binary | std::ios::trunc);
                if (!stream) {
                    result.error = tr("the file cannot be written").toStdString();
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
            statusBar()->showMessage(tr("Export cancelled"), 3000);
        } else {
            QMessageBox::critical(this,
                                  tr("Export Animated GIF Failed"),
                                  QString::fromStdString(result.error));
        }
        return;
    }
    QFile::remove(path);
    if (!QFile::rename(partial, path)) {
        QFile::remove(partial);
        QMessageBox::critical(this,
                              tr("Export Animated GIF Failed"),
                              tr("%1 cannot be replaced").arg(path));
        return;
    }
    statusBar()->showMessage(tr("Exported %1 frame(s) as %2 picture(s) to %3")
                                 .arg(static_cast<qulonglong>(frames.size()))
                                 .arg(static_cast<qulonglong>(result.pictures))
                                 .arg(QDir::toNativeSeparators(path)),
                             6000);
}

void MainWindow::openFrameTester()
{
    if (!m_document) {
        statusBar()->showMessage(tr("No project loaded"), 2500);
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
        keepWindowGeometry(&dialog, QStringLiteral("frameTester"));
        dialog.onAddFrameRequested = [this](const runtime::TesterFrame& frame) {
            try {
                std::vector<document::FrameRecord> frames;
                frames.push_back(m_document->makeUncolorizedFrame(frame.original,
                                                                  frame.durationMs));
                recordStructureEdit(edit::addFrames(*m_document, std::move(frames)));
                showStructureEditStatus(tr("Added the dump frame as frame %1")
                                            .arg(static_cast<qulonglong>(
                                                m_document->frameCount() - 1)),
                                        4000);
                return true;
            } catch (const std::exception& ex) {
                statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
                return false;
            }
        };
        // The display shows the tester's picture while it is open, of the plane the DMD
        // output settings ask for (the other one when the frame has only that).
        m_dmdPictureOverride = [this, &dialog]() -> std::optional<document::Rgb565Frame> {
            if (const document::Rgb565Frame* picture =
                    dialog.currentPicture(dmdPrefersRows64())) {
                return *picture;
            }
            return std::nullopt;
        };
        dialog.exec();
        m_dmdPictureOverride = nullptr;
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
        statusBar()->showMessage(tr("No project loaded"), 2500);
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
        tr("Export .cROMc"),
        proposedDirectory.absoluteFilePath(rom + QStringLiteral(".cROMc")),
        tr("Serum colorization for machines (*.cROMc);;All files (*)"));
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
            runProjectTask(this, tr("Exporting .cROMc"), progress, [&]() {
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
        statusBar()->showMessage(tr("Export cancelled"), 3000);
        return;
    }
    if (!result.ok) {
        QMessageBox::critical(this,
                              tr("Export .cROMc Failed"),
                              QString::fromStdString(result.error));
        return;
    }
    QStringList sidecars;
    for (const std::string& sidecar : result.sidecars) {
        sidecars << QString::fromStdString(sidecar);
    }
    QString note;
    if (QFileInfo(target).completeBaseName().compare(rom, Qt::CaseInsensitive) != 0) {
        note = tr(". libserum looks for %1.cROMc").arg(rom);
    }
    statusBar()->showMessage(
        tr("Exported %1%2%3")
            .arg(QDir::toNativeSeparators(target),
                 sidecars.isEmpty() ? QString()
                                    : tr(" with %1").arg(sidecars.join(
                                          tr(" and "))),
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
        statusBar()->showMessage(tr("No frame selected"), 2500);
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
            tr("Frame %1 is not exported: %2")
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
        tr("Export Current Frame PNG"),
        defaultPath,
        tr("PNG image (*.png);;All files (*)"));
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
                              tr("Export Failed"),
                              tr("Could not write PNG file."));
        return;
    }
    statusBar()->showMessage(tr("Exported frame PNG as libserum renders it, %1 rows")
                                 .arg(plane->image.size.height),
                             3000);
}

void MainWindow::importImageIntoSelectedFrames()
{
    if (!m_document || selectedFrameIndices().empty()) {
        statusBar()->showMessage(tr("Select at least one frame first"), 2500);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Import Image Into Selected Frames"),
        m_currentCromPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentCromPath).absolutePath(),
        tr("Images (*.png *.bmp *.jpg *.jpeg);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image(path);
    if (image.isNull()) {
        QMessageBox::critical(this, tr("Import Failed"), tr("Could not read image file."));
        return;
    }
    importPicture(image, QFileInfo(path).fileName(), nullptr, false);
}

void MainWindow::importImageFromClipboard()
{
    if (!m_document || selectedFrameIndices().empty()) {
        statusBar()->showMessage(tr("Select at least one frame first"), 2500);
        return;
    }
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull()) {
        statusBar()->showMessage(tr("The clipboard holds no picture"), 3000);
        return;
    }
    importPicture(image, tr("the clipboard"), nullptr,
                  activePaintTarget() == PaintTarget::Background);
}

void MainWindow::importMovieIntoSelectedFrames()
{
    if (!m_document || selectedFrameIndices().empty()) {
        statusBar()->showMessage(tr("Select at least one frame first"), 2500);
        return;
    }
    QString why;
    if (!VideoClip::isAvailable(&why)) {
        QMessageBox::warning(this, tr("Import Movie Clip"),
                             tr("Movie clips cannot be read: %1").arg(why));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Import Movie Clip Into Selected Frames"),
        m_currentCromPath.isEmpty() ? QDir::homePath()
                                    : QFileInfo(m_currentCromPath).absolutePath(),
        tr("Movies (*.mp4 *.m4v *.mov *.mkv *.avi *.webm *.mpg *.mpeg *.wmv *.flv *.gif);;"
           "All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    VideoClip clip;
    QString error;
    if (!clip.open(path, &error)) {
        QMessageBox::critical(this, tr("Import Failed"), error);
        return;
    }
    importPicture(QImage(), QFileInfo(path).fileName(), &clip, false);
}

void MainWindow::importPicture(const QImage& still,
                               const QString& name,
                               VideoClip* clip,
                               bool toBackground)
{
    const std::vector<std::size_t> frameIndices = selectedFrameIndices();
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || (frameIndices.empty() && !backgroundIndex)) {
        statusBar()->showMessage(tr("Select at least one frame first"), 2500);
        return;
    }
    const render::ProjectGeometry& geometry = m_document->geometry();
    // The planes: the SD and the HD plane of an ordinary project, the one plane of the others.
    std::vector<render::Resolution> planes;
    for (render::Resolution plane : {render::Resolution::Sd, render::Resolution::Hd}) {
        if (geometry.isEditable(plane)) {
            planes.push_back(plane);
        }
    }
    if (planes.empty()) {
        return;
    }
    // The selection, on the plane it was made on; without one, the whole plane.
    const edit::PlaneSelection* selection = m_canvasEdit.selection();
    const render::Resolution selectionPlane =
        selection && !selection->isEmpty() ? selection->plane()
        : std::find(planes.begin(), planes.end(), m_activeResolution) != planes.end()
            ? m_activeResolution
            : planes.front();
    const render::Size selectionPlaneSize = geometry.planeSize(selectionPlane);
    PlaneArea area;
    area.rect = render::Rect{0, 0, selectionPlaneSize.width, selectionPlaneSize.height};
    if (selection && !selection->isEmpty()) {
        area.rect = *selection->bounds();
        area.mask = selection->mask();
    }

    ImageImportSetup setup;
    setup.selectionSize = render::Size{area.rect.width, area.rect.height};
    if (!area.mask.empty()) {
        for (int y = area.rect.y; y < area.rect.y + area.rect.height; ++y) {
            for (int x = area.rect.x; x < area.rect.x + area.rect.width; ++x) {
                setup.selectionMask.push_back(
                    area.mask[static_cast<std::size_t>(y) * selectionPlaneSize.width +
                              static_cast<std::size_t>(x)]);
            }
        }
    }
    const auto planeName = [&geometry](render::Resolution plane) {
        const render::Size size = geometry.planeSize(plane);
        return QStringLiteral("%1x%2").arg(size.width).arg(size.height);
    };
    setup.firstPlaneName = planeName(planes.front());
    setup.firstPlaneChecked = planes.front() == selectionPlane;
    if (planes.size() > 1) {
        setup.secondPlaneName = planeName(planes.back());
        setup.secondPlaneChecked = planes.back() == selectionPlane;
    }
    if (backgroundIndex) {
        setup.backgroundName = QString::number(*backgroundIndex);
    }
    setup.toBackground = toBackground || frameIndices.empty();
    for (std::size_t frameIndex : frameIndices) {
        setup.durationsMs.push_back(m_document->frameDurationMs(frameIndex));
    }
    setup.savedZones = m_document->imageZones();

    std::optional<ImageImportDialog> dialog;
    if (clip) {
        dialog.emplace(*clip, name, setup, this);
    } else {
        dialog.emplace(still, name, setup, this);
    }
    const bool accepted = dialog->exec() == QDialog::Accepted;
    if (!m_document) {
        return;
    }
    // Zones saved or deleted are the project's, also when nothing is imported.
    if (dialog->savedZonesChanged()) {
        m_document->setImageZones(dialog->savedZones());
        updateProjectHeaderStatus();
    }
    if (!accepted) {
        return;
    }
    std::vector<render::Resolution> targets;
    if (dialog->toFirstPlane()) {
        targets.push_back(planes.front());
    }
    if (dialog->toSecondPlane()) {
        targets.push_back(planes.back());
    }
    if (targets.empty()) {
        statusBar()->showMessage(tr("Nothing imported: no plane was chosen"), 3000);
        return;
    }
    const ImageAdjustments adjustments = dialog->adjustments();
    const ImageScaleFilter filter = dialog->filter();
    // The picture of the n-th target, adjusted; null past a clip's end.
    const std::vector<double> times = dialog->frameTimes();
    const auto pictureFor = [&](std::size_t index) -> QImage {
        if (!clip) {
            return dialog->adjustedPicture();
        }
        const QImage picture =
            clip->frameAt(index < times.size() ? times[index] : dialog->startMs());
        return picture.isNull() ? QImage() : adjustedImage(picture, adjustments);
    };
    const auto changesFor = [&](const document::Rgb565Frame& plane, render::Resolution resolution,
                                const QImage& picture, const QRect& zone) {
        const PlaneArea onPlane =
            areaOnPlane(area, selectionPlaneSize, geometry.planeSize(resolution));
        const QImage scaled = zonePicture(
            picture, zone, QSize(onPlane.rect.width, onPlane.rect.height), filter);
        return importedPixelChanges(plane, scaled, onPlane.rect, onPlane.mask);
    };

    if (dialog->toBackground() && backgroundIndex) {
        const QImage picture = pictureFor(0);
        int planesChanged = 0;
        try {
            for (render::Resolution resolution : targets) {
                if (resolution == render::Resolution::Hd &&
                    !m_document->background(*backgroundIndex).colorizedHd) {
                    // An HD image first, the SD image doubled, as Create HD makes it.
                    const document::Rgb565Frame& sd =
                        m_document->backgroundFrame(*backgroundIndex, render::Resolution::Sd);
                    const render::Size hdSize = geometry.planeSize(render::Resolution::Hd);
                    document::Rgb565Frame hd = document::makeRgb565Frame(hdSize);
                    for (int y = 0; y < hdSize.height; ++y) {
                        for (int x = 0; x < hdSize.width; ++x) {
                            hd.pixels[static_cast<std::size_t>(y) * hdSize.width + x] =
                                sd.pixels[static_cast<std::size_t>(y / 2) * sd.size.width + x / 2];
                        }
                    }
                    commitEdit(std::make_unique<edit::BackgroundHdCreateCommand>(*backgroundIndex,
                                                                                 std::move(hd)));
                }
                std::vector<edit::PixelChange> changes =
                    changesFor(m_document->backgroundFrame(*backgroundIndex, resolution),
                               resolution, picture, dialog->zoneFor(0, 1));
                if (!changes.empty()) {
                    commitEdit(std::make_unique<edit::PixelEditCommand>(
                        edit::EditKind::BackgroundImage, *backgroundIndex, resolution,
                        std::move(changes)));
                    ++planesChanged;
                }
            }
        } catch (const std::exception& ex) {
            statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        }
        if (m_preview) {
            m_preview->update();
        }
        refreshComponentBrowsers();
        updateBackgroundControls();
        updateUndoRedoActions();
        updateProjectHeaderStatus();
        statusBar()->showMessage(planesChanged > 0
                                     ? tr("Imported into background %1").arg(*backgroundIndex)
                                     : tr("Background %1 already has this picture")
                                           .arg(*backgroundIndex),
                                 4000);
        return;
    }

    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    int updatedFrames = 0;
    int createdHd = 0;
    int pastEnd = 0;
    QProgressDialog progress(tr("Importing..."), tr("Cancel"), 0,
                             static_cast<int>(frameIndices.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    try {
        for (std::size_t index = 0; index < frameIndices.size(); ++index) {
            progress.setValue(static_cast<int>(index));
            if (progress.wasCanceled()) {
                break;
            }
            const std::size_t frameIndex = frameIndices[index];
            const QImage picture = pictureFor(index);
            if (picture.isNull()) {
                // Past the clip's end: the frames from here keep their pictures.
                pastEnd = static_cast<int>(frameIndices.size() - index);
                break;
            }
            const QRect zone = dialog->zoneFor(index, frameIndices.size());
            bool changed = false;
            for (render::Resolution resolution : targets) {
                if (resolution == render::Resolution::Hd && planes.size() > 1 &&
                    !m_document->hasHdFrame(frameIndex)) {
                    // The Win32 editor's "copy to extra resolution (create if needed)".
                    document::FrameHdParts parts;
                    parts.backgroundMask = m_document->backgroundData(frameIndex).has_value();
                    commands.push_back(edit::createFrameHd(*m_document, frameIndex, parts));
                    ++createdHd;
                }
                std::vector<edit::PixelChange> changes =
                    changesFor(m_document->colorizedFrame(frameIndex, resolution), resolution,
                               picture, zone);
                if (changes.empty()) {
                    continue;
                }
                auto command = std::make_unique<edit::PixelEditCommand>(
                    edit::EditKind::FramePixels, frameIndex, resolution, std::move(changes));
                command->apply(*m_document);
                commands.push_back(std::move(command));
                changed = true;
            }
            updatedFrames += changed ? 1 : 0;
        }
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
    progress.setValue(static_cast<int>(frameIndices.size()));
    recordFrameEdits(std::move(commands));
    updateFrameMetadataControls();
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    QStringList notes;
    if (createdHd > 0) {
        notes << tr("%n HD frame(s) created", "", createdHd);
    }
    if (pastEnd > 0) {
        notes << tr("%1 after the clip's end left as they are").arg(pastEnd);
    }
    statusBar()->showMessage(
        tr("Imported into %n frame(s)", "", updatedFrames) +
            (notes.isEmpty() ? QString()
                             : QStringLiteral(" (%1)").arg(notes.join(QStringLiteral("; ")))),
        5000);
}

void MainWindow::loadProject(const QString& cromPath, const QString& crpPath)
{
    if (!isAutosavePath(cromPath)) {
        const AutosaveFiles autosave = autosaveFilesFor(cromPath, crpPath);
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
            question.setWindowTitle(tr("Open Project"));
            question.setText(tr("The project has an autosave from %1 that is newer "
                                "than its files.")
                                 .arg(QLocale().toString(*autosaved, QLocale::ShortFormat)));
            question.setInformativeText(
                tr("It holds changes that were not saved, for example after a "
                   "crash. Open Autosave opens it as this project with unsaved "
                   "changes: Save then writes them to the project files. %1 and "
                   "%2 are kept either way.")
                    .arg(QFileInfo(autosave.cromPath).fileName(),
                         QFileInfo(autosave.crpPath).fileName()));
            QPushButton* autosaveButton =
                question.addButton(tr("Open Autosave"), QMessageBox::AcceptRole);
            QPushButton* projectButton =
                question.addButton(tr("Open Project"), QMessageBox::RejectRole);
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
            runProjectTask(this, tr("Opening Project"), progress, [&]() {
                result = legacy::loadLegacyProjectDocument(paths, &progress);
            });
        if (failure) {
            result = {};
            result.error.message = *failure;
        }
    }

    if (result.cancelled) {
        setStatusText(tr("Open cancelled"));
        return;
    }
    if (!result.ok) {
        QMessageBox::critical(this,
                              tr("Open Project Failed"),
                              QString::fromStdString(result.error.message));
        setStatusText(tr("Open failed"));
        return;
    }

    // A project of the Win32 editor (a .cRP) is saved with a .cRPX beside its .cROM.
    const bool win32Project = !crpPath.isEmpty() && !isCrpxPath(crpPath);
    QString sidecarNote;
    try {
        m_document = std::move(*result.document);
        result.document.reset();
        // The project is saved as its .cRZ, also when it was opened from a .cROM.
        m_currentCromPath = isCrzPath(cromPath)
                                ? cromPath
                                : siblingProjectFile(cromPath, QStringLiteral("cRZ"));
        m_currentCrpPath = win32Project ? editorFileFor(m_currentCromPath) : crpPath;
        if (!cromPath.isEmpty() && !crpPath.isEmpty()) {
            rememberRecentProject(cromPath, crpPath);
        }
        sidecarNote = importColorizationSidecars();
        if (recovered) {
            // What the autosave holds is not in the project files.
            m_document->markUnsaved();
        }
        m_undoHistories.clear();
        m_canvasHistory.clear();
        updateHistoryButtons();
        startProjectClock();
        updateRuntimeSceneFile();
        m_pasteMode.clearClipboard();
        updateUndoRedoActions();
        if (m_saveAction) {
            m_saveAction->setEnabled(true);
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this,
                              tr("Open Project Failed"),
                              QString::fromUtf8(ex.what()));
        setStatusText(tr("Open failed"));
        return;
    }

    refreshProjectSummary();
    if (win32Project) {
        const QString note = tr("A project of the Win32 editor: once saved here, the Win32 "
                                "editor cannot open it any more");
        statusBar()->showMessage(sidecarNote.isEmpty() ? note
                                                       : QStringLiteral("%1. %2").arg(sidecarNote,
                                                                                      note),
                                 12000);
    } else if (!sidecarNote.isEmpty()) {
        statusBar()->showMessage(sidecarNote, 8000);
    }
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
    job.snapshot.editorMetadata.timeElapsedMs = projectTimeMs();
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
        statusBar()->showMessage(tr("Autosave failed: %1")
                                     .arg(QString::fromStdString(result.error.message)),
                                 8000);
        return;
    }
    m_autosavedRevision = revision;
    statusBar()->showMessage(tr("Autosaved to %1 at %2")
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
    question.setWindowTitle(tr("Autosave Found"));
    question.setText(tr("A new project of %1 that was never saved has an autosave "
                        "from %2.")
                         .arg(name, QLocale().toString(crom.lastModified(),
                                                       QLocale::ShortFormat)));
    question.setInformativeText(
        tr("Open opens it as a new project with unsaved changes. Delete removes the "
           "autosave. Later keeps it and asks again at the next start.%1")
            .arg(autosaves.size() > 1
                     ? tr(" %1 more autosave(s) of never saved projects are in %2.")
                           .arg(static_cast<int>(autosaves.size() - 1))
                           .arg(QDir::toNativeSeparators(untitledAutosaveDirectory()))
                     : QString()));
    QPushButton* openButton = question.addButton(tr("Open"), QMessageBox::AcceptRole);
    QPushButton* deleteButton =
        question.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    QPushButton* laterButton = question.addButton(tr("Later"), QMessageBox::RejectRole);
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
        markFramesInList({});
        return;
    }
    if (!state.hasFrame) {
        markFramesInList({});
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
    // Outlined in the frame list and ticked on its scroll bar, as the Win32 editor marks them
    // (issue #24).
    markFramesInList(state.sameFrames);
    // The count in the status line is known now as well.
    updateFrameStatusText();
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

void MainWindow::markFramesInList(const std::vector<std::size_t>& frameIndices)
{
    if (!m_frameList) {
        return;
    }
    std::vector<int> rows;
    rows.reserve(frameIndices.size());
    for (const std::size_t frameIndex : frameIndices) {
        // A frame the filter hides has no row.
        if (frameIndex < m_frameRows.size() && m_frameRows[frameIndex] >= 0) {
            rows.push_back(m_frameRows[frameIndex]);
        }
    }
    // The rows follow the frames' order.
    m_frameList->setMarkedRows(std::move(rows));
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
        tr("Same frames of frame %1 counted for %2 mask(s)")
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
    const std::vector<std::size_t> selected = selectedFrameIndices();
    m_preview->setDifferenceOverlay(m_sameFramesPanel->spotMarks() == SpotMarks::Common
                                        ? edit::commonPixels(*m_document, selected)
                                        : edit::differingPixels(*m_document, selected));
}

void MainWindow::deleteSameFrames(SameFrameDeletion deletion)
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex || !m_sameFramesPanel) {
        statusBar()->showMessage(tr("No frame selected"), 2500);
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
            what = tr("the same as frame %1 under its mask and shape mode")
                       .arg(static_cast<qulonglong>(*frameIndex));
            break;
        case SameFrameDeletion::SameAsSelected:
            frames = m_sameFrameFinder.duplicatesOf(*m_document, selected, kept);
            what = tr("the same as one of the %1 selected frame(s) under that "
                      "frame's mask and shape mode")
                       .arg(static_cast<qulonglong>(selected.size()));
            break;
        case SameFrameDeletion::AllDuplicates:
            frames = m_sameFrameFinder.allDuplicates(*m_document, kept);
            what = tr("the same as another frame under that frame's mask and "
                      "shape mode");
            break;
    }
    if (frames.empty()) {
        statusBar()->showMessage(tr("Nothing to delete: no frame is %1%2")
                                     .arg(what, kept.empty() ? QString()
                                                             : tr(" and not selected")),
                                 5000);
        return;
    }
    deleteFrames(frames,
                 tr("%1 frame(s) are %2%3. The runtime cannot tell them apart.")
                     .arg(static_cast<qulonglong>(frames.size()))
                     .arg(what, kept.empty() ? QString()
                                             : tr("; selected frames are kept")));
}

void MainWindow::writeCommonPixelsToMask()
{
    const std::vector<std::size_t> selected = selectedFrameIndices();
    if (!m_document || selected.size() < 2) {
        statusBar()->showMessage(
            tr("Select the frames whose differing pixels the mask is to cover"), 4000);
        return;
    }
    const std::optional<std::size_t> maskIndex = currentComparisonMaskIndex();
    if (!maskIndex) {
        statusBar()->showMessage(
            tr("Assign a comparison mask to the current frame first"), 4000);
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
                tr("Comparison mask %1 covers exactly the differing pixels already")
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
        tr("Comparison mask %1 now covers the %2 pixel(s) that differ among the %3 "
           "selected frames; %4 frame(s) use it")
            .arg(static_cast<int>(*maskIndex))
            .arg(static_cast<qlonglong>(differingCount))
            .arg(static_cast<qulonglong>(selected.size()))
            .arg(static_cast<qulonglong>(m_document->comparisonMaskUseCount(*maskIndex))),
        6000);
}

void MainWindow::writeCommonPixelsToNewMask()
{
    const std::vector<std::size_t> selected = selectedFrameIndices();
    if (!m_document || selected.size() < 2) {
        statusBar()->showMessage(
            tr("Select the frames whose differing pixels the mask is to cover"), 4000);
        return;
    }
    if (m_document->comparisonMaskCount() >= document::kMaxComparisonMasks) {
        statusBar()->showMessage(
            tr("Not added: a project holds at most %1 comparison masks")
                .arg(static_cast<int>(document::kMaxComparisonMasks)),
            4000);
        return;
    }
    const document::IndexedFrame differing = edit::differingPixels(*m_document, selected);
    const auto differingCount =
        std::count_if(differing.pixels.begin(), differing.pixels.end(),
                      [](uint8_t value) { return value != 0; });
    try {
        leaveSpriteCanvas();
        recordStructureEdit(edit::addComparisonMaskForFrames(*m_document, differing, selected));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    const std::size_t maskIndex = m_document->comparisonMaskCount() - 1;
    m_selectedMaskIndex = maskIndex;
    if (m_comparisonMaskOverlayButton && !m_runtimeView.isEnabled()) {
        m_comparisonMaskOverlayButton->setChecked(true);
    }
    refreshComponentBrowsers();
    updateMaskControls();
    updateUndoRedoActions();
    showStructureEditStatus(
        tr("Added comparison mask %1: it covers the %2 pixel(s) that differ among the %3 "
           "selected frames, which use it now")
            .arg(static_cast<int>(maskIndex))
            .arg(static_cast<qlonglong>(differingCount))
            .arg(static_cast<qulonglong>(selected.size())),
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
    const QString maskText = maskId < 0 ? tr("no mask")
                                        : tr("mask %1").arg(maskId);
    if (!first) {
        statusBar()->showMessage(
            tr("No frame with %1 is in the frame list").arg(maskText), 4000);
        return;
    }
    selectFrame(m_frameList->currentRow());
    const int hidden = static_cast<int>(frames.size()) - listed;
    statusBar()->showMessage(
        tr("Selected %1 frame(s) with %2%3")
            .arg(listed)
            .arg(maskText, hidden > 0 ? tr("; %1 more are hidden by the frame "
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
        m_sectionBar->setContent(false, {});
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
        m_sectionBar->setContent(true, entries);
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
            existing ? tr("Rename Section") : tr("New Section"),
            existing ? tr("Name of the section that starts at frame %1:")
                           .arg(static_cast<qulonglong>(*frameIndex))
                     : tr("Name of the section that starts at frame %1 (up to 31 "
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
            existing ? tr("Renamed section %1 to \"%2\"")
                           .arg(static_cast<int>(*existing))
                           .arg(name)
                     : tr("Section \"%1\" starts at frame %2")
                           .arg(name)
                           .arg(static_cast<qulonglong>(*frameIndex)));
    } catch (const std::exception& ex) {
        statusBar()->showMessage(tr("Not named: %1").arg(QString::fromUtf8(ex.what())),
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
    if (!confirmDeletion(tr("Delete Section"),
                         tr("Delete the section \"%1\" this frame is part of?")
                             .arg(name),
                         tr("Its frames stay; they belong to the section before it "
                            "then. The deletion can be undone."))) {
        return;
    }
    try {
        recordSectionsEdit(edit::deleteSection(*m_document, *section),
                           tr("Deleted section \"%1\"").arg(name));
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
            list << tr("%1 (frame %2)")
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
                tr("Moved a section in the list"));
            dialog.setNames(names(), row + offset);
        } catch (const std::exception& ex) {
            statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        }
    };
    dialog.onSortRequested = [this, &dialog, &names]() {
        recordSectionsEdit(edit::sortSectionsByName(*m_document),
                           tr("Sorted the sections by name"));
        dialog.setNames(names(), -1);
    };
    dialog.exec();
}

void MainWindow::showFramesForPlugin(std::size_t current, const std::vector<std::size_t>& selected)
{
    if (!m_frameList || selected.empty()) {
        goToFrame(current);
        return;
    }
    QListWidgetItem* currentItem = frameListItem(current);
    if (!currentItem) {
        goToFrame(current);
        return;
    }
    {
        // Told to the window once, below.
        QSignalBlocker blocker(m_frameList);
        m_frameList->clearSelection();
        m_frameList->setCurrentItem(currentItem, QItemSelectionModel::Select);
        for (std::size_t frame : selected) {
            if (QListWidgetItem* item = frameListItem(frame)) {
                item->setSelected(true);
            }
        }
    }
    m_frameList->scrollToItem(currentItem);
    selectFrame(m_frameList->currentRow());
}

void MainWindow::applyHelperLinesShown()
{
    // The menu and the canvas are made at different times; whichever comes last applies it.
    if (!m_helperLinesShownAction) {
        return;
    }
    const bool shown = m_helperLinesShownAction->isChecked();
    if (m_preview) {
        m_preview->setHelperLinesShown(shown);
    }
    if (m_helperLinesPanel) {
        m_helperLinesPanel->setLinesShown(shown);
    }
}

void MainWindow::refreshHelperLines()
{
    if (m_helperLinesPanel) {
        m_helperLinesPanel->setLines(m_document ? m_document->helperLines()
                                                : std::vector<document::HelperLine>{},
                                     m_document ? &*m_document : nullptr);
    }
    if (m_preview) {
        m_preview->update();
    }
}

void MainWindow::changeHelperLines(std::vector<document::HelperLine> lines, const QString& status)
{
    if (!m_document) {
        return;
    }
    std::unique_ptr<edit::EditCommand> command = edit::setHelperLines(*m_document, std::move(lines));
    if (!command) {
        return;
    }
    recordEdit(std::move(command));
    refreshHelperLines();
    statusBar()->showMessage(status, 3000);
}

void MainWindow::addHelperLine(bool vertical)
{
    if (!m_document) {
        return;
    }
    document::HelperLine line;
    line.vertical = vertical;
    line.plane = m_activeResolution;
    const render::Size plane = m_document->geometry().planeSize(line.plane);
    // Where the mouse last pointed on the canvas, in this plane's pixels; else the middle.
    line.position = static_cast<uint32_t>((vertical ? plane.width : plane.height) / 2);
    if (m_lastCanvasHit) {
        const render::Size from = m_document->geometry().planeSize(m_lastCanvasHit->resolution);
        const int value = vertical ? m_lastCanvasHit->x : m_lastCanvasHit->y;
        const int fromExtent = vertical ? from.width : from.height;
        const int toExtent = vertical ? plane.width : plane.height;
        if (fromExtent > 0) {
            line.position = static_cast<uint32_t>(value * toExtent / fromExtent);
        }
    }
    line.name = tr("Line %1").arg(m_document->helperLines().size() + 1).toStdString();
    HelperLineDialog dialog(line, plane, planeLabel(*m_document, line.plane), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    std::vector<document::HelperLine> lines = m_document->helperLines();
    lines.push_back(dialog.line());
    changeHelperLines(std::move(lines), tr("Added the helper line"));
}

void MainWindow::editHelperLine(std::size_t index)
{
    if (!m_document || index >= m_document->helperLines().size()) {
        return;
    }
    const document::HelperLine& line = m_document->helperLines()[index];
    HelperLineDialog dialog(line, m_document->geometry().planeSize(line.plane),
                            planeLabel(*m_document, line.plane), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    std::vector<document::HelperLine> lines = m_document->helperLines();
    lines[index] = dialog.line();
    changeHelperLines(std::move(lines), tr("Changed the helper line"));
}

void MainWindow::showCanvasPosition(std::optional<render::CanvasHit> hit,
                                    std::optional<render::CanvasHit> dragStart)
{
    if (!m_positionLabel) {
        return;
    }
    if (!hit || !m_document) {
        m_positionLabel->clear();
        return;
    }
    m_lastCanvasHit = hit;
    const QString where =
        hit->region == render::CanvasRegion::OriginalFrame
            ? tr("Original")
            : planeLabel(*m_document, hit->resolution);
    QString text = tr("%1: %2, %3").arg(where).arg(hit->x).arg(hit->y);
    if (dragStart && dragStart->region == hit->region &&
        dragStart->resolution == hit->resolution) {
        text += tr(" | from %1, %2: %3x%4")
                    .arg(dragStart->x)
                    .arg(dragStart->y)
                    .arg(std::abs(hit->x - dragStart->x) + 1)
                    .arg(std::abs(hit->y - dragStart->y) + 1);
    }
    m_positionLabel->setText(text);
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
    statusBar()->showMessage(tr("Frame %1 is hidden by the frame filter")
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
        QAction* emptyAction = m_recentProjectsMenu->addAction(tr("No Recent Projects"));
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
    // Another project starts with all its frames: a filter kept from the one before could show
    // none of them (issue #22).
    if (m_frameFilterComboBox) {
        const QSignalBlocker blocker(m_frameFilterComboBox);
        m_frameFilterComboBox->setCurrentIndex(0);
    }
    setSceneTimelineEntry(std::nullopt);
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
    // Another project: no selection, and none to bring back.
    m_canvasEdit.clearSelection(canvasEditContext());
    m_canvasEdit.clearSelectionHistory();
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
    // Another project starts with the whole frame shown.
    m_preview->resetZoom();
    m_preview->setBackgroundLayerEnabled(m_framePanel &&
                                         m_framePanel->isBackgroundLayerChecked());
    m_preview->setSpritesLayerEnabled(!m_framePanel || m_framePanel->isSpritesLayerChecked());
    m_preview->setSpriteOutlinesEnabled(m_spritePanel && m_spritePanel->isOutlinesChecked());
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
        setStatusText(tr("No project loaded"));
        setFrameStatusText(QString());
        if (m_saveAction) {
            m_saveAction->setEnabled(false);
        }
        updateSpriteControls();
        updateRuntimeViewControls();
        refreshHelperLines();
        refreshScenes();
        if (m_pupTesterWindow) {
            m_pupTesterWindow->stop();
            m_pupTesterWindow->setProject(nullptr);
        }
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
    refreshHelperLines();
    refreshScenes();
    if (m_pupTesterWindow) {
        m_pupTesterWindow->stop();
        m_pupTesterWindow->setProject(&*m_document);
    }
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
        item->setSizeHint(kFrameStripCellSize);
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
    // The rows marked for the list before are no longer right.
    updateSameFrameControls();
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
        reasons << tr("no %1 frame").arg(size);
    }
    if (gaps.background) {
        reasons << tr("background %1 has no %2 image")
                       .arg(static_cast<int>(m_document->backgroundId(frameIndex)))
                       .arg(size);
    }
    if (!gaps.sprites.empty()) {
        QStringList ids;
        for (uint16_t spriteId : gaps.sprites) {
            ids << QString::number(static_cast<int>(spriteId));
        }
        reasons << tr("%1 %2 without %3 picture")
                       .arg(ids.size() > 1 ? tr("sprites") : tr("sprite"))
                       .arg(ids.join(QStringLiteral(", ")))
                       .arg(size);
    }
    return reasons.isEmpty() ? QString()
                             : tr("Not rendered at runtime: %1")
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
        lines << tr("Comparison mask %1 does not exist; the frame is compared "
                    "without a mask")
                     .arg(static_cast<int>(m_document->comparisonMaskId(frameIndex)));
    }
    if (problems.background) {
        lines << tr("Background %1 does not exist")
                     .arg(static_cast<int>(m_document->backgroundId(frameIndex)));
    }
    if (problems.sprite) {
        lines << tr("A sprite slot names a sprite that does not exist");
    }
    if (problems.spriteSlotGap) {
        lines << tr("Sprite slot %1 is empty: the sprites after slot %1 are ignored "
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
        lines << inactiveFrameToolTip();
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
    offer(FrameFilter::Hd, tr("HD frames"), 1, showsHdAndSd());
    offer(FrameFilter::HdIncomplete,
          tr("Not rendered"),
          m_frameFilterComboBox->count(),
          isHdOnlyProject());
    bool ambientLighting = false;
    for (std::size_t frame = 0; m_document && !ambientLighting && frame < m_document->frameCount();
         ++frame) {
        ambientLighting = edit::hasAutomaticLights(m_document->frameLights(frame));
    }
    offer(FrameFilter::AmbientLighting, tr("Ambient lighting"), m_frameFilterComboBox->count(),
          ambientLighting);
    offer(FrameFilter::AmbientLightingOutOfDate,
          tr("Ambient lighting out of date"),
          m_frameFilterComboBox->count(),
          ambientLighting);

    // The scenes, at the end: all scene frames and the others. A scene's own frames: "Using the
    // selected asset" with the scene selected in the left sidebar.
    const std::vector<document::SceneDefinition> scenes =
        m_document && m_document->colorizationSettings()
            ? m_document->colorizationSettings()->scenes
            : std::vector<document::SceneDefinition>{};
    const int current = m_frameFilterComboBox->currentData().toInt();
    const QString currentTag = m_frameFilterComboBox->currentData(kFilterTagRole).toString();
    for (int index = m_frameFilterComboBox->count() - 1; index >= 0; --index) {
        const int data = m_frameFilterComboBox->itemData(index).toInt();
        if (data == static_cast<int>(FrameFilter::SceneFrames) ||
            data == static_cast<int>(FrameFilter::WithoutSceneFrames) ||
            data == static_cast<int>(FrameFilter::Tagged)) {
            m_frameFilterComboBox->removeItem(index);
        }
    }
    if (!scenes.empty()) {
        m_frameFilterComboBox->addItem(tr("Scene frames"),
                                       static_cast<int>(FrameFilter::SceneFrames));
        m_frameFilterComboBox->addItem(tr("Without scene frames"),
                                       static_cast<int>(FrameFilter::WithoutSceneFrames));
    }
    // The frames' tags, one entry each, at the very end.
    int restored = -1;
    if (m_document) {
        for (const std::string& tag : m_document->allFrameTags()) {
            const QString name = QString::fromStdString(tag);
            m_frameFilterComboBox->addItem(tr("Tag: %1").arg(name),
                                           static_cast<int>(FrameFilter::Tagged));
            m_frameFilterComboBox->setItemData(m_frameFilterComboBox->count() - 1, name,
                                               kFilterTagRole);
            if (current == static_cast<int>(FrameFilter::Tagged) && name == currentTag) {
                restored = m_frameFilterComboBox->count() - 1;
            }
        }
    }
    if (current != static_cast<int>(FrameFilter::Tagged)) {
        restored = m_frameFilterComboBox->findData(current);
    }
    m_frameFilterComboBox->setCurrentIndex(restored >= 0 ? restored : 0);
    if (current == static_cast<int>(FrameFilter::Tagged) && restored < 0) {
        // The tag shown is on no frame any more: all frames.
        blocker.unblock();
        refreshFrameList(currentFrameIndex());
    }
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
        details << tr("Dyn");
    }
    const std::optional<document::FrameBackgroundData>& backgroundData =
        m_document->backgroundData(frameIndex);
    if (backgroundData && backgroundData->backgroundId < m_document->backgroundCount()) {
        details << tr("Background %1")
                       .arg(static_cast<int>(backgroundData->backgroundId));
    }
    const std::vector<document::FrameSpriteReference>& spriteReferences =
        m_document->frameSpriteReferences(frameIndex);
    const int spriteCount = static_cast<int>(
        std::count_if(spriteReferences.begin(),
                      spriteReferences.end(),
                      [](const document::FrameSpriteReference& reference) {
                          return reference.spriteId != document::kNoSpriteId;
                      }));
    if (spriteCount > 0) {
        details << tr("%1 sprites").arg(spriteCount);
    }
    if (legacy::isFrameSkippedAtRuntime(*m_document, frameIndex)) {
        details << tr("inactive");
    }
    if (const std::optional<document::SceneFrameRef>& scene = m_document->sceneFrame(frameIndex)) {
        const document::SceneDefinition* definition =
            m_document->colorizationSettings()
                ? m_document->colorizationSettings()->scene(scene->sceneId)
                : nullptr;
        details << (definition && definition->frameGroups > 1
                        ? tr("Scene %1 (%2.%3)")
                              .arg(scene->sceneId)
                              .arg(scene->group)
                              .arg(scene->frameIndex + 1)
                        : tr("Scene %1 (%2)").arg(scene->sceneId).arg(scene->frameIndex + 1));
    }
    const document::FrameHdGaps gaps = m_document->frameHdGaps(frameIndex);
    if (gaps.frame) {
        details << noPictureText();
    }
    if (gaps.background) {
        details << tr("Background: %1").arg(noPictureText());
    }
    if (!gaps.sprites.empty()) {
        details << tr("Spr: %1").arg(noPictureText());
    }
    if (m_document->frameReferenceProblems(frameIndex).any()) {
        details << tr("ref. problem");
    }
    for (const std::string& tag : m_document->frameTags(frameIndex)) {
        details << QStringLiteral("#%1").arg(QString::fromStdString(tag));
    }
    QString first = QString::number(static_cast<qulonglong>(frameIndex));
    if (const std::optional<std::size_t> section =
            edit::sectionStartingAt(*m_document, frameIndex)) {
        first += QStringLiteral(" [%1]").arg(
            QString::fromStdString(edit::sectionName(*m_document, *section)));
    }
    // Below the thumbnail in the strip: the number, then the details as one line that wraps.
    return QStringLiteral("%1\n%2").arg(first, details.join(QStringLiteral(" · ")));
}

void MainWindow::refreshComponentBrowsers()
{
    constexpr QSize iconSize{160, 42};
    const bool hdOnly = isHdOnlyProject();
    // The thumbnails stay on the SD picture whichever plane is edited (the only plane in a
    // project with one); the HD badge says which records have HD data.
    const render::Resolution thumbnailPlane =
        hdOnly ? render::Resolution::Hd : render::Resolution::Sd;
    const bool hdBadges = m_document && hasHdAsExtraResolution(&*m_document);
    if (m_maskList) {
        QSignalBlocker blocker(m_maskList);
        m_maskList->clear();
        m_maskIconFrame = currentFrameIndex();
        if (m_document) {
            for (std::size_t maskIndex = 0; maskIndex < m_document->comparisonMaskCount();
                 ++maskIndex) {
                auto* item = new QListWidgetItem(maskIcon(maskIndex),
                                                 QString::number(static_cast<int>(maskIndex)));
                item->setData(Qt::UserRole, static_cast<int>(maskIndex));
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
                    componentIcon(renderBackgroundPreviewImage(background,
                                                               thumbnailPlane,
                                                               m_document->geometry()),
                                  iconSize,
                                  hdBadges && background.colorizedHd.has_value()),
                    QStringLiteral("%1%2")
                        .arg(static_cast<int>(backgroundIndex))
                        .arg(extraDataSuffix(*m_document,
                                             background.colorizedHd.has_value())));
                item->setData(Qt::UserRole, static_cast<int>(backgroundIndex));
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
                    componentIcon(renderSpriteColorizedPreviewImage(sprite,
                                                                    thumbnailPlane,
                                                                    hdOnly),
                                  iconSize,
                                  hdBadges && sprite.hasHd),
                    QStringLiteral("%1%2")
                        .arg(spriteLabel)
                        .arg(extraDataSuffix(*m_document, sprite.hasHd)));
                item->setData(Qt::UserRole, static_cast<int>(spriteIndex));
                m_spriteList->addItem(item);
            }
        }
    }

    if (m_palettePanel) {
        m_palettePanel->setPalettes(m_document ? &*m_document : nullptr);
    }
    if (m_scenesPanel) {
        m_scenesPanel->refreshThumbnails();
    }

    updateMaskBrowserSelection();
    updateBackgroundBrowserSelection();
    updateSpriteBrowserSelection();
    updatePaletteBrowserSelection();
    updateMaskControls();
    updatePaletteControls();
    applyAssetFilter();
}

QIcon MainWindow::maskIcon(std::size_t maskIndex) const
{
    const document::IndexedFrame& mask = m_document->comparisonMask(maskIndex);
    const std::optional<std::size_t> frame = currentFrameIndex();
    if (frame && *frame < m_document->frameCount()) {
        const document::IndexedFrame& original = m_document->originalFrame(*frame);
        if (original.size.width == mask.size.width && original.size.height == mask.size.height) {
            return previewIcon(
                renderMaskOverFrameImage(mask, original, m_document->colorCount()),
                QSize(160, 42));
        }
    }
    return previewIcon(renderMaskPreviewImage(mask), QSize(160, 42));
}

void MainWindow::updateFrameRestViews()
{
    applyAssetFilter();
    if (!m_maskList || !m_document) {
        return;
    }
    const std::optional<std::size_t> frame = currentFrameIndex();
    if (frame == m_maskIconFrame) {
        return;
    }
    m_maskIconFrame = frame;
    for (int row = 0; row < m_maskList->count(); ++row) {
        QListWidgetItem* item = m_maskList->item(row);
        const int maskIndex = item->data(Qt::UserRole).toInt();
        if (maskIndex >= 0 &&
            static_cast<std::size_t>(maskIndex) < m_document->comparisonMaskCount()) {
            item->setIcon(maskIcon(static_cast<std::size_t>(maskIndex)));
        }
    }
}

void MainWindow::applyAssetFilter()
{
    const bool filtered = m_currentFrameAssetsCheckBox &&
                          m_currentFrameAssetsCheckBox->isChecked();
    const std::optional<std::size_t> frame = currentFrameIndex();
    const bool hasFrame = m_document && frame && *frame < m_document->frameCount();
    std::set<int> masks;
    std::set<int> backgrounds;
    std::set<int> sprites;
    if (filtered && hasFrame) {
        masks.insert(m_document->comparisonMaskId(*frame));
        // backgroundId() throws for a frame without background data.
        if (const auto& background = m_document->backgroundData(*frame)) {
            backgrounds.insert(background->backgroundId);
        }
        for (const document::FrameSpriteReference& reference :
             m_document->frameSpriteReferences(*frame)) {
            sprites.insert(reference.spriteId);
        }
    }
    const auto apply = [filtered](QListWidget* list, const std::set<int>& used) {
        if (!list) {
            return;
        }
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem* item = list->item(row);
            item->setHidden(filtered && !used.count(item->data(Qt::UserRole).toInt()));
        }
    };
    apply(m_maskList, masks);
    apply(m_backgroundList, backgrounds);
    apply(m_spriteList, sprites);
    if (m_scenesPanel) {
        const std::optional<document::SceneFrameRef>& scene =
            hasFrame ? m_document->sceneFrame(*frame) : std::nullopt;
        m_scenesPanel->showOnlyScene(filtered && hasFrame,
                                     scene ? std::optional<uint16_t>(scene->sceneId)
                                           : std::nullopt);
    }
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
            item->setIcon(maskIcon(maskIndex));
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

bool MainWindow::ambientLightingOutOfDate(std::size_t frameIndex) const
{
    const document::LightActions& lights = m_document->frameLights(frameIndex);
    if (!edit::hasAutomaticLights(lights)) {
        return false;
    }
    const document::LightActions suggested = edit::suggestedAmbientLighting(composeResolvedFrame(
        *m_document, frameIndex, m_document->geometry().nativePlane(), true));
    return edit::updatedAmbientLighting(lights, suggested) != lights;
}

bool MainWindow::frameMatchesFilter(std::size_t frameIndex) const
{
    if (!m_document || frameIndex >= m_document->frameCount()) {
        return false;
    }
    const int filterData = m_frameFilterComboBox ? m_frameFilterComboBox->currentData().toInt()
                                                 : static_cast<int>(FrameFilter::All);
    const std::optional<document::SceneFrameRef>& scene = m_document->sceneFrame(frameIndex);
    const FrameFilter filter = static_cast<FrameFilter>(filterData);
    if (filter == FrameFilter::Tagged) {
        const std::string tag =
            m_frameFilterComboBox->currentData(kFilterTagRole).toString().toStdString();
        const std::vector<std::string>& tags = m_document->frameTags(frameIndex);
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }
    if (filter == FrameFilter::SceneFrames) {
        return scene.has_value();
    }
    if (filter == FrameFilter::WithoutSceneFrames) {
        return !scene;
    }
    if (filter == FrameFilter::Hd) {
        return m_document->hasHdFrame(frameIndex);
    }
    if (filter == FrameFilter::HdIncomplete) {
        return m_document->frameHdGaps(frameIndex).any();
    }
    if (filter == FrameFilter::Inactive) {
        return legacy::isFrameSkippedAtRuntime(*m_document, frameIndex);
    }
    if (filter == FrameFilter::Triggers) {
        return m_document->triggerId(frameIndex) != document::kNoTriggerId;
    }
    if (filter == FrameFilter::AmbientLighting) {
        return edit::hasAutomaticLights(m_document->frameLights(frameIndex));
    }
    if (filter == FrameFilter::AmbientLightingOutOfDate) {
        return ambientLightingOutOfDate(frameIndex);
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
                               return reference.spriteId != document::kNoSpriteId;
                           });
    }
    if (filter == FrameFilter::Rotations) {
        const auto rotates = [this, frameIndex](render::Resolution resolution) {
            for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
                if (m_document->colorRotationLength(frameIndex, resolution, slot) > 0) {
                    return true;
                }
            }
            return false;
        };
        const render::Resolution native = m_document->geometry().nativePlane();
        if (!m_document->rendersHdPlaneOnly() && rotates(native)) {
            return true;
        }
        return m_document->hasHdFrame(frameIndex) && rotates(render::Resolution::Hd);
    }
    if (filter == FrameFilter::SceneTimeline) {
        return m_sceneTimeline && scene && scene->sceneId == *m_sceneTimeline;
    }
    if (filter != FrameFilter::UsesSelectedAsset) {
        return true;
    }
    QWidget* assetTab = m_leftBrowserTabs ? m_leftBrowserTabs->currentWidget() : nullptr;
    // None used yet: the first open one.
    for (int index = 0; !assetTab && m_leftBrowserTabs && index < m_leftBrowserTabs->count();
         ++index) {
        if (m_leftBrowserTabs->isSectionOpen(index)) {
            assetTab = m_leftBrowserTabs->widget(index);
        }
    }
    if (assetTab && assetTab == m_scenesPanel) {
        // The game's frames that start the scene, as a background scene too: their trigger.
        const std::optional<uint16_t> sceneId = m_scenesPanel->selectedScene();
        return sceneId && edit::startsScene(*m_document, frameIndex, *sceneId);
    }
    if (assetTab && assetTab == m_lightEffectsPanel) {
        const std::optional<uint16_t> effectId = m_lightEffectsPanel->selectedEffect();
        return effectId && m_document->frameUsesLightEffect(frameIndex, *effectId);
    }
    if (assetTab && assetTab == m_maskPage) {
        return m_selectedMaskIndex && *m_selectedMaskIndex < m_document->comparisonMaskCount() &&
               m_document->comparisonMaskId(frameIndex) == *m_selectedMaskIndex;
    }
    if (assetTab && assetTab == m_backgroundPage) {
        const std::optional<std::size_t> background = selectedBackgroundIndex();
        const std::optional<document::FrameBackgroundData>& backgroundData =
            m_document->backgroundData(frameIndex);
        return background && backgroundData && backgroundData->backgroundId == *background;
    }
    if (assetTab && assetTab == m_spritePage) {
        if (m_selectedSpriteIndex < 0 ||
            static_cast<std::size_t>(m_selectedSpriteIndex) >= m_document->spriteCount()) {
            return false;
        }
        const std::vector<document::FrameSpriteReference>& references =
            m_document->frameSpriteReferences(frameIndex);
        return std::any_of(references.begin(), references.end(),
                           [this](const document::FrameSpriteReference& reference) {
                               return reference.spriteId == m_selectedSpriteIndex;
                           });
    }
    return true;
}

void MainWindow::refreshSelectedAssetFilter()
{
    // A scene's timeline or not: Leave Scene follows.
    updateHistoryButtons();
    if (m_frameFilterComboBox && static_cast<FrameFilter>(m_frameFilterComboBox->currentData()
                                                              .toInt()) ==
                                     FrameFilter::UsesSelectedAsset) {
        refreshFrameListItems();
    }
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
    // The canvas selection stays on another frame, as in the Win32 editor: it can be checked
    // on each frame of a selection before copying.
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
    if (m_pupTesterWindow) {
        const uint32_t trigger = m_document->triggerId(*frameIndex);
        m_pupTesterWindow->frameShown(trigger == document::kNoTriggerId
                                          ? std::nullopt
                                          : std::optional<uint32_t>(trigger));
    }
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
            tr("Frame %1 | Runtime view, %2 rows | editing off%3%4")
                .arg(static_cast<int>(*frameIndex))
                .arg(m_runtimeView.rowsFor(m_document->geometry()) ==
                             runtime::PlaneRequest::Rows64
                         ? 64
                         : 32)
                .arg(inactive ? tr(" | inactive") : QString())
                .arg(hint.isEmpty() ? QString() : QStringLiteral(" | %1").arg(hint)));
        QStringList toolTip{
            tr("The canvas shows libserum's output. Switch the Runtime view off "
               "in the View menu or the toolbar to edit.")};
        if (inactive) {
            toolTip << inactiveFrameToolTip();
        }
        if (!hint.isEmpty()) {
            toolTip << tr("%1. libserum identifies a ROM frame by order: it "
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
                                      : tr("HD unavailable"))
                       : nativePlaneText(*m_document);
    // How many frames the runtime cannot tell from this one, as the Win32 editor shows it
    // beside the frame; known once the frame has come to rest (m_frameRestTimer) or the
    // Comparison Mask section has asked.
    const std::optional<std::size_t> sameFrames = m_sameFrameFinder.knownSameFrameCount(
        *m_document, *frameIndex, edit::comparisonRuleOf(*m_document, *frameIndex));
    // An inactive frame is marked in the frame list and explained in the tool tip here; the
    // line itself stays as short as it was.
    setFrameStatusText(tr("Frame %1 | %2 | %3 ms%4%5")
                           .arg(static_cast<int>(*frameIndex))
                           .arg(planes)
                           .arg(m_document->frameDurationMs(*frameIndex))
                           .arg(selectedCount > 1
                                    ? tr(" | %1 selected").arg(selectedCount)
                                    : QString())
                           .arg(gapsText.isEmpty() ? QString()
                                                   : tr(" | not rendered")) +
                       (sameFrames ? tr(" | %1 same").arg(*sameFrames) : QString()));
    QStringList toolTip;
    if (sameFrames) {
        toolTip << tr("%1 frames are the same as this one under its comparison mask and shape "
                      "mode: the runtime cannot tell them apart. The Comparison Mask section "
                      "lists them.")
                       .arg(*sameFrames);
    }
    if (!gapsText.isEmpty()) {
        toolTip << QStringLiteral("%1.").arg(gapsText);
    }
    if (inactive) {
        toolTip << inactiveFrameToolTip();
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
        m_playbackButton->setText(running ? tr("Pause") : tr("Play"));
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
    // Watching the playback is working on the project.
    noteActivity();
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
        dynamicZonesHere ? tr("Show dynamic mask overlay")
                         : tr("Dynamic zones are defined on the 128x32 plane: "
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
            tr("Dynamic mode is off in HD: dynamic zones are defined on the "
               "128x32 plane"),
            4000);
    }
    updateCanvasModeButtons();
    if (m_dynamicPanel) {
        m_dynamicPanel->setNote(
            isDynamicPlaneActive()
                ? QString()
                : tr("Dynamic zones are defined on the 128x32 plane. The colors "
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
    // The left sidebar follows the frame once it has come to rest.
    m_frameRestTimer.start();
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
    updateFrameLightControls();
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
                    tr("Background %1%2")
                        .arg(static_cast<int>(backgroundIndex))
                        .arg(extraDataSuffix(*m_document,
                                             background.colorizedHd.has_value())),
                    static_cast<int>(backgroundIndex));
            }
        }

        if (m_backgroundComboBox->count() == 0) {
            m_backgroundComboBox->addItem(tr("No backgrounds"), -1);
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
    const std::vector<std::size_t> selectedFrames = selectedFrameIndices();
    state.hasSelectedFrames = !selectedFrames.empty();
    state.editing = isBackgroundImageTargetActive();
    state.selectedFramesHaveBackground =
        m_document && std::any_of(selectedFrames.begin(), selectedFrames.end(),
                                  [this](std::size_t frame) {
                                      return frame < m_document->frameCount() &&
                                             m_document->backgroundData(frame) &&
                                             m_document->backgroundData(frame)->backgroundId !=
                                                 document::kNoBackgroundId;
                                  });
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
    uint32_t value,
    const std::function<bool(std::size_t frameIndex)>& skip)
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
            if ((attribute == edit::FrameAttribute::BackgroundId &&
                 !m_document->backgroundData(frameIndex)) ||
                (skip && skip(frameIndex))) {
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
            pushToHistory(key, std::move(command));
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
            maskId < 0 ? tr("Cleared comparison mask assignment on %1")
                             .arg(frameEditCountText(count.changed, count.skipped))
                       : tr("Assigned comparison mask %1 on %2")
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
        statusBar()->showMessage(tr("No comparison mask selected"), 2500);
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
        statusBar()->showMessage(tr("Assign a comparison mask before editing"), 2500);
        updateMaskControls();
        return;
    }
    leaveSpriteCanvas();
    if (m_comparisonMaskOverlayButton) {
        m_comparisonMaskOverlayButton->setChecked(true);
    }
    updateUndoRedoActions();
    statusBar()->showMessage(
        tr("Editing comparison mask %1").arg(static_cast<int>(*maskIndex)),
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
            (mode == 0 ? tr("Set full comparison mode on %1")
                       : tr("Set shape mode on %1"))
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
                                                     tr("no background data"));
        statusBar()->showMessage(
            backgroundId < 0
                ? tr("Cleared background assignment on %1").arg(countText)
                : tr("Assigned background %1 on %2").arg(backgroundId).arg(countText),
            2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setSelectedFramesTrigger(document::TriggerKind kind, uint32_t triggerId)
{
    if (!m_document || selectedFrameIndices().empty()) {
        return;
    }
    const bool clearing = triggerId == document::kNoTriggerId;
    try {
        const FrameEditCount count = editSelectedFramesAttribute(
            edit::FrameAttribute::TriggerId, triggerId, [this, kind, clearing](std::size_t frame) {
                const document::TriggerKind has = document::triggerKind(m_document->triggerId(frame));
                // Clearing touches the frames with a trigger of this kind; setting leaves the
                // frames with a trigger of another kind alone (an ID of no kind is replaced).
                return clearing ? has != kind
                                : has != kind && has != document::TriggerKind::None &&
                                      has != document::TriggerKind::Other;
            });
        const QString countText = frameEditCountText(count.changed, count.skipped);
        QString message;
        switch (kind) {
            case document::TriggerKind::Pup:
                message = clearing ? tr("Cleared the PUP trigger on %1").arg(countText)
                                   : tr("Set PUP trigger %1 on %2").arg(triggerId).arg(countText);
                break;
            case document::TriggerKind::Scene:
                message = clearing ? tr("Cleared the scene on %1").arg(countText)
                                   : tr("Set scene %1 on %2").arg(triggerId).arg(countText);
                break;
            case document::TriggerKind::Monochrome:
                message = clearing ? tr("Disabled the monochrome fallback on %1").arg(countText)
                                   : tr("Enabled the monochrome fallback on %1").arg(countText);
                break;
            case document::TriggerKind::PaletteMonochrome:
                message = clearing
                              ? tr("Disabled the palette-based monochrome fallback on %1")
                                    .arg(countText)
                              : tr("Enabled the palette-based monochrome fallback on %1")
                                    .arg(countText);
                break;
            default:
                break;
        }
        if (count.changed == 0) {
            updateFrameMetadataControls();
        }
        statusBar()->showMessage(message, 2500);
    } catch (const std::exception& ex) {
        updateFrameMetadataControls();
        updateUndoRedoActions();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setSelectedFramesTags(const QString& text)
{
    const std::optional<std::size_t> current = currentFrameIndex();
    if (!m_document || !current) {
        return;
    }
    const std::vector<std::string> before = m_document->frameTags(*current);
    const std::vector<std::string> after = edit::parseFrameTags(text.toStdString());
    std::vector<std::size_t> changed;
    for (std::size_t frameIndex : selectedFrameIndices()) {
        std::vector<std::string> tags =
            edit::retaggedFrame(m_document->frameTags(frameIndex), before, after);
        if (tags == m_document->frameTags(frameIndex)) {
            continue;
        }
        auto command = std::make_unique<edit::FrameTagsCommand>(
            frameIndex, m_document->frameTags(frameIndex), std::move(tags));
        command->apply(*m_document);
        const edit::HistoryKey key = edit::historyKey(command->target());
        pushToHistory(key, std::move(command));
        changed.push_back(frameIndex);
    }
    updateFrameListItems(changed);
    updateFrameFilterChoices();
    updateFrameMetadataControls();
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(
        tr("Tags of %n frame(s) changed", "", static_cast<int>(changed.size())), 2500);
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
            tr("Set duration %1 ms on %2")
                .arg(storedDurationMs)
                .arg(frameEditCountText(count.changed, count.skipped)),
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
        statusBar()->showMessage(tr("No background selected"), 2500);
        updateBackgroundControls();
        return;
    }
    setCurrentFrameBackgroundId(backgroundIndex);
}

void MainWindow::editFrameInsteadOfBackground()
{
    setPaintTarget(PaintTarget::Frame);
    updatePaintTargetMode();
    updateBackgroundControls();
    statusBar()->showMessage(tr("Editing the frame"), 1500);
}

void MainWindow::editAssignedBackground(bool fromFrame)
{
    if (!m_framePanel) {
        return;
    }
    if (fromFrame && activePaintTarget() == PaintTarget::Frame && !spriteCanvasShown()) {
        // From the frame: its own background, not the one selected for something earlier.
        if (const std::optional<std::size_t> assigned = currentBackgroundIndex()) {
            const int comboIndex = m_backgroundComboBox->findData(static_cast<int>(*assigned));
            if (comboIndex >= 0) {
                m_backgroundComboBox->setCurrentIndex(comboIndex);
            }
        }
    }
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!backgroundIndex) {
        statusBar()->showMessage(tr("Select a background before editing"), 2500);
        updateBackgroundControls();
        return;
    }
    if (m_activeResolution == render::Resolution::Hd &&
        !m_document->background(*backgroundIndex).colorizedHd) {
        if (isHdOnlyProject()) {
            statusBar()->showMessage(
                tr("Background %1 has no %2 image yet: import an image, or use "
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
    updateBackgroundControls();
    statusBar()->showMessage(
        tr("Editing background %1; Edit Background again goes back to the frame")
            .arg(static_cast<int>(*backgroundIndex)),
        4000);
}

void MainWindow::copyCurrentFrameToSelectedBackground()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !frameIndex || !backgroundIndex) {
        statusBar()->showMessage(tr("Select a frame and background first"), 2500);
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
            tr("The frame and the background both need their %1 data first")
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
            statusBar()->showMessage(tr("Background already matches frame"), 2500);
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
            tr("Copied frame %1 to background %2 (%3)")
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
        statusBar()->showMessage(tr("Select a background and frames first"), 2500);
        updateBackgroundControls();
        return;
    }

    const document::BackgroundRecord& background = m_document->background(*backgroundIndex);
    render::Resolution sourceResolution = m_activeResolution;
    if (sourceResolution == render::Resolution::Hd && !background.colorizedHd) {
        if (isHdOnlyProject()) {
            statusBar()->showMessage(tr("The background has no %1 image yet")
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
            statusBar()->showMessage(tr("No selected frame changed"), 2500);
            return;
        }
        updateUndoRedoActions();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            tr("Copied background %1 to %2 frame(s) (%3)")
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
    if (!m_document || !selectedBackgroundIndex()) {
        statusBar()->showMessage(tr("Select a background first"), 2500);
        updateBackgroundControls();
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Background Image"),
        m_currentCromPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentCromPath).absolutePath(),
        tr("Images (*.png *.bmp *.jpg *.jpeg);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    const QImage image(path);
    if (image.isNull()) {
        QMessageBox::critical(this, tr("Import Failed"), tr("Could not read image file."));
        return;
    }
    importPicture(image, QFileInfo(path).fileName(), nullptr, true);
}

void MainWindow::exportSelectedBackgroundPng()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !backgroundIndex) {
        statusBar()->showMessage(tr("Select a background first"), 2500);
        return;
    }

    const document::BackgroundRecord& background = m_document->background(*backgroundIndex);
    render::Resolution exportResolution = m_activeResolution;
    if (exportResolution == render::Resolution::Hd && !background.colorizedHd) {
        if (isHdOnlyProject()) {
            statusBar()->showMessage(tr("The background has no %1 image yet")
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
        tr("Export Background PNG"),
        defaultPath,
        tr("PNG image (*.png);;All files (*)"));
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
                              tr("Export Failed"),
                              tr("Could not write PNG file."));
        return;
    }
    statusBar()->showMessage(tr("Exported background PNG"), 3000);
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
    if (!confirmPassingSpriteWarning(frameIndices, *slotIndex)) {
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
            reference.spriteId = static_cast<uint16_t>(spriteIndex);
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
            tr("Assigned sprite %1 to slot %2 on %3 frame(s)%4%5")
                .arg(spriteIndex)
                .arg(static_cast<int>(*slotIndex))
                .arg(assigned)
                .arg(moved > 0 ? tr(" (on %1 of them to the first empty slot, "
                                    "before which no slot may stay empty)")
                                     .arg(moved)
                               : QString())
                .arg(isHdOnlyProject() &&
                             !m_document->hasHdSprite(static_cast<std::size_t>(spriteIndex))
                         ? tr(". The sprite has no %1 picture: these frames are not "
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
        statusBar()->showMessage(tr("Select a frame first"), 2500);
        updateSpriteControls();
        return;
    }

    const std::optional<std::size_t> slotIndex = m_spritePanel->selectedSlotIndex();
    if (!slotIndex) {
        statusBar()->showMessage(tr("Select a sprite slot first"), 2500);
        updateSpriteControls();
        return;
    }

    // Bounds, selection and the captured pixels are original frame pixels. The colors come
    // from the native plane, which lines up with the original pixel for pixel: the HD frame of
    // an HD-only project.
    const bool hdOnly = isHdOnlyProject();
    if (hdOnly && !m_document->hasHdFrame(*frameIndex)) {
        statusBar()->showMessage(
            tr("Frame %1 has no %2 frame to capture colors from: use Create %2 "
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
        statusBar()->showMessage(tr("Sprite capture bounds are invalid"), 2500);
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
        statusBar()->showMessage(tr("Sprite capture selection is invalid"), 2500);
        updateSpriteControls();
        return;
    }

    if (!confirmPassingSpriteWarning({*frameIndex}, *slotIndex)) {
        return;
    }

    QMessageBox confirm(this);
    confirm.setWindowTitle(tr("Capture Sprite"));
    confirm.setText(
        selectedPixels.empty()
            ? tr("Capture frame region [%1,%2-%3,%4]?")
                  .arg(captureMinX)
                  .arg(captureMinY)
                  .arg(captureMaxX)
                  .arg(captureMaxY)
            : tr("Capture %1 selected pixel(s) from [%2,%3-%4,%5]?")
                  .arg(static_cast<int>(selectedPixels.size()))
                  .arg(captureMinX)
                  .arg(captureMinY)
                  .arg(captureMaxX)
                  .arg(captureMaxY));
    confirm.setInformativeText(
        spriteIndex ? tr("Replace sprite %1 with the captured pixels, or create a new "
                         "sprite from them.")
                          .arg(static_cast<int>(*spriteIndex))
                    : tr("Create a new sprite from the captured pixels."));
    QPushButton* replaceButton =
        confirm.addButton(tr("Replace Current"), QMessageBox::AcceptRole);
    replaceButton->setEnabled(spriteIndex.has_value());
    QPushButton* createButton =
        confirm.addButton(tr("Create New"), QMessageBox::ActionRole);
    createButton->setEnabled(m_document->spriteCount() < m_document->limits().maxSprites);
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
                                                    static_cast<uint16_t>(*spriteIndex),
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
            tr("Captured frame region [%1,%2-%3,%4] into sprite %5")
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
    const uint16_t spriteId = currentReferences[*slotIndex].spriteId;
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
            tr("Removed sprite %1 from %2")
                .arg(static_cast<int>(spriteId))
                .arg(frameEditCountText(changed, skipped,
                                        tr("without that sprite"))),
            2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setCurrentSpriteSlotBounds(std::optional<SpriteSlotBounds> drawn)
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

    const SpriteSlotBounds bounds = drawn.value_or(m_spritePanel->slotBounds());
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
            if (*slotIndex >= references.size() ||
                references[*slotIndex].spriteId == document::kNoSpriteId) {
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
            if (drawn) {
                const bool assigned = std::any_of(
                    frameIndices.begin(), frameIndices.end(), [&](std::size_t frameIndex) {
                        const auto& slots = m_document->frameSpriteReferences(frameIndex);
                        return *slotIndex < slots.size() &&
                               slots[*slotIndex].spriteId != document::kNoSpriteId;
                    });
                statusBar()->showMessage(
                    assigned ? tr("Sprite slot %1 has these bounds already")
                                   .arg(static_cast<int>(*slotIndex))
                             : tr("Sprite slot %1 is empty: assign a sprite to it first")
                                   .arg(static_cast<int>(*slotIndex)),
                    3000);
            }
            return;
        }
        if (m_preview) {
            m_preview->update();
        }
        updateSpriteControls();
        updateProjectHeaderStatus();
        statusBar()->showMessage(
            tr("Updated sprite slot %1 bounds on %2 frame(s)")
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
        statusBar()->showMessage(tr("Renamed sprite %1").arg(spriteIndex), 1500);
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
            tr("Updated sprite %1 detection area %2")
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
            tr("Cleared sprite %1 detection area %2")
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

    try {
        // Color set replace: each pixel takes the edit color of the sprite original's shade.
        const edit::PixelColorSource colorForPoint =
            m_drawColorMode == edit::DrawColorMode::ColorSetReplace
                ? edit::spriteColorSetReplaceColorSource(*m_document, spriteIndex, resolution)
                : edit::PixelColorSource([color = m_activeDrawColor](render::Point) {
                      return color;
                  });
        const edit::EditLayer layer{edit::EditKind::SpriteImage, spriteIndex, resolution};
        if (!commitPixelChanges(layer,
                                edit::makeSpritePaintChanges(*m_document,
                                                             spriteIndex,
                                                             resolution,
                                                             pixels,
                                                             colorForPoint,
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
        statusBar()->showMessage(tr("Picked sprite color 0x%1")
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
        if (m_drawColorMode != edit::DrawColorMode::ColorSetReplace &&
            m_document->spriteColorizedPixel(spriteIndex, resolution, x, y) == m_activeDrawColor) {
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
    statusBar()->showMessage(tr("Applied sprite edits to previews"), 1000);
}

QComboBox* MainWindow::addUpscaleFilterRow(QDialog& dialog, QVBoxLayout& layout) const
{
    auto* filterRow = new QHBoxLayout();
    filterRow->addWidget(new QLabel(tr("Filter"), &dialog));
    auto* filterComboBox = new QComboBox(&dialog);
    filterComboBox->setToolTip(tr(
        "How the SD picture becomes twice as large: Nearest doubles every pixel, Scale2x keeps "
        "the colors and smooths diagonal edges, the others blend neighbouring colors"));
    for (document::UpscaleFilter filter :
         {document::UpscaleFilter::Nearest, document::UpscaleFilter::Scale2x,
          document::UpscaleFilter::Bilinear, document::UpscaleFilter::Bicubic,
          document::UpscaleFilter::Lanczos4}) {
        filterComboBox->addItem(QString::fromStdString(document::upscaleFilterName(filter)),
                                static_cast<int>(filter));
    }
    filterComboBox->setCurrentIndex(
        std::max(0, filterComboBox->findData(static_cast<int>(m_createHdFilter))));
    filterRow->addWidget(filterComboBox, 1);
    layout.addLayout(filterRow);
    return filterComboBox;
}

bool MainWindow::askUpscaleFilter(const QString& title, const QString& text)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* dialogLayout = new QVBoxLayout(&dialog);
    auto* description = new QLabel(text, &dialog);
    description->setWordWrap(true);
    dialogLayout->addWidget(description);
    QComboBox* filterComboBox = addUpscaleFilterRow(dialog, *dialogLayout);
    auto* buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    dialogLayout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    m_createHdFilter = static_cast<document::UpscaleFilter>(filterComboBox->currentData().toInt());
    return true;
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
    // An HD-only project takes the HD sprite from the sprite original: nothing is scaled.
    if (!isHdOnlyProject() &&
        !askUpscaleFilter(tr("Create HD Sprite"),
                          tr("Create the HD sprite of sprite %1 from its SD colors; the mask "
                             "is doubled.")
                              .arg(*spriteIndex))) {
        return;
    }

    try {
        std::unique_ptr<edit::EditCommand> command =
            edit::createSpriteHd(*m_document, *spriteIndex, m_createHdFilter);
        m_activeResolution = render::Resolution::Hd;
        recordEdit(std::move(command));
        updateUndoRedoActions();
        statusBar()->showMessage(
            isHdOnlyProject()
                ? tr("Created the %1 sprite from the sprite original in its plain "
                     "colors")
                      .arg(nativePlaneText(*m_document))
                : tr("Created HD sprite from SD (%1)")
                      .arg(QString::fromStdString(document::upscaleFilterName(m_createHdFilter))),
            2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::createSelectedBackgroundHd()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !backgroundIndex ||
        m_document->background(*backgroundIndex).colorizedHd || isOriginalPlaneOnlyProject()) {
        updateBackgroundControls();
        return;
    }
    if (!isHdOnlyProject() &&
        !askUpscaleFilter(tr("Create HD Background"),
                          tr("Create the HD image of background %1 from its SD image.")
                              .arg(*backgroundIndex))) {
        return;
    }
    try {
        const render::Size hdSize = m_document->geometry().planeSize(render::Resolution::Hd);
        // From the SD image with the chosen filter; a project with 256x64
        // original frames has no SD image to take it from: black, as Create 256x64 Frame
        // gives a background.
        document::Rgb565Frame hd =
            isHdOnlyProject()
                ? document::makeRgb565Frame(hdSize, 0)
                : document::upscale2x(
                      m_document->backgroundFrame(*backgroundIndex, render::Resolution::Sd),
                      m_createHdFilter);
        commitEdit(std::make_unique<edit::BackgroundHdCreateCommand>(*backgroundIndex,
                                                                     std::move(hd)));
        if (activePaintTarget() == PaintTarget::Background) {
            // The background being edited: show its new HD image, as Create HD Sprite does.
            m_activeResolution = render::Resolution::Hd;
            updateResolutionMode();
        }
        refreshComponentBrowsers();
        updateBackgroundControls();
        updateUndoRedoActions();
        statusBar()->showMessage(
            isHdOnlyProject()
                ? tr("Gave background %1 a black %2 image")
                      .arg(*backgroundIndex)
                      .arg(nativePlaneText(*m_document))
                : tr("Created the HD image of background %1 (%2)")
                      .arg(*backgroundIndex)
                      .arg(QString::fromStdString(document::upscaleFilterName(m_createHdFilter))),
            3000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::removeSelectedBackgroundHd()
{
    const std::optional<std::size_t> backgroundIndex = selectedBackgroundIndex();
    if (!m_document || !backgroundIndex || !m_document->background(*backgroundIndex).colorizedHd ||
        isHdOnlyProject()) {
        updateBackgroundControls();
        return;
    }
    try {
        if (activePaintTarget() == PaintTarget::Background &&
            m_activeResolution == render::Resolution::Hd) {
            // The HD image being edited goes: back to SD.
            m_activeResolution = render::Resolution::Sd;
            updateResolutionMode();
        }
        commitEdit(std::make_unique<edit::BackgroundHdRemoveCommand>(
            *backgroundIndex, *m_document->background(*backgroundIndex).colorizedHd));
        refreshComponentBrowsers();
        updateBackgroundControls();
        updateUndoRedoActions();
        statusBar()->showMessage(tr("Removed the HD image of background %1").arg(*backgroundIndex),
                                 3000);
    } catch (const std::exception& ex) {
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
            tr("The %1 sprite is the only sprite data this project renders; it "
               "cannot be removed")
                .arg(nativePlaneText(*m_document)),
            4000);
        return;
    }

    try {
        recordEdit(edit::removeSpriteHd(*m_document, *spriteIndex));
        updateUndoRedoActions();
        statusBar()->showMessage(tr("Removed HD sprite"), 2500);
    } catch (const std::exception& ex) {
        updateSpriteControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::recordFrameEdits(std::vector<std::unique_ptr<edit::EditCommand>> commands)
{
    // Each in the history of its frame; the views are refreshed once for all of them.
    std::vector<std::size_t> frames;
    const edit::EditCommand* last = nullptr;
    for (std::unique_ptr<edit::EditCommand>& command : commands) {
        if (!command) {
            continue;
        }
        const edit::EditTarget target = command->target();
        frames.insert(frames.end(), target.frameIndices.begin(), target.frameIndices.end());
        last = command.get();
        pushToHistory(edit::historyKey(target), std::move(command));
    }
    if (!last) {
        return;
    }
    refreshAfterEdit(*last);
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    updateFrameListItems(frames);
    updateUndoRedoActions();
}

void MainWindow::createCurrentFrameHd()
{
    if (!m_document || !currentFrameIndex()) {
        return;
    }
    if (isHdOnlyProject()) {
        createSelectedFramesHdFromOriginal();
        return;
    }
    if (isOriginalPlaneOnlyProject()) {
        return;
    }
    // Every selected frame without HD, as the Win32 editor's extra resolution switch does.
    std::vector<std::size_t> targets;
    for (std::size_t frameIndex : selectedFrameIndices()) {
        if (!m_document->hasHdFrame(frameIndex)) {
            targets.push_back(frameIndex);
        }
    }
    if (targets.empty()) {
        updateFrameMetadataControls();
        return;
    }

    bool hasBackgroundData = false;
    bool hasAssignedBackground = false;
    for (std::size_t frameIndex : targets) {
        const std::optional<document::FrameBackgroundData>& backgroundData =
            m_document->backgroundData(frameIndex);
        hasBackgroundData = hasBackgroundData || backgroundData.has_value();
        hasAssignedBackground =
            hasAssignedBackground ||
            (backgroundData && backgroundData->backgroundId < m_document->backgroundCount());
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Create HD Frame"));
    auto* dialogLayout = new QVBoxLayout(&dialog);
    // No dynamic data among them: the runtime renders the dynamic zones of the SD plane into
    // the HD picture and does not read an HD dynamic mask.
    auto* description = new QLabel(
        targets.size() == 1
            ? tr("Create HD frame from SD. Upscale related data?")
            : tr("Create HD frames from SD for %n selected frame(s). Upscale related data?", "",
                 static_cast<int>(targets.size())),
        &dialog);
    description->setWordWrap(true);
    dialogLayout->addWidget(description);
    auto* backgroundMaskCheckBox = new QCheckBox(tr("Background mask"), &dialog);
    backgroundMaskCheckBox->setChecked(hasBackgroundData);
    backgroundMaskCheckBox->setEnabled(hasBackgroundData);
    backgroundMaskCheckBox->setToolTip(tr("Scale SD background mask to HD"));
    dialogLayout->addWidget(backgroundMaskCheckBox);
    auto* backgroundImageCheckBox = new QCheckBox(tr("Assigned background image"), &dialog);
    backgroundImageCheckBox->setChecked(hasAssignedBackground);
    backgroundImageCheckBox->setEnabled(hasAssignedBackground);
    backgroundImageCheckBox->setToolTip(tr("Scale assigned SD background image to HD"));
    dialogLayout->addWidget(backgroundImageCheckBox);
    QComboBox* filterComboBox = addUpscaleFilterRow(dialog, *dialogLayout);
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                           &dialog);
    dialogLayout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_createHdFilter = static_cast<document::UpscaleFilter>(filterComboBox->currentData().toInt());
    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    try {
        // A background several of the frames use is upscaled once, by the first of them.
        std::set<uint16_t> upscaledBackgrounds;
        for (std::size_t frameIndex : targets) {
            const std::optional<document::FrameBackgroundData>& backgroundData =
                m_document->backgroundData(frameIndex);
            document::FrameHdParts parts;
            parts.filter = m_createHdFilter;
            parts.backgroundMask = backgroundMaskCheckBox->isChecked() && backgroundData;
            parts.backgroundImage =
                backgroundImageCheckBox->isChecked() && backgroundData &&
                backgroundData->backgroundId < m_document->backgroundCount() &&
                upscaledBackgrounds.insert(backgroundData->backgroundId).second;
            commands.push_back(edit::createFrameHd(*m_document, frameIndex, parts));
        }
    } catch (const std::exception& ex) {
        recordFrameEdits(std::move(commands));
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    m_activeResolution = render::Resolution::Hd;
    recordFrameEdits(std::move(commands));
    statusBar()->showMessage(
        tr("Created %n HD frame(s) from SD (%1)", "", static_cast<int>(targets.size()))
            .arg(QString::fromStdString(document::upscaleFilterName(m_createHdFilter))),
        2500);
}

void MainWindow::createSelectedFramesHdFromOriginal()
{
    const std::vector<std::size_t> selected = selectedFrameIndices();
    // A black image for a background is the author's decision each time: every frame that
    // shows the background shows it black then.
    bool blackBackgrounds = false;
    const std::vector<std::size_t> withoutImage =
        edit::backgroundsWithoutHdImage(*m_document, selected);
    if (!withoutImage.empty()) {
        const bool framesMissing =
            std::any_of(selected.begin(), selected.end(), [this](std::size_t frameIndex) {
                return m_document->frameHdGaps(frameIndex).frame;
            });
        QStringList names;
        for (const std::size_t background : withoutImage) {
            names << QString::number(background);
        }
        QMessageBox question(this);
        question.setIcon(QMessageBox::Question);
        question.setWindowTitle(tr("Create %1 Frame").arg(nativePlaneText(*m_document)));
        question.setText(
            tr("Backgrounds without a %2 image: %1. Give them a black one? Every frame that "
               "shows such a background shows black there until it gets a picture (Import "
               "Image on the Background section).")
                .arg(names.join(QStringLiteral(", ")))
                .arg(nativePlaneText(*m_document)));
        QPushButton* blackButton =
            question.addButton(tr("Give Them a Black Image"), QMessageBox::AcceptRole);
        QPushButton* framesOnlyButton =
            framesMissing ? question.addButton(tr("Frames Only"), QMessageBox::AcceptRole)
                          : nullptr;
        QPushButton* cancelButton = question.addButton(QMessageBox::Cancel);
        question.setDefaultButton(blackButton);
        question.setEscapeButton(cancelButton);
        question.exec();
        if (question.clickedButton() == blackButton) {
            blackBackgrounds = true;
        } else if (!framesOnlyButton || question.clickedButton() != framesOnlyButton) {
            return;
        }
    }
    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    int frames = 0;
    int backgrounds = 0;
    try {
        for (std::size_t frameIndex : selected) {
            document::FrameHdGaps gaps = m_document->frameHdGaps(frameIndex);
            gaps.background = gaps.background && blackBackgrounds;
            if (!gaps.frame && !gaps.background) {
                continue;
            }
            // Nothing is upscaled from the unused SD data; the HD masks, dynamic colors and
            // rotations of the frame stay as they are stored.
            document::FrameHdParts parts;
            parts.backgroundImage = gaps.background;
            commands.push_back(edit::createFrameHd(*m_document, frameIndex, parts));
            frames += gaps.frame ? 1 : 0;
            backgrounds += gaps.background ? 1 : 0;
        }
    } catch (const std::exception& ex) {
        recordFrameEdits(std::move(commands));
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    if (commands.empty()) {
        updateFrameMetadataControls();
        return;
    }
    recordFrameEdits(std::move(commands));
    statusBar()->showMessage(tr("Created %1 frame(s) from the originals in their plain colors "
                                "and %2 black background image(s)")
                                 .arg(frames)
                                 .arg(backgrounds),
                             4000);
}

void MainWindow::removeCurrentFrameHd()
{
    if (!m_document || !currentFrameIndex()) {
        return;
    }
    if (isHdOnlyProject()) {
        statusBar()->showMessage(
            tr("The %1 frame is the only frame data this project renders; it "
               "cannot be removed")
                .arg(nativePlaneText(*m_document)),
            4000);
        return;
    }
    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    try {
        for (std::size_t frameIndex : selectedFrameIndices()) {
            if (m_document->hasHdFrame(frameIndex)) {
                commands.push_back(edit::removeFrameHd(*m_document, frameIndex));
            }
        }
    } catch (const std::exception& ex) {
        recordFrameEdits(std::move(commands));
        updateFrameMetadataControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    const int removed = static_cast<int>(commands.size());
    if (removed == 0) {
        updateFrameMetadataControls();
        return;
    }
    recordFrameEdits(std::move(commands));
    statusBar()->showMessage(tr("Removed %n HD frame(s)", "", removed), 2500);
}

void MainWindow::updateDynamicColorControls()
{
    if (m_dynamicPanel) {
        m_dynamicPanel->refreshSets(m_document ? &*m_document : nullptr, currentFrameIndex(),
                                    dynamicPlane());
        m_dynamicPanel->refreshColors(m_document ? &*m_document : nullptr,
                                      currentFrameIndex(),
                                      dynamicPlane());
    }
    if (m_preview) {
        m_preview->setSelectedDynamicSet(m_document ? std::optional<uint8_t>(activeDynamicSet())
                                                    : std::nullopt);
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
    statusBar()->showMessage(tr("Selected dynamic set %1").arg(setIndex), 1500);
    updateDynamicColorControls();
}

void MainWindow::editDynamicColor(std::size_t originalColorIndex, std::optional<uint16_t> color)
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
            color ? color : pickColor(oldRgb565, tr("Dynamic Color"));
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
        statusBar()->showMessage(tr("Updated dynamic color 0x%1")
                                     .arg(newRgb565, 4, 16, QLatin1Char('0')),
                                 2500);
    } catch (const std::exception& ex) {
        updateDynamicColorControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::setDynamicSetOriginalOnly(bool originalOnly)
{
    if (!m_document || !currentFrameIndex() ||
        activeDynamicSet() >= document::kMaxDynamicSetsPerFrame) {
        return;
    }
    // Every selected frame, one step each, as the set's shadow.
    const uint32_t bit = 1u << activeDynamicSet();
    bool changed = false;
    for (std::size_t frameIndex : selectedFrameIndices()) {
        if (!m_document->dynamicData(frameIndex).has_value()) {
            continue;
        }
        const uint32_t before = m_document->originalOnlyDynamicSets(frameIndex);
        const uint32_t after = originalOnly ? before | bit : before & ~bit;
        if (after != before) {
            commitEdit(
                std::make_unique<edit::OriginalOnlyDynamicSetsCommand>(frameIndex, before, after));
            changed = true;
        }
    }
    updateDynamicShadowControls();
    if (changed) {
        updateUndoRedoActions();
        statusBar()->showMessage(originalOnly
                                     ? tr("Set %1 colors the SD picture only")
                                           .arg(activeDynamicSet())
                                     : tr("Set %1 colors the SD and the HD picture")
                                           .arg(activeDynamicSet()),
                                 2500);
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
        statusBar()->showMessage(tr("Updated dynamic shadow direction"), 2500);
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
            pickColor(oldRgb565, tr("Dynamic Shadow Color"));
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
        statusBar()->showMessage(tr("Updated dynamic shadow color 0x%1")
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
            tr("Set rotation length %1 on %2 frame(s)")
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
            tr("Set rotation delay %1 ms on %2 frame(s)")
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
        pickColor(oldColor, tr("Color Rotation Color"));
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
            tr("Updated rotation color on %1 frame(s)")
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
            tr("Cleared rotation slot %1 on %2 frame(s)")
                .arg(static_cast<int>(slotIndex))
                .arg(static_cast<int>(frameIndices.size())),
            2000);
    } catch (const std::exception& ex) {
        updateColorRotationControls();
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

std::optional<uint16_t> MainWindow::pickColor(uint16_t initialRgb565, const QString& title)
{
    // The project's palettes to pick from, the one selected in the Palette section first.
    std::vector<ColorPickerPalette> palettes;
    if (m_document) {
        for (std::size_t index = 0; index < m_document->paletteCount(); ++index) {
            const document::PaletteRecord& palette = m_document->palette(index);
            palettes.push_back({QString::fromStdString(palette.name), palette.colors});
        }
    }
    return ColorPickerDialog::getColor(initialRgb565, this, title, std::move(palettes),
                                       selectedPaletteIndex().value_or(0));
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
        tr("Picked palette %1 color %2: 0x%3")
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
        pickColor(oldColor, tr("Palette Color"));
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
            tr("Updated palette %1 color %2")
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
        statusBar()->showMessage(tr("Gradient needs at least two colors"), 2500);
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
            tr("Applied palette gradient %1-%2")
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
    // Another sprite in the sprite editor: another canvas content.
    if (spriteCanvasShown()) {
        noteCanvasPlace();
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

void MainWindow::updateInspectorSections()
{
    if (!m_inspectorSections) {
        return;
    }
    // Palettes, color sets and helper lines serve every canvas; the frame's own topics only the
    // frame canvas. A background or a sprite being edited has its own section (Back to Frame
    // there leaves it), opened on the way in.
    static const QStringList everywhere{QStringLiteral("Palette"), QStringLiteral("Sets"),
                                        QStringLiteral("HelperLines")};
    static const QStringList records{QStringLiteral("BGRecord"), QStringLiteral("SpriteRecord")};
    // A scene frame is identified by its marker, not by the ROM frame: a comparison mask, sprites
    // (found on the original) and dynamic zones (the original's shades) do nothing there.
    static const QStringList notForSceneFrames{QStringLiteral("Mask"), QStringLiteral("Sprite"),
                                               QStringLiteral("Dyn")};
    QString record;
    if (spriteCanvasShown()) {
        record = QStringLiteral("SpriteRecord");
    } else if (isBackgroundImageTargetActive()) {
        record = QStringLiteral("BGRecord");
    }
    const std::optional<std::size_t> frame = currentFrameIndex();
    const bool sceneFrame = record.isEmpty() && m_document && frame &&
                            m_document->sceneFrame(*frame).has_value();
    for (int index = 0; index < m_inspectorSections->count(); ++index) {
        const QString key = m_inspectorSections->tabText(index);
        if (key == QStringLiteral("SceneSettings")) {
            continue;  // updateSceneSettingsSection()
        }
        const bool shown = record.isEmpty()
                               ? !records.contains(key) &&
                                     !(sceneFrame && notForSceneFrames.contains(key))
                               : key == record || everywhere.contains(key);
        if (key == record && m_inspectorSections->isSectionHidden(index)) {
            m_inspectorSections->setSectionOpen(index, true);
        }
        m_inspectorSections->setSectionHidden(index, !shown);
    }
    updateSceneSettingsSection(false);
    noteCanvasPlace();
}

CanvasPlace MainWindow::currentCanvasPlace() const
{
    CanvasPlace place;
    place.frame = currentFrameIndex().value_or(0);
    place.index = place.frame;
    if (m_frameFilterComboBox) {
        place.filter = m_frameFilterComboBox->currentData().toInt();
        place.filterTag =
            m_frameFilterComboBox->currentData(kFilterTagRole).toString().toStdString();
        // The frames of a scene.
        if (static_cast<FrameFilter>(place.filter) == FrameFilter::SceneTimeline &&
            m_sceneTimeline) {
            place.filterScene = *m_sceneTimeline;
        }
    }
    if (spriteCanvasShown()) {
        if (const std::optional<std::size_t> sprite = currentSpriteIndex()) {
            place.kind = CanvasPlace::Kind::Sprite;
            place.index = *sprite;
        }
    } else if (isBackgroundImageTargetActive()) {
        place.kind = CanvasPlace::Kind::Background;
        place.index = *selectedBackgroundIndex();
    }
    return place;
}

void MainWindow::noteCanvasPlace(bool frameRested)
{
    // Playback steps through frames by itself: not a place the author went to.
    if (!m_document || m_showingCanvasPlace || !currentFrameIndex() ||
        (m_playbackTimer && m_playbackTimer->isActive())) {
        return;
    }
    m_canvasHistory.note(currentCanvasPlace(), frameRested);
    updateHistoryButtons();
}

void MainWindow::leaveSceneTimeline()
{
    if (!m_document) {
        return;
    }
    noteCanvasPlace(true);
    const document::ProjectDocument& document = *m_document;
    const auto exists = [&document](const CanvasPlace& place) {
        return place.kind != CanvasPlace::Kind::Frame || place.index < document.frameCount();
    };
    // A frame of the timeline before: not a scene's, and not a scene frame.
    const auto outside = [&document](const CanvasPlace& place) {
        return place.filterScene < 0 && place.kind == CanvasPlace::Kind::Frame &&
               place.index < document.frameCount() && !document.sceneFrame(place.index);
    };
    if (const std::optional<CanvasPlace> place = m_canvasHistory.backTo(exists, outside)) {
        showCanvasPlace(*place);
    } else if (m_frameFilterComboBox) {
        // Nothing before the scene: all frames, at the current one.
        m_frameFilterComboBox->setCurrentIndex(
            std::max(0, m_frameFilterComboBox->findData(static_cast<int>(FrameFilter::All))));
    }
    updateHistoryButtons();
}

void MainWindow::updateHistoryButtons()
{
    if (m_leaveSceneButton) {
        m_leaveSceneButton->setVisible(m_document && currentCanvasPlace().filterScene >= 0);
    }
    if (m_historyBackButton) {
        m_historyBackButton->setEnabled(m_canvasHistory.canGoBack());
    }
    if (m_historyForwardButton) {
        m_historyForwardButton->setEnabled(m_canvasHistory.canGoForward());
    }
}

void MainWindow::goBackOrForward(bool back)
{
    if (!m_document) {
        return;
    }
    // Where the canvas is now counts, also a frame that has not rested yet.
    noteCanvasPlace(true);
    const document::ProjectDocument& document = *m_document;
    const auto exists = [&document](const CanvasPlace& place) {
        switch (place.kind) {
            case CanvasPlace::Kind::Frame:
                return place.index < document.frameCount();
            case CanvasPlace::Kind::Background:
                return place.index < document.backgroundCount();
            case CanvasPlace::Kind::Sprite:
                return place.index < document.spriteCount();
        }
        return false;
    };
    const std::optional<CanvasPlace> place =
        back ? m_canvasHistory.back(exists) : m_canvasHistory.forward(exists);
    if (place) {
        showCanvasPlace(*place);
    }
    updateHistoryButtons();
}

void MainWindow::showCanvasPlace(const CanvasPlace& place)
{
    m_showingCanvasPlace = true;
    if (m_playbackTimer && m_playbackTimer->isActive()) {
        setPlaybackRunning(false);
    }
    // The timeline first, so that the frame is in it.
    if (place.filterScene >= 0) {
        setSceneTimelineEntry(static_cast<uint16_t>(place.filterScene));
    }
    if (m_frameFilterComboBox) {
        int filterIndex = m_frameFilterComboBox->findData(place.filter);
        if (!place.filterTag.empty()) {
            filterIndex = m_frameFilterComboBox->findData(QString::fromStdString(place.filterTag),
                                                          kFilterTagRole);
        }
        if (filterIndex >= 0 && filterIndex != m_frameFilterComboBox->currentIndex()) {
            m_frameFilterComboBox->setCurrentIndex(filterIndex);
        } else if (place.filterScene >= 0) {
            refreshFrameListItems();
        }
    }
    if (place.frame < m_document->frameCount()) {
        goToFrame(place.kind == CanvasPlace::Kind::Frame ? place.index : place.frame);
    }
    switch (place.kind) {
        case CanvasPlace::Kind::Frame:
            leaveSpriteCanvas();
            if (activePaintTarget() == PaintTarget::Background) {
                editFrameInsteadOfBackground();
            }
            break;
        case CanvasPlace::Kind::Background: {
            const int comboIndex = m_backgroundComboBox->findData(static_cast<int>(place.index));
            if (comboIndex >= 0) {
                m_backgroundComboBox->setCurrentIndex(comboIndex);
            }
            editAssignedBackground();
            break;
        }
        case CanvasPlace::Kind::Sprite:
            if (activePaintTarget() == PaintTarget::Background) {
                editFrameInsteadOfBackground();
            }
            m_selectedSpriteIndex = static_cast<int>(place.index);
            updateSpriteControls();
            if (m_spritePanel && !m_spritePanel->isSpriteCanvasChecked()) {
                m_spritePanel->setSpriteCanvasChecked(true);
                if (m_spritePanel->onSpriteCanvasToggled) {
                    m_spritePanel->onSpriteCanvasToggled(true);
                }
            }
            break;
    }
    m_showingCanvasPlace = false;
}

void MainWindow::updatePaintTargetMode()
{
    const bool backgroundTargetActive = isBackgroundImageTargetActive();
    if (m_preview) {
        m_preview->setSelectedBackgroundIndex(selectedBackgroundIndex());
        m_preview->setBackgroundImageEditModeEnabled(backgroundTargetActive);
        m_preview->clearPreviewPixels();
    }
    updateInspectorSections();
    updateUndoRedoActions();
}

void MainWindow::setPaintTarget(PaintTarget target)
{
    if (m_paintTarget == target) {
        return;
    }
    m_paintTarget = target;
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
    return m_paintTarget;
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
    context.spriteBounds = m_spritePanel && m_spritePanel->isBoundsOverlayChecked();
    context.paintTarget = activePaintTarget();
    context.selectedBackground = selectedBackgroundIndex();
    context.selectedPalette = selectedPaletteIndex();
    context.tool = m_drawingTool;
    // Alt (Option on a Mac) while dragging a shape draws it from its centre, as in Photoshop.
    context.fromCentre = QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
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
    outputs.spriteBoundsDrawn = [this](render::Point min, render::Point max) {
        setCurrentSpriteSlotBounds(SpriteSlotBounds{min.x, min.y, max.x, max.y});
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
    // The author's choice for every project, off as in Win32.
    const bool stayInPasteMode =
        QSettings().value(QStringLiteral("edit/stayInPasteMode"), false).toBool();
    actions.setStayInPasteMode(stayInPasteMode);
    m_pasteMode.setStaysActive(stayInPasteMode);
    actions.onStayInPasteModeToggled = [this](bool stay) {
        m_pasteMode.setStaysActive(stay);
        QSettings().setValue(QStringLiteral("edit/stayInPasteMode"), stay);
    };
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
    actions.onDynamicToStatic = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_editHelpers.dynamicToStatic(canvasEditContext(), m_canvasEdit.selection());
        }
    };
    actions.onBrightness = [this]() {
        if (!canUseFrameCanvasHelpers()) {
            return;
        }
        bool accepted = false;
        const int percent = QInputDialog::getInt(
            this,
            tr("Brightness"),
            tr("Brightness of the selected pixels, in percent of their color "
               "(below 100 darker, above brighter):"),
            m_brightnessPercent, 0, 2000, 5, &accepted);
        if (accepted) {
            m_brightnessPercent = percent;
            m_editHelpers.brightness(canvasEditContext(), m_canvasEdit.selection(),
                                     percent / 100.0);
        }
    };
    actions.onShadowSelection = [this](edit::SelectionDirection direction) {
        if (canUseFrameCanvasHelpers()) {
            m_editHelpers.shadowSelection(canvasEditContext(), m_canvasEdit.selection(),
                                          direction);
        }
    };
    actions.onGrowSelection = [this](edit::SelectionDirection direction) {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.growSelection(canvasEditContext(), direction);
        }
    };
    actions.onSelectAll = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.selectAll(canvasEditContext());
        }
    };
    actions.onSelectNone = [this]() {
        if (canUseFrameCanvasHelpers()) {
            m_canvasEdit.clearSelection(canvasEditContext());
            statusBar()->showMessage(tr("Selection cleared"), 1500);
        }
    };
    actions.onUndoSelection = [this]() {
        if (canUseFrameCanvasHelpers() && !m_canvasEdit.undoSelection()) {
            statusBar()->showMessage(tr("No selection to bring back"), 1500);
        }
    };
    actions.onRedoSelection = [this]() {
        if (canUseFrameCanvasHelpers() && !m_canvasEdit.redoSelection()) {
            statusBar()->showMessage(tr("No undone selection to bring back"), 1500);
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
        statusBar()->showMessage(tr("No frame"), 2000);
        return false;
    }
    if (isSpriteCanvasActive()) {
        statusBar()->showMessage(
            tr("On the sprite canvas only Copy and Paste work: the selection tools "
               "work on the frame canvas"),
            3000);
        return false;
    }
    if (m_runtimeView.isEnabled()) {
        statusBar()->showMessage(tr("Nothing can be edited in the Runtime view"),
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

void MainWindow::paintSpriteDynamic(std::size_t spriteIndex,
                                    const std::vector<render::Point>& pixels,
                                    bool erase)
{
    if (!m_document || spriteIndex >= m_document->spriteCount() || !m_spritePanel) {
        return;
    }
    const uint8_t value = erase ? uint8_t{255} : static_cast<uint8_t>(m_spritePanel->dynamicSet());
    edit::SpriteState before = edit::spriteState(*m_document, spriteIndex);
    try {
        if (m_document->setSpriteDynamicMaskPixels(spriteIndex, pixels, value) == 0) {
            return;
        }
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
        return;
    }
    recordEdit(edit::spriteStateCommand(*m_document, spriteIndex, std::move(before)));
    if (m_spriteEditor) {
        m_spriteEditor->update();
    }
    setSpriteDependentViewsDirty();
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
        const QString text = tr("Copied sprite %1 (%2x%3)")
                                 .arg(static_cast<int>(*spriteIndex))
                                 .arg(buffer.box.width)
                                 .arg(buffer.box.height);
        m_pasteMode.setClipboard(std::move(buffer));
        updateEditMenuActions();
        statusBar()->showMessage(text, 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(tr("Not copied: %1").arg(QString::fromUtf8(ex.what())),
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
        statusBar()->showMessage(tr("Nothing has been copied"), 2500);
        return;
    }
    try {
        recordEdit(edit::pasteIntoSprite(
            *m_document, *spriteIndex, *buffer, spriteCanvasPlane(*spriteIndex), false, false));
        setSpriteDependentViewsDirty();
        updateSpriteControls();
        statusBar()->showMessage(tr("Pasted into sprite %1")
                                     .arg(static_cast<int>(*spriteIndex)),
                                 2500);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(tr("Not pasted: %1").arg(QString::fromUtf8(ex.what())),
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
                tr("%1 pixels selected. %2 frame(s) show the same there, %3 of them "
                   "would change.")
                    .arg(static_cast<qulonglong>(selection->count()))
                    .arg(static_cast<qulonglong>(plan->scan.matches.size()))
                    .arg(static_cast<qulonglong>(plan->changingFrames.size()));
            if (plan->scan.withoutPlane > 0) {
                text += tr(" %1 more without a picture on this plane are left alone.")
                            .arg(static_cast<qulonglong>(plan->scan.withoutPlane));
            }
            if (plan->scan.hashCollisions > 0) {
                text += tr(" %1 frame(s) had the same hash but other pixels.")
                            .arg(static_cast<qulonglong>(plan->scan.hashCollisions));
            }
            return text;
        },
        this);
    if (dialog.exec() != QDialog::Accepted || !plan) {
        return;
    }
    if (plan->changingFrames.empty()) {
        statusBar()->showMessage(tr("Auto copy: nothing to change"), 2500);
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
    ScrollFillPlan plan = m_editHelpers.planScrollFill(context, m_canvasEdit.selection());
    if (plan.lastFrame < plan.firstFrame + 2) {
        statusBar()->showMessage(
            tr("Select the frames of the scrolling, the current one among them: the "
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
    question.setWindowTitle(tr("Scrolling Auto Fill"));
    question.setText(
        (plan.selectionOnly
             ? tr("Fill the %1 frame(s) between frame %2 and frame %3 with the selected part "
                  "of them?")
             : tr("Fill the %1 frame(s) between frame %2 and frame %3?"))
            .arg(static_cast<qulonglong>(plan.frames.size()))
            .arg(static_cast<qulonglong>(plan.firstFrame))
            .arg(static_cast<qulonglong>(plan.lastFrame)));
    question.setInformativeText(
        tr("%1 scroll from the first frame, %2 from the last; %3 would change.")
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
                                 ? tr("Click a pixel to select every pixel of its "
                                      "color (Esc cancels)")
                                 : tr("Click a pixel to recolor every pixel of its "
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
            tr("Active draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
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
    button->setToolTip(tr("Draw color 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')));
    button->setStyleSheet(favoriteColorButtonStyle(rgb565));
    setActiveDrawColor(rgb565);
    statusBar()->showMessage(
        tr("Updated favorite color %1 to 0x%2")
            .arg(static_cast<int>(favoriteIndex + 1))
            .arg(rgb565, 4, 16, QLatin1Char('0')),
        2000);
}

void MainWindow::editFavoriteColor(std::size_t favoriteIndex)
{
    if (favoriteIndex >= m_favoriteColors.size()) {
        return;
    }
    const std::optional<uint16_t> picked =
        pickColor(m_favoriteColors[favoriteIndex], tr("Favorite Color"));
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
    const std::optional<uint16_t> picked = pickColor(
        m_document->editColor(slot), tr("Edit Color %1").arg(static_cast<int>(slot)));
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
                       tr("Edit color %1 is 0x%2")
                           .arg(static_cast<int>(slot))
                           .arg(rgb565, 4, 16, QLatin1Char('0')));
}

void MainWindow::setDrawColorMode(edit::DrawColorMode mode)
{
    m_drawColorMode = mode;
    if (m_spriteEditor) {
        m_spriteEditor->setColorSetReplace(mode == edit::DrawColorMode::ColorSetReplace);
    }
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
                                 ? tr("Drawing replaces each original shade by its "
                                      "edit color")
                                 : tr("Drawing with a single color"),
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
                       tr("Stored the edit colors in color set %1")
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
                       tr("Loaded color set %1 into the edit colors")
                           .arg(static_cast<int>(setIndex)));
}

void MainWindow::deactivateColorSet(std::size_t setIndex)
{
    if (!m_document) {
        return;
    }
    commitEditorColors(
        edit::withColorSetActive(m_document->colorSetTables(), setIndex, false),
        tr("Deactivated color set %1").arg(static_cast<int>(setIndex)));
}

void MainWindow::renameColorSet(std::size_t setIndex, const QString& name)
{
    if (!m_document) {
        return;
    }
    commitEditorColors(
        edit::withColorSetName(m_document->colorSetTables(), setIndex, name.toStdString()),
        tr("Renamed color set %1").arg(static_cast<int>(setIndex)));
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
            tr("Copied the edit colors to dynamic set %1 of %2 frame(s)")
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
                           tr("Copied dynamic set %1 to the edit colors")
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
            tr("No edit color is 0x%1").arg(rgb565, 4, 16, QLatin1Char('0')), 2500);
        return;
    }
    selectEditColorSlot(*slot);
    statusBar()->showMessage(
        tr("Selected edit color %1 (0x%2)")
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

bool MainWindow::spriteCanvasShown() const
{
    return m_spriteEditor && m_canvasStack && m_canvasStack->currentWidget() == m_spriteEditor;
}

void MainWindow::updateZoomButton()
{
    if (!m_zoomButton) {
        return;
    }
    const double zoom = spriteCanvasShown() ? m_spriteEditor->zoom() : m_preview->zoom();
    m_zoomButton->setText(QStringLiteral("%1%").arg(std::lround(zoom * 100.0)));
}

bool MainWindow::handleSpacePanning(QEvent* event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return false;
    }
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() != Qt::Key_Space || keyEvent->modifiers() != Qt::NoModifier) {
        return false;
    }
    if (event->type() == QEvent::KeyRelease) {
        if (!m_spacePanning || keyEvent->isAutoRepeat()) {
            return m_spacePanning;
        }
        endSpacePanning();
        return true;
    }
    if (m_spacePanning) {
        // The key repeats while held; nothing else gets it meanwhile.
        return true;
    }
    // Not while typing into a field, nor when the mouse is not over the canvas.
    QWidget* focus = QApplication::focusWidget();
    if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus) ||
        qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus) ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        return false;
    }
    QWidget* canvas = spriteCanvasShown() ? static_cast<QWidget*>(m_spriteEditor)
                                          : static_cast<QWidget*>(m_preview);
    QWidget* under = QApplication::widgetAt(QCursor::pos());
    if (!canvas || !under || (under != canvas && !canvas->isAncestorOf(under))) {
        return false;
    }
    m_spacePanning = true;
    if (canvas == m_spriteEditor) {
        m_spriteEditor->setPanMode(true);
    } else {
        m_preview->setPanMode(true);
    }
    return true;
}

void MainWindow::endSpacePanning()
{
    if (!m_spacePanning) {
        return;
    }
    m_spacePanning = false;
    if (m_spriteEditor) {
        m_spriteEditor->setPanMode(false);
    }
    if (m_preview) {
        m_preview->setPanMode(false);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Space held over the canvas moves the shown part with the mouse, as in image editors.
    if (watched->isWidgetType() && handleSpacePanning(event)) {
        return true;
    }
    if (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::WindowDeactivate) {
        endSpacePanning();
    }
    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::KeyPress:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
            noteActivity();
            break;
        default:
            break;
    }
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
        // Left and Right step through the frames from the canvas as well (issue #20); the
        // canvas keeps the focus.
        if (m_frameList && FrameListWidget::isFrameStepKey(*keyEvent)) {
            m_frameList->stepWithKey(*keyEvent);
            return true;
        }
        // Ctrl+Left and Ctrl+Right look through the same frames in the strip (issue #24).
        if (m_frameList && FrameListWidget::isMarkedRowStepKey(*keyEvent)) {
            m_frameList->showMarkedRow(keyEvent->key() == Qt::Key_Left ? -1 : 1);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape &&
            m_pendingCanvasClick != PendingCanvasClick::None) {
            armCanvasClick(PendingCanvasClick::None);
            statusBar()->showMessage(tr("Cancelled"), 1500);
            return true;
        }
        // With nothing to cancel, Esc reduces a selection of frames to the frame shown, as in
        // the strip.
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier &&
            m_frameList && m_frameList->selectCurrentAlone()) {
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
    pushToHistory(edit::historyKey(applied.target()), std::move(command));
    refreshAfterEdit(applied);
}

void MainWindow::pushEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const edit::HistoryKey key = edit::historyKey(command->target());
    pushToHistory(key, std::move(command));
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
        confirm.addButton(tr("Delete"), QMessageBox::DestructiveRole);
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
        statusBar()->showMessage(tr("No frame selected"), 2500);
        return;
    }
    bool accepted = false;
    const int durationMs = QInputDialog::getInt(
        this,
        tr("Delete Short Frames"),
        tr("Delete the selected frames that are shorter than (ms):"),
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
            tr("None of the %1 selected frame(s) is shorter than %2 ms")
                .arg(static_cast<int>(selected.size()))
                .arg(durationMs),
            4000);
        return;
    }
    deleteFrames(shortFrames,
                 tr("%1 of the %2 selected frame(s) are shorter than %3 ms.")
                     .arg(static_cast<int>(shortFrames.size()))
                     .arg(static_cast<int>(selected.size()))
                     .arg(durationMs));
}

void MainWindow::deleteFrames(const std::vector<std::size_t>& frameIndices, const QString& note)
{
    if (!m_document || frameIndices.empty()) {
        statusBar()->showMessage(tr("No frame selected"), 2500);
        return;
    }
    const int count = static_cast<int>(frameIndices.size());
    const int total = static_cast<int>(m_document->frameCount());
    if (count >= total) {
        statusBar()->showMessage(
            tr("Not deleted: all %1 frame(s) would go, and a project keeps at "
               "least one frame")
                .arg(total),
            5000);
        return;
    }
    QStringList consequences;
    if (!note.isEmpty()) {
        consequences << note;
    }
    consequences << tr("Frames: %1. %2 frame(s) remain. Sections and the source "
                       "frames of sprites follow. The deletion can be undone.")
                        .arg(frameRangesText(frameIndices))
                        .arg(total - count);
    // Frames of scenes: the scenes get smaller and their frames stay numbered without a gap.
    std::set<uint16_t> scenes;
    for (std::size_t frame : frameIndices) {
        if (frame < m_document->frameCount()) {
            if (const auto& ref = m_document->sceneFrame(frame)) {
                scenes.insert(ref->sceneId);
            }
        }
    }
    if (!scenes.empty()) {
        QStringList ids;
        for (uint16_t scene : scenes) {
            ids << QString::number(scene);
        }
        consequences << tr("Scene(s) %1 get fewer frames; their later frames are renumbered "
                           "(markers included), so the frame numbers have no gap.")
                            .arg(ids.join(QStringLiteral(", ")));
    }
    if (!confirmDeletion(tr("Delete Frames"),
                         count == 1 ? tr("Delete frame %1?")
                                          .arg(static_cast<qulonglong>(frameIndices.front()))
                                    : tr("Delete %1 frames?").arg(count),
                         consequences.join(QChar(' ')))) {
        return;
    }
    try {
        if (!scenes.empty()) {
            recordSceneEdit(edit::deleteFramesOfScenes(*m_document, frameIndices));
            return;
        }
        recordStructureEdit(edit::deleteFrames(*m_document, frameIndices));
        showStructureEditStatus(
            tr("Deleted %1 frame(s), %2 remain").arg(count).arg(total - count),
            4000);
    } catch (const std::invalid_argument& ex) {
        QMessageBox::warning(this, tr("Delete Frames"), QString::fromUtf8(ex.what()));
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
    if (m_document->spriteCount() >= m_document->limits().maxSprites) {
        statusBar()->showMessage(
            tr("Not added: a project for %1 holds at most %2 sprites (Project Settings)")
                .arg(QString::fromStdString(
                    document::libserumTargetName(m_document->libserumTarget())))
                .arg(static_cast<int>(m_document->limits().maxSprites)),
            4000);
        return std::nullopt;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(
                             this,
                             tr("New Sprite"),
                             tr("Name of the new sprite (up to 31 characters):"),
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
            tr("Not added: %1").arg(QString::fromStdString(problem)), 4000);
        return std::nullopt;
    }
    try {
        // The cRP records the frame a sprite was made on, as the Win32 editor does.
        recordStructureEdit(
            edit::addSprite(*m_document, name.toStdString(), currentFrameIndex().value_or(0)));
        showStructureEditStatus(
            tr("Added sprite %1 \"%2\", %3 sprite(s) now")
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
        statusBar()->showMessage(tr("No sprite selected"), 2500);
        return;
    }
    const int useCount = static_cast<int>(m_document->spriteUseCount(*spriteIndex));
    const QString name = QString::fromStdString(m_document->sprite(*spriteIndex).name);
    if (!confirmDeletion(
            tr("Delete Sprite"),
            name.isEmpty() ? tr("Delete sprite %1?").arg(static_cast<int>(*spriteIndex))
                           : tr("Delete sprite %1 \"%2\"?")
                                 .arg(static_cast<int>(*spriteIndex))
                                 .arg(name),
            tr("%1 Sprites behind it get a number one lower. The deletion can be "
               "undone.")
                .arg(useCount > 0 ? tr("It is removed from the sprite slots of %1 "
                                       "frame(s).")
                                        .arg(useCount)
                                  : tr("No frame uses it.")))) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteSprite(*m_document, *spriteIndex));
        showStructureEditStatus(
            tr("Deleted sprite %1%2; %3 sprite(s) remain")
                .arg(static_cast<int>(*spriteIndex))
                .arg(useCount > 0 ? tr(", removed from %1 frame(s)").arg(useCount)
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
        statusBar()->showMessage(tr("Select a frame first"), 2500);
        return;
    }
    if (m_document->backgroundCount() >= m_document->limits().maxBackgrounds) {
        statusBar()->showMessage(
            tr("Not added: a project for %1 holds at most %2 backgrounds (Project Settings)")
                .arg(QString::fromStdString(
                    document::libserumTargetName(m_document->libserumTarget())))
                .arg(static_cast<int>(m_document->limits().maxBackgrounds)),
            4000);
        return;
    }
    if (isHdOnlyProject() && !m_document->hasHdFrame(*frameIndex)) {
        statusBar()->showMessage(
            tr("Not added: frame %1 has no %2 frame to make a background of")
                .arg(static_cast<int>(*frameIndex))
                .arg(nativePlaneText(*m_document)),
            5000);
        return;
    }
    try {
        recordStructureEdit(edit::addBackgroundFromFrame(*m_document, *frameIndex));
        showStructureEditStatus(
            tr("Added background %1 from frame %2, %3 background(s) now")
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
        statusBar()->showMessage(tr("No background selected"), 2500);
        return;
    }
    const int useCount = static_cast<int>(m_document->backgroundUseCount(*backgroundIndex));
    if (!confirmDeletion(
            tr("Delete Background"),
            tr("Delete background %1?").arg(static_cast<int>(*backgroundIndex)),
            tr("%1 Backgrounds behind it get a number one lower. The deletion can "
               "be undone.")
                .arg(useCount > 0 ? tr("%1 frame(s) use it and will have no "
                                       "background.")
                                        .arg(useCount)
                                  : tr("No frame uses it.")))) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteBackground(*m_document, *backgroundIndex));
        showStructureEditStatus(
            tr("Deleted background %1%2; %3 background(s) remain")
                .arg(static_cast<int>(*backgroundIndex))
                .arg(useCount > 0 ? tr(", %1 frame(s) have no background now")
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
            tr("Not added: a project holds at most %1 comparison masks")
                .arg(static_cast<int>(document::kMaxComparisonMasks)),
            4000);
        return;
    }
    try {
        recordStructureEdit(edit::addComparisonMask(*m_document));
        showStructureEditStatus(
            tr("Added comparison mask %1, %2 mask(s) now; no frame uses it yet")
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
        statusBar()->showMessage(tr("No comparison mask selected"), 2500);
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
            ? tr("No frame uses it. Masks behind it get a number one lower. The "
                 "deletion can be undone.")
            : tr("%1 frame(s) use it (%2). They will be compared without a mask, "
                 "which changes how the runtime identifies them. Masks behind it "
                 "get a number one lower. The deletion can be undone.")
                  .arg(static_cast<int>(users.size()))
                  .arg(frameRangesText(users));
    if (!confirmDeletion(
            tr("Delete Comparison Mask"),
            tr("Delete comparison mask %1?").arg(static_cast<int>(maskIndex)),
            consequences)) {
        return;
    }
    try {
        recordStructureEdit(edit::deleteComparisonMask(*m_document, maskIndex));
        showStructureEditStatus(
            tr("Deleted comparison mask %1%2; %3 mask(s) remain")
                .arg(static_cast<int>(maskIndex))
                .arg(users.empty() ? QString()
                                   : tr(", %1 frame(s) have no mask now")
                                         .arg(static_cast<int>(users.size())))
                .arg(static_cast<int>(m_document->comparisonMaskCount())),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::moveSelectedFramesBeforeRow(int row)
{
    if (!m_document || !m_frameList) {
        return;
    }
    const std::size_t beforeFrame =
        row >= 0 && row < m_frameList->count()
            ? static_cast<std::size_t>(m_frameList->item(row)->data(Qt::UserRole).toULongLong())
            : m_document->frameCount();
    moveFrames(selectedFrameIndices(), beforeFrame);
}

void MainWindow::moveSelectedFramesUp()
{
    if (!m_document || !m_frameList) {
        return;
    }
    // The row above the first selected one; with a filter, the frame shown above.
    int firstRow = m_frameList->count();
    for (const QListWidgetItem* item : m_frameList->selectedItems()) {
        firstRow = std::min(firstRow, m_frameList->row(item));
    }
    if (firstRow <= 0 || firstRow >= m_frameList->count()) {
        statusBar()->showMessage(tr("Not moved: the frames are the first ones"), 2500);
        return;
    }
    moveSelectedFramesBeforeRow(firstRow - 1);
}

void MainWindow::moveSelectedFramesDown()
{
    if (!m_document || !m_frameList) {
        return;
    }
    // Behind the frame of the row below the last selected one.
    int lastRow = -1;
    for (const QListWidgetItem* item : m_frameList->selectedItems()) {
        lastRow = std::max(lastRow, m_frameList->row(item));
    }
    if (lastRow < 0 || lastRow + 1 >= m_frameList->count()) {
        statusBar()->showMessage(tr("Not moved: the frames are the last ones"), 2500);
        return;
    }
    // Behind it, not into a section that starts after it.
    const std::size_t below = m_frameList->item(lastRow + 1)->data(Qt::UserRole).toULongLong();
    moveFrames(selectedFrameIndices(), below + 1, false);
}

void MainWindow::moveSelectedFramesTo()
{
    const std::vector<std::size_t> frames = selectedFrameIndices();
    if (!m_document || frames.empty()) {
        statusBar()->showMessage(tr("No frame selected"), 2500);
        return;
    }
    const int count = static_cast<int>(m_document->frameCount());
    bool accepted = false;
    const int beforeFrame = QInputDialog::getInt(
        this,
        tr("Move Frames"),
        tr("Move the %1 selected frame(s) before frame (%2 = behind the last "
           "frame):")
            .arg(static_cast<int>(frames.size()))
            .arg(count),
        static_cast<int>(frames.front()),
        0,
        count,
        1,
        &accepted);
    if (accepted) {
        moveFrames(frames, static_cast<std::size_t>(beforeFrame));
    }
}

void MainWindow::moveFrames(const std::vector<std::size_t>& frameIndices,
                            std::size_t beforeFrame,
                            bool joinSection)
{
    if (!m_document || frameIndices.empty()) {
        statusBar()->showMessage(tr("No frame selected"), 2500);
        return;
    }
    try {
        std::unique_ptr<edit::FrameMoveCommand> command =
            edit::moveFrames(*m_document, frameIndices, beforeFrame, joinSection);
        if (!command) {
            statusBar()->showMessage(tr("The frames are there already"), 2500);
            return;
        }
        const edit::FrameMoveCommand& applied = *command;
        m_droppedStructureSteps =
            pushToHistory(edit::structureHistoryKey(), std::move(command));
        refreshAfterFrameMove(applied);
        updateUndoRedoActions();
        const std::vector<std::size_t> moved = applied.movedFrames();
        showStructureEditStatus(
            tr("Moved %1 frame(s); they are frames %2 now")
                .arg(static_cast<int>(moved.size()))
                .arg(frameRangesText(moved)),
            4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::refreshAfterFrameMove(const edit::FrameMoveCommand& command)
{
    if (!m_document) {
        return;
    }
    setPlaybackRunning(false);
    m_pasteMode.cancel();
    m_canvasEdit.cancelFreeGradient();
    m_canvasEdit.clearSelection(canvasEditContext());
    // The capture selection belongs to a frame number.
    m_canvasEdit.clearCaptureSelection();
    m_spriteDependentViewsDirty = false;
    const std::vector<std::size_t> moved = command.movedFrames();
    refreshFrameList(std::nullopt);
    selectFrameNear(moved.front());
    if (m_frameList && moved.size() > 1) {
        // All moved frames that are listed; told to the window once, below.
        QSignalBlocker blocker(m_frameList);
        for (int row = 0; row < m_frameList->count(); ++row) {
            QListWidgetItem* item = m_frameList->item(row);
            const std::size_t frameIndex = item->data(Qt::UserRole).toULongLong();
            if (std::binary_search(moved.begin(), moved.end(), frameIndex)) {
                item->setSelected(true);
            }
        }
    }
    refreshComponentBrowsers();
    updateSectionControls();
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
    updateStructureControls();
}

void MainWindow::recordStructureEdit(std::unique_ptr<edit::StructureCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const edit::StructureCommand& applied = *command;
    m_droppedStructureSteps = pushToHistory(edit::structureHistoryKey(), std::move(command));
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
        tr("%1 (undo keeps at most %2 MB of deleted records: the %3 oldest add or "
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
    const int row = std::min(low, m_frameList->count() - 1);
    selectSingleRow(*m_frameList, row);
    // A rebuilt list starts at the top; the frame is shown, also when its row was current.
    m_frameList->scrollToItem(m_frameList->item(row), QAbstractItemView::PositionAtCenter);
}

QString MainWindow::importColorizationSidecars()
{
    if (!m_document || m_document->colorizationSettings()) {
        return QString();
    }
    const bool wasDirty = m_document->isDirty();
    const runtime::SidecarImport imported = runtime::importColorizationSidecars(
        *m_document,
        platform::pathFromUtf8(QFileInfo(m_currentCromPath).absolutePath().toStdString()));
    if (imported.files.empty()) {
        // Default settings only: nothing to save for them yet.
        if (!wasDirty) {
            m_document->markClean();
        }
        return QString();
    }
    QStringList files;
    for (const std::string& file : imported.files) {
        files << QString::fromStdString(file);
    }
    return tr("%1 taken into the project (%n scene(s), %2 scene frames); saved with it, they "
              "are no longer needed",
              "",
              static_cast<int>(imported.scenes))
        .arg(files.join(tr(" and ")))
        .arg(static_cast<qulonglong>(imported.linkedFrames));
}

void MainWindow::setSpeakerLightPreviewShown(bool shown)
{
    SpeakerLightPreviewOptions options = loadSpeakerLightPreviewOptions();
    options.visible = shown;
    saveSpeakerLightPreviewOptions(options);
    if (m_speakerLights) {
        m_speakerLights->setOptions(options);
        updateFrameLightControls();
    }
}

bool MainWindow::confirmPassingSpriteWarning(const std::vector<std::size_t>& frameIndices,
                                             std::size_t slotIndex)
{
    if (!m_document) {
        return false;
    }
    std::size_t frames = 0;
    for (std::size_t frameIndex : frameIndices) {
        frames += m_document->assignmentPassesSpriteWarning(frameIndex, slotIndex) ? 1 : 0;
    }
    if (frames == 0) {
        return true;
    }
    QMessageBox confirm(QMessageBox::Warning, tr("More Than %1 Sprites")
                                                  .arg(document::kSpritesPerFrameWarning),
                        tr("%n frame(s) would use more than %1 sprites.", "",
                           static_cast<int>(frames))
                                .arg(document::kSpritesPerFrameWarning) +
                            QStringLiteral("\n\n") + SpritePanel::manySpritesAdvice(),
                        QMessageBox::Cancel, this);
    QPushButton* useMore = confirm.addButton(tr("Use More Sprites"), QMessageBox::AcceptRole);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    return confirm.clickedButton() == useMore;
}

void MainWindow::refreshSpeakerLights()
{
    if (m_lightEffectsPanel) {
        m_lightEffectsPanel->setProject(m_document ? &*m_document : nullptr);
    }
    updateLightEffectSection();
    updateFrameLightControls();
}

void MainWindow::updateFrameLightControls()
{
    const document::ProjectDocument* document = m_document ? &*m_document : nullptr;
    const std::optional<std::size_t> frame = currentFrameIndex();
    if (m_frameLightsPanel) {
        // What the ambient lighting would set now, for the channels it set.
        std::optional<document::LightActions> suggested;
        if (document && frame && *frame < document->frameCount() &&
            edit::hasAutomaticLights(document->frameLights(*frame))) {
            suggested = edit::suggestedAmbientLighting(composeResolvedFrame(
                *document, *frame, document->geometry().nativePlane(), true));
        }
        m_frameLightsPanel->setFrame(document, frame, suggested);
    }
    if (m_speakerLights) {
        m_speakerLights->showFrame(document, frame);
    }
}

void MainWindow::updateLightEffectSection()
{
    if (!m_lightEffectPanel || !m_lightEffectsPanel) {
        return;
    }
    std::optional<document::LightEffect> effect;
    if (m_document) {
        if (const std::optional<uint16_t> id = m_lightEffectsPanel->selectedEffect()) {
            if (const document::LightEffect* found = m_document->speakerLights().effect(*id)) {
                effect = *found;
            }
        }
    }
    // Unapplied changes of the effect shown stay while it is the one selected.
    if (effect && m_lightEffectPanel->effectId() == effect->id &&
        m_lightEffectPanel->hasChanges()) {
        return;
    }
    m_lightEffectPanel->setEffect(effect);
}

void MainWindow::refreshAfterLightsChanged(const std::vector<std::size_t>& frameIndices)
{
    updateFrameFilterChoices();
    const int filter = m_frameFilterComboBox ? m_frameFilterComboBox->currentData().toInt() : 0;
    if (filter == static_cast<int>(FrameFilter::AmbientLighting) ||
        filter == static_cast<int>(FrameFilter::AmbientLightingOutOfDate)) {
        refreshFrameList(currentFrameIndex());
        return;
    }
    updateFrameListItems(frameIndices);
}

void MainWindow::recordSpeakerLightsEdit(std::unique_ptr<edit::SpeakerLightsCommand> command,
                                         const QString& status)
{
    if (!m_document || !command) {
        return;
    }
    const std::vector<std::size_t> frames = command->target().frameIndices;
    m_droppedStructureSteps = pushToHistory(edit::structureHistoryKey(), std::move(command));
    refreshSpeakerLights();
    refreshAfterLightsChanged(frames);
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    showStructureEditStatus(status, 3000);
}

void MainWindow::addLightEffect()
{
    if (!m_document) {
        return;
    }
    document::SpeakerLightSettings settings = m_document->speakerLights();
    document::LightEffect effect;
    effect.id = settings.nextEffectId();
    effect.name = tr("Effect %1").arg(effect.id).toStdString();
    settings.effects.push_back(effect);
    recordSpeakerLightsEdit(edit::setSpeakerLights(*m_document, std::move(settings)),
                            tr("Added light effect %1").arg(QString::fromStdString(effect.name)));
    if (m_lightEffectsPanel) {
        m_lightEffectsPanel->selectEffect(effect.id);
        m_leftBrowserTabs->markCurrent(m_lightEffectsPanel);
    }
}

void MainWindow::deleteLightEffect(uint16_t effectId)
{
    if (!m_document) {
        return;
    }
    const document::LightEffect* effect = m_document->speakerLights().effect(effectId);
    if (!effect) {
        return;
    }
    const QString name = QString::fromStdString(effect->name);
    const std::size_t frames = m_document->framesUsingLightEffect(effectId).size();
    QMessageBox confirm(QMessageBox::Question, tr("Delete Light Effect"),
                        frames == 0
                            ? tr("Delete the light effect %1?").arg(name)
                            : tr("Delete the light effect %1? %n frame(s) start it; they leave "
                                 "that channel unchanged then.",
                                 "", static_cast<int>(frames))
                                  .arg(name),
                        QMessageBox::Yes | QMessageBox::Cancel, this);
    confirm.setDefaultButton(QMessageBox::Cancel);
    if (confirm.exec() != QMessageBox::Yes) {
        return;
    }
    recordSpeakerLightsEdit(edit::removeLightEffect(*m_document, effectId),
                            tr("Deleted light effect %1").arg(name));
}

void MainWindow::applyLightEffect(const document::LightEffect& effect)
{
    if (!m_document) {
        return;
    }
    document::SpeakerLightSettings settings = m_document->speakerLights();
    for (document::LightEffect& existing : settings.effects) {
        if (existing.id == effect.id) {
            existing = effect;
        }
    }
    if (m_lightEffectPanel) {
        // Applied: the section shows the project's effect again.
        m_lightEffectPanel->setEffect(effect);
    }
    recordSpeakerLightsEdit(edit::setSpeakerLights(*m_document, std::move(settings)),
                            tr("Changed light effect %1").arg(QString::fromStdString(effect.name)));
}

void MainWindow::setSelectedFramesLight(std::size_t channel, const document::LightAction& action)
{
    if (!m_document || channel >= document::kLightChannels) {
        return;
    }
    std::vector<std::size_t> changed;
    for (std::size_t frameIndex : selectedFrameIndices()) {
        document::LightActions lights = m_document->frameLights(frameIndex);
        if (lights[channel] == action) {
            continue;
        }
        lights[channel] = action;
        auto command = std::make_unique<edit::FrameLightsCommand>(
            frameIndex, m_document->frameLights(frameIndex), lights);
        command->apply(*m_document);
        const edit::HistoryKey key = edit::historyKey(command->target());
        pushToHistory(key, std::move(command));
        changed.push_back(frameIndex);
    }
    if (changed.empty()) {
        return;
    }
    refreshAfterLightsChanged(changed);
    updateFrameLightControls();
    if (m_lightEffectsPanel) {
        m_lightEffectsPanel->setProject(&*m_document);
    }
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(tr("Speaker light %1 of %n frame(s) changed", "",
                                static_cast<int>(changed.size()))
                                 .arg(channel + 1),
                             2500);
}

void MainWindow::suggestSelectedFramesLights()
{
    if (!m_document) {
        return;
    }
    const render::Resolution plane = m_document->geometry().nativePlane();
    std::vector<std::size_t> changed;
    for (std::size_t frameIndex : selectedFrameIndices()) {
        const document::LightActions suggested = edit::suggestedAmbientLighting(
            composeResolvedFrame(*m_document, frameIndex, plane, true));
        document::LightActions lights = m_document->frameLights(frameIndex);
        lights[0] = suggested[0];
        lights[1] = suggested[1];
        if (lights == m_document->frameLights(frameIndex)) {
            continue;
        }
        auto command = std::make_unique<edit::FrameLightsCommand>(
            frameIndex, m_document->frameLights(frameIndex), lights);
        command->apply(*m_document);
        const edit::HistoryKey key = edit::historyKey(command->target());
        pushToHistory(key, std::move(command));
        changed.push_back(frameIndex);
    }
    refreshAfterLightsChanged(changed);
    updateFrameLightControls();
    updateUndoRedoActions();
    updateProjectHeaderStatus();
    statusBar()->showMessage(
        tr("Speaker lights 1 and 2 of %n frame(s) set from their colors", "",
           static_cast<int>(changed.size())),
        2500);
}

void MainWindow::addAmbientLightingToFramesWithoutLights()
{
    if (!m_document) {
        return;
    }
    const std::size_t frames = edit::framesWithoutLights(*m_document).size();
    if (frames == 0) {
        QMessageBox::information(this, tr("Ambient Lighting"),
                                 tr("Every frame does something to the speaker lights already."));
        return;
    }
    QMessageBox confirm(
        QMessageBox::Question, tr("Ambient Lighting"),
        tr("Set speaker lights 1 and 2 of %n frame(s) that do nothing to the speaker lights yet "
           "to the colors of their left and right part?",
           "", static_cast<int>(frames)),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    confirm.setDefaultButton(QMessageBox::Yes);
    if (confirm.exec() != QMessageBox::Yes) {
        return;
    }
    const render::Resolution plane = m_document->geometry().nativePlane();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto command = edit::addAmbientLightingToFramesWithoutLights(
        *m_document, [this, plane](std::size_t frameIndex) {
            return composeResolvedFrame(*m_document, frameIndex, plane, true);
        });
    QApplication::restoreOverrideCursor();
    recordSpeakerLightsEdit(std::move(command),
                            tr("Ambient lighting for %n frame(s)", "", static_cast<int>(frames)));
}

void MainWindow::updateAmbientLighting()
{
    if (!m_document) {
        return;
    }
    const render::Resolution plane = m_document->geometry().nativePlane();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto command = edit::updateAmbientLighting(*m_document, [this, plane](std::size_t frameIndex) {
        return composeResolvedFrame(*m_document, frameIndex, plane, true);
    });
    QApplication::restoreOverrideCursor();
    if (!command) {
        statusBar()->showMessage(tr("The ambient lighting is up to date"), 3000);
        return;
    }
    const int frames = static_cast<int>(command->target().frameIndices.size());
    recordSpeakerLightsEdit(std::move(command),
                            tr("Ambient lighting of %n frame(s) updated", "", frames));
}

void MainWindow::refreshScenes()
{
    // The scene whose frames are listed is gone: back to all frames.
    if (m_sceneTimeline &&
        (!m_document || !m_document->colorizationSettings() ||
         !m_document->colorizationSettings()->scene(*m_sceneTimeline))) {
        const bool shown = m_frameFilterComboBox &&
                           m_frameFilterComboBox->currentData().toInt() ==
                               static_cast<int>(FrameFilter::SceneTimeline);
        setSceneTimelineEntry(std::nullopt);
        if (shown && m_document) {
            refreshFrameList(currentFrameIndex());
        }
    }
    if (m_scenesPanel) {
        m_scenesPanel->setProject(m_document ? &*m_document : nullptr);
        applyAssetFilter();
        refreshSelectedAssetFilter();
    }
    updateSceneSettingsSection(true);
    refreshSpeakerLights();
}

void MainWindow::updateSceneSettingsSection(bool reload)
{
    if (!m_sceneSettingsPanel || !m_inspectorSections) {
        return;
    }
    // The scene of the frame on the frame canvas (not while a background or a sprite is
    // edited).
    std::optional<document::SceneDefinition> scene;
    const std::optional<std::size_t> frame = currentFrameIndex();
    if (m_document && frame && !spriteCanvasShown() && !isBackgroundImageTargetActive()) {
        if (const std::optional<document::SceneFrameRef>& ref = m_document->sceneFrame(*frame);
            ref && m_document->colorizationSettings()) {
            if (const document::SceneDefinition* definition =
                    m_document->colorizationSettings()->scene(ref->sceneId)) {
                scene = *definition;
            }
        }
    }
    const int index = m_inspectorSections->indexOf(m_sceneSettingsPanel);
    // Opened when it comes, as the sections of a background or a sprite being edited.
    if (scene && m_inspectorSections->isSectionHidden(index)) {
        m_inspectorSections->setSectionOpen(index, true);
    }
    m_inspectorSections->setSectionHidden(index, !scene);
    const std::optional<uint16_t> shownId =
        scene ? std::optional<uint16_t>(scene->sceneId) : std::nullopt;
    // Unapplied changes stay while the frames of the same scene are shown.
    if (!reload && shownId == m_sceneSettingsPanel->sceneId()) {
        return;
    }
    m_sceneSettingsPanel->setScene(
        scene, [this, shownId](const document::SceneDefinition& candidate) {
            return m_document && shownId
                       ? edit::sceneProblem(*m_document, candidate, *shownId)
                       : std::string();
        });
}

void MainWindow::openProjectSettings()
{
    if (!m_document) {
        return;
    }
    ProjectSettingsDialog dialog(*m_document, this);
    if (dialog.exec() != QDialog::Accepted || !m_document) {
        return;
    }
    document::ColorizationSettings settings =
        m_document->colorizationSettings().value_or(document::ColorizationSettings{});
    settings.scalingAlgorithm = dialog.scalingAlgorithm();
    settings.shadowOffset = dialog.shadowOffset();
    try {
        recordSceneEdit(
            edit::setProjectSettings(*m_document, std::move(settings), dialog.libserumTarget()));
        document::SpeakerLightSettings lights = m_document->speakerLights();
        lights.defaults = dialog.lightDefaults();
        recordSpeakerLightsEdit(edit::setSpeakerLights(*m_document, std::move(lights)),
                                tr("Changed the speaker lights' defaults"));
        if (auto command = edit::setLicense(*m_document, dialog.license())) {
            m_droppedStructureSteps =
                pushToHistory(edit::structureHistoryKey(), std::move(command));
            updateUndoRedoActions();
            updateProjectHeaderStatus();
            showStructureEditStatus(tr("Changed the license"), 3000);
        }
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, tr("Project Settings"), QString::fromUtf8(ex.what()));
    }
}

void MainWindow::addScene()
{
    if (!m_document) {
        return;
    }
    const std::optional<uint16_t> freeId = edit::nextFreeSceneId(*m_document);
    if (!freeId) {
        QMessageBox::information(this, tr("Add Scene"),
                                 tr("Every scene ID from %1 to %2 is taken.")
                                     .arg(document::kFirstSceneId)
                                     .arg(document::kLastSceneId));
        return;
    }
    document::SceneDefinition scene;
    scene.sceneId = *freeId;
    SceneDialog dialog(
        scene,
        [this](const document::SceneDefinition& candidate) {
            return edit::sceneProblem(*m_document, candidate);
        },
        this);
    dialog.setWindowTitle(tr("Add Scene"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    try {
        recordSceneEdit(edit::addScene(*m_document, dialog.scene()));
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, tr("Add Scene"), QString::fromUtf8(ex.what()));
    }
}

void MainWindow::openScene(uint16_t sceneId)
{
    if (!m_document) {
        return;
    }
    const std::vector<std::size_t> frames = edit::sceneFrames(*m_document, sceneId);
    if (frames.empty()) {
        statusBar()->showMessage(tr("Scene %1 has no frames in the project").arg(sceneId), 3000);
        return;
    }
    // The timeline and the frame before, also when the frame has not rested yet: < goes back
    // to them.
    noteCanvasPlace(true);
    m_showingCanvasPlace = true;
    showSceneFrames(sceneId);
    // On the frame canvas, as a double click on a frame does.
    leaveSpriteCanvas();
    if (activePaintTarget() == PaintTarget::Background) {
        editFrameInsteadOfBackground();
    }
    goToFrame(frames.front());
    m_showingCanvasPlace = false;
    updateInspectorSections();
    noteCanvasPlace(true);
}

void MainWindow::changeScene(uint16_t sceneId, const document::SceneDefinition& scene)
{
    if (!m_document) {
        return;
    }
    std::size_t lost = 0;
    for (std::size_t frame : edit::sceneFrames(*m_document, sceneId)) {
        const document::SceneFrameRef& ref = *m_document->sceneFrame(frame);
        if (ref.group > scene.frameGroups || ref.frameIndex >= scene.frameCount) {
            ++lost;
        }
    }
    if (lost > 0 &&
        !confirmDeletion(tr("Edit Scene %1").arg(sceneId),
                         tr("The scene has fewer frames now. Delete %n of its frames?", "",
                            static_cast<int>(lost)),
                         tr("Their colorization is deleted with them; Undo brings them "
                            "back."))) {
        return;
    }
    try {
        recordSceneEdit(edit::changeScene(*m_document, sceneId, scene));
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, tr("Edit Scene"), QString::fromUtf8(ex.what()));
    }
}

void MainWindow::deleteScene(uint16_t sceneId)
{
    if (!m_document) {
        return;
    }
    const std::size_t frames = edit::sceneFrames(*m_document, sceneId).size();
    bool withFrames = false;
    if (frames > 0) {
        QMessageBox box(QMessageBox::Question, tr("Delete Scene %1").arg(sceneId),
                        tr("Delete its %n frame(s) too?", "", static_cast<int>(frames)),
                        QMessageBox::NoButton, this);
        QPushButton* deleteFrames = box.addButton(tr("Delete Scene and Frames"),
                                                  QMessageBox::DestructiveRole);
        QPushButton* keepFrames = box.addButton(tr("Keep the Frames"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(keepFrames);
        box.exec();
        if (box.clickedButton() != deleteFrames && box.clickedButton() != keepFrames) {
            return;
        }
        withFrames = box.clickedButton() == deleteFrames;
    }
    try {
        recordSceneEdit(edit::removeScene(*m_document, sceneId, withFrames));
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, tr("Delete Scene"), QString::fromUtf8(ex.what()));
    }
}

void MainWindow::setSceneTimelineEntry(std::optional<uint16_t> sceneId)
{
    if (!m_frameFilterComboBox) {
        return;
    }
    m_sceneTimeline = sceneId;
    const int existing =
        m_frameFilterComboBox->findData(static_cast<int>(FrameFilter::SceneTimeline));
    if (!sceneId) {
        if (existing >= 0) {
            const QSignalBlocker blocker(m_frameFilterComboBox);
            if (m_frameFilterComboBox->currentIndex() == existing) {
                m_frameFilterComboBox->setCurrentIndex(0);
            }
            m_frameFilterComboBox->removeItem(existing);
        }
        return;
    }
    const QString text = tr("Frames of scene %1").arg(*sceneId);
    if (existing >= 0) {
        m_frameFilterComboBox->setItemText(existing, text);
        return;
    }
    const QSignalBlocker blocker(m_frameFilterComboBox);
    m_frameFilterComboBox->addItem(text, static_cast<int>(FrameFilter::SceneTimeline));
}

void MainWindow::showSceneFrames(uint16_t sceneId)
{
    if (!m_frameFilterComboBox) {
        return;
    }
    setSceneTimelineEntry(sceneId);
    const int index =
        m_frameFilterComboBox->findData(static_cast<int>(FrameFilter::SceneTimeline));
    if (index < 0) {
        return;
    }
    if (m_frameFilterComboBox->currentIndex() == index) {
        refreshFrameList(currentFrameIndex());
        updateHistoryButtons();
    } else {
        m_frameFilterComboBox->setCurrentIndex(index);
    }
}

void MainWindow::recordSceneEdit(std::unique_ptr<edit::SceneEditCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const edit::SceneEditCommand& applied = *command;
    m_droppedStructureSteps = pushToHistory(edit::structureHistoryKey(), std::move(command));
    refreshAfterSceneEdit(applied);
    updateUndoRedoActions();
    QString text = sceneEditText(applied);
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    showStructureEditStatus(text, 3000);
}

void MainWindow::refreshAfterSceneEdit(const edit::SceneEditCommand& command)
{
    if (!m_document) {
        return;
    }
    refreshScenes();
    updateFrameFilterChoices();
    updateRuntimeViewControls();
    updateProjectHeaderStatus();
    // The Frame section lists the scenes.
    updateFrameMetadataControls();
    if (command.kind() == edit::SceneEditCommand::Kind::LibserumTarget) {
        // The frames' sprite slots and the limits of sprites and backgrounds.
        updateSpriteControls();
        updateStructureControls();
    }
    if (!command.changesFrames()) {
        return;
    }
    setPlaybackRunning(false);
    m_pasteMode.cancel();
    m_canvasEdit.cancelFreeGradient();
    m_canvasEdit.clearSelection(canvasEditContext());
    m_canvasEdit.clearCaptureSelection();
    m_spriteDependentViewsDirty = false;
    const std::size_t current = currentFrameIndex().value_or(0);
    refreshFrameList(std::nullopt);
    selectFrameNear(std::min(current, m_document->frameCount() - 1));
    refreshComponentBrowsers();
    updateSectionControls();
    updateFrameMetadataControls();
    updateColorRotationControls();
    updateSpriteControls();
    updatePaintTargetMode();
    if (m_preview) {
        m_preview->update();
    }
    updateFrameStatusText();
    updatePlaybackControls();
    updateStructureControls();
}

void MainWindow::recordPluginEdit(std::unique_ptr<edit::EditCommand> command)
{
    if (!m_document || !command) {
        return;
    }
    const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(command.get());
    const QString name = batch ? QString::fromStdString(batch->name()) : QString();
    m_droppedStructureSteps = pushToHistory(edit::structureHistoryKey(), std::move(command));
    refreshAfterEditBatch();
    updateUndoRedoActions();
    showStructureEditStatus(name, 3000);
}

void MainWindow::refreshAfterEditBatch()
{
    if (!m_document) {
        return;
    }
    setPlaybackRunning(false);
    m_pasteMode.cancel();
    m_canvasEdit.cancelFreeGradient();
    m_canvasEdit.clearSelection(canvasEditContext());
    m_canvasEdit.clearCaptureSelection();
    m_spriteDependentViewsDirty = false;
    const std::size_t current = currentFrameIndex().value_or(0);
    refreshFrameList(std::nullopt);
    selectFrameNear(std::min(current, m_document->frameCount() - 1));
    refreshComponentBrowsers();
    updateSectionControls();
    updateFrameMetadataControls();
    updateBackgroundControls();
    updateDynamicColorControls();
    updateColorRotationControls();
    updateSpriteControls();
    updatePaintTargetMode();
    if (m_preview) {
        m_preview->update();
    }
    updateFrameStatusText();
    updatePlaybackControls();
    updateStructureControls();
    updateProjectHeaderStatus();
}

void MainWindow::updateRuntimeSceneFile()
{
    // The <rom>.pup.csv beside the project, as the .cROMc export takes it: the Runtime view and
    // the frame tester then play its scenes (background scenes too) as a machine does.
    std::string sceneFile;
    if (m_document && !isUntitledProject()) {
        const QString directory = QFileInfo(m_currentCromPath).absolutePath();
        if (const std::optional<std::filesystem::path> csv = runtime::findCaseInsensitive(
                platform::pathFromUtf8(directory.toStdString()), m_document->romName() + ".pup.csv")) {
            sceneFile = runtime::libserumPath(*csv).value_or(std::string());
        }
    }
    runtime::RuntimePreview::instance().setSceneFile(sceneFile);
}

void MainWindow::startProjectClock()
{
    m_projectTimeAtOpenMs = m_document ? m_document->editorMetadata().timeElapsedMs : 0;
    m_sessionClock.restart();
    m_activityClock.start(0);
    m_clockTimer.start();
    m_lastInputMs = 0;
    m_nextPackPassMs = 0;
    updateTimeLabel();
}

void MainWindow::packIdleBlocks()
{
    constexpr qint64 kIdleMs = 2000;
    constexpr qint64 kPassIntervalMs = 30000;
    // Not while a dialog or a project task (which may read the document on another thread)
    // runs, nor while the author works or playback runs (which counts as activity).
    if (!m_document || !m_sessionClock.isValid() || QApplication::activeModalWidget()) {
        return;
    }
    const qint64 now = m_sessionClock.elapsed();
    if (now - m_lastInputMs < kIdleMs || now < m_nextPackPassMs) {
        return;
    }
    std::vector<std::size_t> keep = selectedFrameIndices();
    if (const std::optional<std::size_t> current = currentFrameIndex()) {
        keep.push_back(*current);
    }
    const document::ProjectDocument::PackProgress progress =
        m_document->packBlocks(keep, std::chrono::milliseconds(8));
    if (progress.done) {
        m_nextPackPassMs = now + kPassIntervalMs;
    }
}

void MainWindow::noteActivity()
{
    if (!m_sessionClock.isValid()) {
        return;
    }
    const bool wasPaused = m_activityClock.isPaused(m_sessionClock.elapsed());
    m_activityClock.activity(m_sessionClock.elapsed());
    m_lastInputMs = m_sessionClock.elapsed();
    if (wasPaused) {
        updateTimeLabel();
    }
}

uint32_t MainWindow::projectTimeMs() const
{
    if (!m_sessionClock.isValid()) {
        return m_projectTimeAtOpenMs;
    }
    // The cRP stores it in 32 bits (Win32 DWORD).
    const uint64_t total =
        uint64_t{m_projectTimeAtOpenMs} +
        static_cast<uint64_t>(m_activityClock.elapsedMs(m_sessionClock.elapsed()));
    return static_cast<uint32_t>(std::min<uint64_t>(total, 0xffffffffu));
}

void MainWindow::updateTimeLabel()
{
    if (!m_timeLabel) {
        return;
    }
    if (!m_document) {
        m_timeLabel->clear();
        return;
    }
    const auto clock = [](uint64_t ms) {
        const uint64_t seconds = ms / 1000;
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    };
    const int64_t now = m_sessionClock.isValid() ? m_sessionClock.elapsed() : 0;
    const QString text =
        tr("Time: Project %1 / Session %2")
            .arg(clock(projectTimeMs()),
                 clock(static_cast<uint64_t>(m_activityClock.elapsedMs(now))));
    m_timeLabel->setText(m_activityClock.isPaused(now) ? tr("%1 (paused)").arg(text) : text);
}

void MainWindow::addSourceFrame()
{
    const std::optional<std::size_t> frameIndex = currentFrameIndex();
    if (!m_document || !frameIndex) {
        return;
    }
    std::string problem;
    std::optional<document::IndexedFrame> original =
        edit::sourceFrameFromColors(*m_document, *frameIndex, &problem);
    if (!original) {
        QMessageBox::information(
            this, tr("New Frame From Original Colors"),
            tr("Frame %1 cannot become an original frame: %2.\n\nPaint its picture in the "
               "plain original colors only (the %3 colors of an uncolorized frame), on the "
               "plane of the original frames.")
                .arg(static_cast<qulonglong>(*frameIndex))
                .arg(QString::fromStdString(problem))
                .arg(m_document->colorCount()));
        return;
    }
    try {
        std::vector<document::FrameRecord> frames;
        frames.push_back(m_document->makeUncolorizedFrame(std::move(*original),
                                                          edit::kSourceFrameDurationMs));
        recordStructureEdit(edit::addFrames(*m_document, std::move(frames)));
        showStructureEditStatus(tr("Added frame %1 with the picture of frame %2 as its original")
                                    .arg(static_cast<qulonglong>(m_document->frameCount() - 1))
                                    .arg(static_cast<qulonglong>(*frameIndex)),
                                4000);
    } catch (const std::exception& ex) {
        statusBar()->showMessage(QString::fromUtf8(ex.what()), 4000);
    }
}

void MainWindow::updateStructureControls()
{
    const bool hasDocument = m_document.has_value();
    if (m_projectSettingsAction) {
        m_projectSettingsAction->setEnabled(hasDocument);
    }
    if (m_ambientLightingAction) {
        m_ambientLightingAction->setEnabled(hasDocument);
    }
    if (m_updateAmbientLightingAction) {
        m_updateAmbientLightingAction->setEnabled(hasDocument);
    }
    const bool canDeleteFrames =
        hasDocument && m_document->frameCount() > 1 && !selectedFrameIndices().empty();
    if (m_deleteFramesAction) {
        m_deleteFramesAction->setEnabled(canDeleteFrames);
    }
    if (m_sourceFrameAction) {
        m_sourceFrameAction->setEnabled(hasDocument && currentFrameIndex().has_value());
    }
    if (m_deleteFramesListAction) {
        m_deleteFramesListAction->setEnabled(canDeleteFrames);
    }
    if (m_deleteShortFramesAction) {
        m_deleteShortFramesAction->setEnabled(canDeleteFrames);
    }
    for (QAction* action : {m_moveFramesUpAction, m_moveFramesDownAction, m_moveFramesToAction}) {
        if (action) {
            action->setEnabled(canDeleteFrames);
        }
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
        state.maxRecordCount = hasDocument ? m_document->limits().maxBackgrounds : 0;
        state.hasSource = currentFrameIndex().has_value();
        state.hasSelection = selectedBackgroundIndex().has_value();
        m_backgroundPage->refresh(state);
    }
    if (m_spritePage) {
        state.recordCount = hasDocument ? m_document->spriteCount() : 0;
        state.maxRecordCount = hasDocument ? m_document->limits().maxSprites : 0;
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
            // The thumbnail shows the zones, and the list's "Dyn" follows them; so does the
            // list of the frame's sets.
            for (std::size_t frameIndex : target.frameIndices) {
                updateFrameListItem(frameIndex);
                updateInactiveFrameMarker(frameIndex);
            }
            updateDynamicColorControls();
            [[fallthrough]];
        case edit::EditKind::ComparisonMask:
        case edit::EditKind::BackgroundMask:
            m_preview->update();
            updateProjectHeaderStatus();
            if (target.kind == edit::EditKind::BackgroundMask) {
                // Where the background shows changes the frame's picture in the strip.
                for (std::size_t frameIndex : target.frameIndices) {
                    updateFrameThumbnail(frameIndex);
                }
            }
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
            // A tag that comes or goes: the frame filter's entries follow.
            if (dynamic_cast<const edit::FrameTagsCommand*>(&command)) {
                updateFrameFilterChoices();
            }
            if (dynamic_cast<const edit::FrameLightsCommand*>(&command)) {
                refreshAfterLightsChanged(target.frameIndices);
            }
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
            // Remove BG follows the backgrounds the selected frames have.
            updateBackgroundControls();
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
            } else if (const auto* move = dynamic_cast<const edit::FrameMoveCommand*>(&command)) {
                refreshAfterFrameMove(*move);
            } else if (const auto* scene =
                           dynamic_cast<const edit::SceneEditCommand*>(&command)) {
                refreshAfterSceneEdit(*scene);
            } else if (dynamic_cast<const edit::EditBatchCommand*>(&command)) {
                refreshAfterEditBatch();
            } else if (dynamic_cast<const edit::SpeakerLightsCommand*>(&command)) {
                refreshSpeakerLights();
                refreshAfterLightsChanged(target.frameIndices);
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
        case edit::EditKind::HelperLines:
            refreshHelperLines();
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
    std::vector<edit::HistoryKey> keys{edit::structureHistoryKey(), edit::sectionsHistoryKey(),
                                       edit::helperLinesHistoryKey()};
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

std::size_t MainWindow::pushToHistory(const edit::HistoryKey& key,
                                      std::unique_ptr<edit::EditCommand> command)
{
    return m_actionGroups.push(key, std::move(command));
}

QString MainWindow::undoRedoStatusText(const edit::EditCommand& command, bool undoing) const
{
    const QString verb = undoing ? tr("Undo") : tr("Redo");
    if (const auto* structure = dynamic_cast<const edit::StructureCommand*>(&command)) {
        return tr("%1: %2").arg(verb, structureCommandText(*structure));
    }
    if (const auto* move = dynamic_cast<const edit::FrameMoveCommand*>(&command)) {
        return tr("%1: move %2 frame(s)")
            .arg(verb)
            .arg(static_cast<int>(move->movedFrames().size()));
    }
    if (const auto* scene = dynamic_cast<const edit::SceneEditCommand*>(&command)) {
        return tr("%1: %2").arg(verb, sceneEditText(*scene));
    }
    if (const auto* batch = dynamic_cast<const edit::EditBatchCommand*>(&command)) {
        return tr("%1: %2").arg(verb, QString::fromStdString(batch->name()));
    }
    return verb + editKindStatusSuffix(command, showsHdAndSd());
}

void MainWindow::undo(bool anywhere)
{
    runUndoRedo(anywhere, true);
}

void MainWindow::redo(bool anywhere)
{
    runUndoRedo(anywhere, false);
}

void MainWindow::runUndoRedo(bool anywhere, bool undoing)
{
    if (!m_document) {
        return;
    }
    std::vector<edit::HistoryKey> keys = currentHistoryKeys();
    if (anywhere) {
        keys = m_undoHistories.keys();
        const std::optional<edit::HistoryKey> key = undoing
                                                        ? m_undoHistories.latestUndoKey(keys)
                                                        : m_undoHistories.latestRedoKey(keys);
        if (!key) {
            return;
        }
        goToHistory(*key);
        keys = {*key};
    }
    // Each command the step ran (several for one action over several frames) refreshes what
    // it changed right away: a later command of the step can drop its history.
    QString text;
    std::size_t ran = 0;
    const auto refresh = [&](const edit::EditCommand& command) {
        if (ran++ == 0) {
            text = undoRedoStatusText(command, undoing);
        }
        refreshAfterEdit(command);
    };
    try {
        if (undoing) {
            (void)m_undoHistories.undoLatest(keys, *m_document, refresh);
        } else {
            (void)m_undoHistories.redoLatest(keys, *m_document, refresh);
        }
    } catch (const std::exception& ex) {
        // The command could not run and stays where it was (UndoHistories leaves it on its
        // stack), and the document is unchanged.
        updateUndoRedoActions();
        statusBar()->showMessage(
            (undoing ? tr("Undo failed, the step stays in the history: %1")
                     : tr("Redo failed, the step stays in the history: %1"))
                .arg(QString::fromUtf8(ex.what())),
            6000);
        return;
    }
    if (ran == 0) {
        return;
    }
    updateUndoRedoActions();
    if (ran > 1) {
        text = tr("%1 (one step of %2 changes)").arg(text).arg(static_cast<qulonglong>(ran));
    }
    statusBar()->showMessage(text, 2500);
}

void MainWindow::goToHistory(const edit::HistoryKey& key)
{
    if (!m_document) {
        return;
    }
    switch (key.kind) {
        case edit::EditKind::FramePixels:
        case edit::EditKind::DynamicMask:
        case edit::EditKind::DynamicColor:
        case edit::EditKind::DynamicShadow:
        case edit::EditKind::BackgroundMask:
        case edit::EditKind::FrameAttributes:
            break;
        default:
            // Masks, backgrounds, sprites, palettes and the project's own histories have no
            // frame to show.
            return;
    }
    if (key.index >= m_document->frameCount()) {
        return;
    }
    if (currentFrameIndex() != key.index) {
        goToFrame(key.index);
    }
    // The plane of a pixel edit, where the frame has both.
    if (key.kind == edit::EditKind::FramePixels && key.resolution != m_activeResolution &&
        !isHdOnlyProject() && !isOriginalPlaneOnlyProject() &&
        (key.resolution == render::Resolution::Sd || m_document->hasHdFrame(key.index))) {
        QToolButton* button =
            key.resolution == render::Resolution::Hd ? m_hdButton : m_sdButton;
        if (button && button->isEnabled()) {
            button->click();
        }
    }
}

void MainWindow::updateUndoRedoActions()
{
    // An edit may have changed the colors of the frame shown.
    m_frameLightMarksTimer.start();
    if (m_undoAnywhereAction) {
        const std::vector<edit::HistoryKey> all = m_undoHistories.keys();
        m_undoAnywhereAction->setEnabled(m_document &&
                                         m_undoHistories.latestUndoKey(all).has_value());
        m_redoAnywhereAction->setEnabled(m_document &&
                                         m_undoHistories.latestRedoKey(all).has_value());
    }
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
        setStatusText(tr("No project loaded"));
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
    setStatusText(tr("%1: %2 frames%3, %4 backgrounds, %5 sprites%6%7")
                      .arg(QString::fromStdString(m_document->romName()))
                      .arg(static_cast<int>(m_document->frameCount()))
                      .arg(showsHdAndSd() ? tr(", %1 HD").arg(hdCount) : QString())
                      .arg(static_cast<int>(m_document->backgroundCount()))
                      .arg(static_cast<int>(m_document->spriteCount()))
                      .arg(notRenderedCount > 0
                               ? tr(", %1 not rendered at runtime")
                                     .arg(notRenderedCount)
                               : QString())
                      .arg(m_document->isDirty() ? tr(" (modified)") : QString()));
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
    return m_dmdRenderOptions;
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
