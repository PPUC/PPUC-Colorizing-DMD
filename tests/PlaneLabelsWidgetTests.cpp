// Where the UI says "HD" and "SD": only in a project that has HD as an extra resolution. The
// frame list builds its thumbnails for the rows in view (LazyListIcons).

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameListWidget.h"
#include "FrameThumbnail.h"
#include "CanvasHistory.h"
#include "LazyListIcons.h"
#include "panels/BackgroundPanel.h"
#include "panels/ColorRotationPanel.h"
#include "panels/FramePanel.h"
#include "panels/SpritePanel.h"
#include "PlaneLabels.h"
#include "SectionStack.h"

using colorizing::document::ProjectDocument;
using colorizing::render::PlaneLayout;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;

namespace document = colorizing::document;
namespace app = colorizing::app;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: plane labels: " << message << '\n';
        std::exit(1);
    }
}

// Frame 0 uses background 0 and sprite 0, frame 1 background 1 and sprite 1. Where the layout
// has an editable extra plane, frame 0, background 0 and sprite 0 have data on it and frame 1,
// background 1 and sprite 1 do not.
ProjectDocument makeDocument(PlaneLayout layout)
{
    const ProjectGeometry geometry(layout);
    const Size original = geometry.planeSize(Resolution::Sd);
    const Size extra = geometry.planeSize(Resolution::Hd);
    const Size romFrame = geometry.romFrameSize();
    const bool hasExtra = geometry.isEditable(Resolution::Hd);
    const Size cell{static_cast<int>(document::kMaxSpriteWidth),
                    static_cast<int>(document::kMaxSpriteHeight)};

    document::ProjectSnapshot snapshot;
    snapshot.romName = "labels";
    snapshot.colorCount = 4;
    snapshot.geometry = geometry;
    for (int index = 0; index < 2; ++index) {
        const bool withExtra = hasExtra && index == 0;
        document::SpriteRecord sprite;
        sprite.hasHd = withExtra;
        sprite.originalSd = document::makeIndexedFrame(cell, 255);
        sprite.colorizedSd = document::makeRgb565Frame(cell, 0);
        sprite.maskHd = document::makeIndexedFrame(cell, 255);
        sprite.colorizedHd = document::makeRgb565Frame(cell, 0);
        sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
        sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
        sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, {});
        snapshot.sprites.push_back(sprite);

        document::BackgroundRecord background;
        background.colorizedSd = document::makeRgb565Frame(original, 0x1111);
        if (withExtra) {
            background.colorizedHd = document::makeRgb565Frame(extra, 0x2222);
        }
        snapshot.backgrounds.push_back(background);

        document::ProjectFrameSnapshot frame;
        frame.originalSd = document::makeIndexedFrame(romFrame, 0);
        frame.colorizedSd = document::makeRgb565Frame(original, 0x3333);
        if (withExtra) {
            frame.colorizedHd = document::makeRgb565Frame(extra, 0x4444);
        }
        frame.comparisonMaskId = 255;
        document::FrameBackgroundData backgroundData;
        backgroundData.backgroundId = static_cast<uint16_t>(index);
        backgroundData.maskSd = document::makeIndexedFrame(original, 0);
        backgroundData.maskHd = document::makeIndexedFrame(extra, 0);
        frame.backgroundData = backgroundData;
        frame.spriteReferences.assign(
            document::kMaxSpritesPerFrame,
            document::FrameSpriteReference{0xffff,
                                           0,
                                           0,
                                           static_cast<uint16_t>(romFrame.width - 1),
                                           static_cast<uint16_t>(romFrame.height - 1)});
        frame.spriteReferences[0].spriteId = static_cast<uint16_t>(index);
        snapshot.frames.push_back(frame);
    }
    return ProjectDocument::fromSnapshot(snapshot);
}

// Whether a widget the user can see says "HD" or "SD" in a text or a tool tip.
bool mentionsHdOrSd(const QWidget& parent)
{
    static const QRegularExpression word(QStringLiteral("\\b(HD|SD)\\b"));
    for (const QWidget* widget : parent.findChildren<QWidget*>()) {
        if (widget->isHidden()) {
            continue;
        }
        QStringList texts{widget->toolTip()};
        if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
            texts << button->text();
        }
        if (const auto* label = qobject_cast<const QLabel*>(widget)) {
            texts << label->text();
        }
        if (const auto* combo = qobject_cast<const QComboBox*>(widget)) {
            for (int index = 0; index < combo->count(); ++index) {
                texts << combo->itemText(index);
            }
        }
        for (const QString& text : texts) {
            if (word.match(text).hasMatch()) {
                return true;
            }
        }
    }
    return false;
}

QToolButton* buttonWithToolTipStart(QWidget& parent, const QString& start)
{
    for (QToolButton* button : parent.findChildren<QToolButton*>()) {
        if (button->toolTip().startsWith(start)) {
            return button;
        }
    }
    expect(false, "button " + start.toStdString());
    return nullptr;
}

bool hasLabel(const QWidget& parent, const QString& text)
{
    for (const QLabel* label : parent.findChildren<QLabel*>()) {
        if (label->text() == text) {
            return true;
        }
    }
    return false;
}

void testLabels()
{
    const ProjectDocument ordinary = makeDocument(PlaneLayout::Ordinary);
    const ProjectDocument nativeExtra = makeDocument(PlaneLayout::NativeExtra);
    const ProjectDocument nativeOriginal = makeDocument(PlaneLayout::NativeOriginal);

    expect(app::hasHdAsExtraResolution(nullptr) && app::hasHdAsExtraResolution(&ordinary) &&
               !app::hasHdAsExtraResolution(&nativeExtra) &&
               !app::hasHdAsExtraResolution(&nativeOriginal),
           "HD is an extra resolution in the ordinary layout only");
    expect(app::planeLabel(ordinary, Resolution::Sd) == QStringLiteral("SD") &&
               app::planeLabel(ordinary, Resolution::Hd) == QStringLiteral("HD") &&
               app::planeLabel(nativeExtra, Resolution::Hd) == QStringLiteral("256x64") &&
               app::planeLabel(nativeOriginal, Resolution::Sd) == QStringLiteral("192x64"),
           "a single plane is named by its size");
    expect(app::nativePlaneText(ordinary) == QStringLiteral("128x32") &&
               app::nativePlaneText(nativeExtra) == QStringLiteral("256x64") &&
               app::nativePlaneText(nativeOriginal) == QStringLiteral("192x64"),
           "the native plane's size");
    expect(app::extraDataSuffix(ordinary, true) == QStringLiteral(" (HD)") &&
               app::extraDataSuffix(ordinary, false).isEmpty() &&
               app::extraDataSuffix(nativeExtra, true).isEmpty() &&
               app::extraDataSuffix(nativeExtra, false) == QStringLiteral(" (no picture)") &&
               app::extraDataSuffix(nativeOriginal, true).isEmpty() &&
               app::extraDataSuffix(nativeOriginal, false).isEmpty(),
           "list entries mark HD in ordinary projects and a missing picture in 256x64 ones");
}

void testFramePanelActions()
{
    const ProjectDocument ordinary = makeDocument(PlaneLayout::Ordinary);
    const ProjectDocument nativeExtra = makeDocument(PlaneLayout::NativeExtra);
    const ProjectDocument nativeOriginal = makeDocument(PlaneLayout::NativeOriginal);

    app::FramePanel panel;
    const auto refresh = [&](const ProjectDocument* document, std::size_t frameIndex) {
        app::FramePanelState state;
        state.document = document;
        if (document) {
            state.frameIndex = frameIndex;
        }
        state.hasSelectedFrames = document != nullptr;
        panel.refresh(state);
    };
    // The two buttons are told apart by what they were created as.
    QToolButton* create = buttonWithToolTipStart(panel, QStringLiteral("Create HD frame"));
    QToolButton* remove = buttonWithToolTipStart(panel, QStringLiteral("Remove HD frame"));
    const auto shown = [&](const QString& createText, const QString& removeText) {
        return create->isHidden() == createText.isEmpty() &&
               remove->isHidden() == removeText.isEmpty() &&
               (createText.isEmpty() || create->text() == createText) &&
               (removeText.isEmpty() || remove->text() == removeText);
    };

    refresh(nullptr, 0);
    expect(shown(QStringLiteral("Create HD"), QStringLiteral("Remove HD")) &&
               !create->isEnabled() && !remove->isEnabled(),
           "without a project both HD buttons are shown, disabled");

    refresh(&ordinary, 0);
    expect(shown(QStringLiteral("Create HD"), QStringLiteral("Remove HD")) &&
               !create->isEnabled() && remove->isEnabled(),
           "ordinary frame with HD: Remove HD");
    refresh(&ordinary, 1);
    expect(create->isEnabled() && !remove->isEnabled() &&
               create->toolTip() == QStringLiteral("Create HD frame from SD frame") &&
               remove->toolTip() == QStringLiteral("Remove HD frame data"),
           "ordinary frame without HD: Create HD");

    refresh(&nativeExtra, 0);
    expect(shown(QStringLiteral("Create 256x64 Frame"), QString()) && !create->isEnabled() &&
               !mentionsHdOrSd(panel),
           "256x64 project, complete frame: the repair is shown without HD wording, disabled; "
           "Remove is hidden");
    refresh(&nativeExtra, 1);
    expect(shown(QStringLiteral("Create 256x64 Frame"), QString()) && create->isEnabled() &&
               create->toolTip().contains(QStringLiteral("256x64 frame from the original")) &&
               !mentionsHdOrSd(panel),
           "256x64 project, frame without its picture: Create 256x64 Frame");

    refresh(&nativeOriginal, 0);
    expect(shown(QString(), QString()) && !mentionsHdOrSd(panel),
           "192x64 project: no Create or Remove button for a frame");

    refresh(&ordinary, 1);
    expect(shown(QStringLiteral("Create HD"), QStringLiteral("Remove HD")) &&
               create->isEnabled() &&
               create->toolTip() == QStringLiteral("Create HD frame from SD frame"),
           "back in an ordinary project both buttons are there again");
    refresh(&nativeOriginal, 0);
    refresh(nullptr, 0);
    expect(shown(QStringLiteral("Create HD"), QStringLiteral("Remove HD")),
           "closing a 192x64 project shows the ordinary buttons again");
}

void testListLabelsOfPanels()
{
    struct Expectation {
        PlaneLayout layout;
        QString withData;
        QString withoutData;
        QString slotRemark;
        bool mentions;
    };
    const std::vector<Expectation> expectations = {
        {PlaneLayout::Ordinary, QStringLiteral(" (HD)"), QString(), QString(), true},
        {PlaneLayout::NativeExtra,
         QString(),
         QStringLiteral(" (no picture)"),
         QStringLiteral(" no picture: frame not rendered"),
         false},
        {PlaneLayout::NativeOriginal, QString(), QString(), QString(), false},
    };
    for (const Expectation& expected : expectations) {
        const ProjectDocument document = makeDocument(expected.layout);
        const std::string name =
            app::nativePlaneText(document).toStdString() + " project: ";

        app::SpritePanel spritePanel;
        app::SpritePanelState spriteState;
        spriteState.document = &document;
        spriteState.frameIndex = 1;
        spriteState.spriteIndex = 0;
        spritePanel.refresh(spriteState);
        expect(hasLabel(spritePanel, QStringLiteral("Selected: 0") + expected.withData),
               name + "selected sprite with extra data");
        const QString bounds = QStringLiteral("0: Sprite 1 [0,0-%1,%2]")
                                   .arg(document.originalFrameSize().width - 1)
                                   .arg(document.originalFrameSize().height - 1);
        expect(hasLabel(spritePanel, bounds + expected.slotRemark),
               name + "the slot of a sprite without extra data");
        spriteState.spriteIndex = 1;
        spritePanel.refresh(spriteState);
        expect(hasLabel(spritePanel, QStringLiteral("Selected: 1") + expected.withoutData),
               name + "selected sprite without extra data");
        expect(mentionsHdOrSd(spritePanel) == expected.mentions,
               name + "the sprite page says HD only where the project has HD");

        app::BackgroundPanel backgroundPanel;
        app::BackgroundPanelState backgroundState;
        backgroundState.document = &document;
        backgroundState.selectedBackgroundIndex = 0;
        backgroundPanel.refresh(backgroundState);
        expect(hasLabel(backgroundPanel, QStringLiteral("Selected: 0") + expected.withData),
               name + "selected background with extra data");
        expect(mentionsHdOrSd(backgroundPanel) == expected.mentions,
               name + "the background page says HD only for an HD background");
        backgroundState.selectedBackgroundIndex = 1;
        backgroundPanel.refresh(backgroundState);
        expect(hasLabel(backgroundPanel, QStringLiteral("Selected: 1") + expected.withoutData),
               name + "selected background without extra data");

        app::ColorRotationPanel rotationPanel;
        rotationPanel.refresh(&document, 0, document.geometry().nativePlane());
        expect(mentionsHdOrSd(rotationPanel) == expected.mentions,
               name + "the rotation page names the SD/HD mode only where there is one");
        rotationPanel.refresh(nullptr, std::nullopt, Resolution::Sd);
        expect(mentionsHdOrSd(rotationPanel), "without a project the tool tip is the ordinary one");
    }
}

bool hasColor(const QImage& image, const QColor& color)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == color) {
                return true;
            }
        }
    }
    return false;
}

void testThumbnailBadges()
{
    const QSize size(160, 84);
    const QColor hdBadge(35, 95, 160);
    const QColor warningBadge(190, 45, 45);
    const auto thumbnail = [&](const ProjectDocument& document, std::size_t frameIndex) {
        return app::makeFrameThumbnail(document, frameIndex, size).pixmap(size).toImage();
    };

    const ProjectDocument ordinary = makeDocument(PlaneLayout::Ordinary);
    expect(hasColor(thumbnail(ordinary, 0), hdBadge) &&
               thumbnail(ordinary, 0).pixelColor(133, 5) == hdBadge &&
               thumbnail(ordinary, 0).pixelColor(131, 5) != hdBadge &&
               !hasColor(thumbnail(ordinary, 1), hdBadge) &&
               !hasColor(thumbnail(ordinary, 1), warningBadge),
           "ordinary project: the HD badge marks frames with HD, at its place");

    const ProjectDocument nativeExtra = makeDocument(PlaneLayout::NativeExtra);
    expect(!hasColor(thumbnail(nativeExtra, 0), hdBadge) &&
               !hasColor(thumbnail(nativeExtra, 0), warningBadge),
           "256x64 project: a complete frame has no badge");
    const QImage incomplete = thumbnail(nativeExtra, 1);
    expect(hasColor(incomplete, warningBadge) && !hasColor(incomplete, hdBadge) &&
               incomplete.pixelColor(155, 4) == warningBadge &&
               incomplete.pixelColor(157, 4) != warningBadge,
           "256x64 project: a frame without its picture has the warning badge");

    const ProjectDocument nativeOriginal = makeDocument(PlaneLayout::NativeOriginal);
    for (std::size_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
        expect(!hasColor(thumbnail(nativeOriginal, frameIndex), hdBadge) &&
                   !hasColor(thumbnail(nativeOriginal, frameIndex), warningBadge),
               "192x64 project: no badge");
    }
}

struct LazyList {
    QListWidget list;
    int builds = 0;
    QIcon built;
    app::LazyListIcons icons;

    explicit LazyList(int rows)
        : built(makeIcon(Qt::red))
        , icons(&list, makeIcon(Qt::black), [this](const QListWidgetItem&) {
            ++builds;
            return built;
        })
    {
        list.setIconSize(QSize(16, 16));
        list.setUniformItemSizes(false);
        list.resize(120, 200);
        for (int row = 0; row < rows; ++row) {
            auto* item = new QListWidgetItem(icons.placeholder(), QString::number(row));
            item->setSizeHint(QSize(100, 20));
            list.addItem(item);
        }
        icons.scheduleRowsInView();
    }

    static QIcon makeIcon(const QColor& color)
    {
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        return QIcon(pixmap);
    }

    [[nodiscard]] int builtRows() const
    {
        int count = 0;
        for (int row = 0; row < list.count(); ++row) {
            count += icons.isBuilt(list.item(row)) ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] bool builtExactly(int first, int last) const
    {
        for (int row = 0; row < list.count(); ++row) {
            if (icons.isBuilt(list.item(row)) != (row >= first && row <= last)) {
                return false;
            }
        }
        return true;
    }
};

// The frame strip: frames from left to right in one line.
void testLazyStripIcons()
{
    LazyList lazy(1000);
    lazy.list.setViewMode(QListView::IconMode);
    lazy.list.setFlow(QListView::LeftToRight);
    lazy.list.setWrapping(false);
    lazy.list.setMovement(QListView::Static);
    lazy.list.setGridSize(QSize(100, 40));
    lazy.list.resize(400, 80);
    lazy.list.show();
    QApplication::processEvents();
    const int inView = lazy.list.viewport()->width() / 100 + 1;
    expect(lazy.builds > 0 && lazy.builds <= inView + 3 && lazy.icons.isBuilt(lazy.list.item(0)) &&
               !lazy.icons.isBuilt(lazy.list.item(20)),
           "a strip builds the frames in view across, not every frame of the line");
    lazy.list.scrollToItem(lazy.list.item(600), QAbstractItemView::PositionAtTop);
    QApplication::processEvents();
    expect(lazy.icons.isBuilt(lazy.list.item(600)) && !lazy.icons.isBuilt(lazy.list.item(500)) &&
               lazy.builds <= 2 * (inView + 5),
           "scrolling a strip across builds the frames that come into view");
}

void testLazyListIcons()
{
    LazyList lazy(1000);
    QApplication::processEvents();
    expect(lazy.builds == 0, "nothing is built while the list is hidden");
    const int rowHeight = 20;
    for (int row = 0; row < lazy.list.count(); ++row) {
        expect(lazy.list.item(row)->sizeHint().height() == rowHeight &&
                   !lazy.list.item(row)->icon().isNull(),
               "every row has the placeholder and its own height");
    }

    lazy.list.show();
    QApplication::processEvents();
    // 200 pixels show 10 rows; two more are built beyond the end of the view.
    const int inView = lazy.list.viewport()->height() / rowHeight;
    expect(inView >= 8 && lazy.builtExactly(0, inView + 2) && lazy.builds == inView + 3,
           "showing the list builds the rows in view and a small margin");
    expect(lazy.list.visualRect(lazy.list.model()->index(5, 0)).height() == rowHeight,
           "a built row is as high as a placeholder row");

    QApplication::processEvents();
    lazy.icons.scheduleRowsInView();
    QApplication::processEvents();
    expect(lazy.builds == inView + 3, "a built icon is kept");

    lazy.list.scrollToItem(lazy.list.item(500), QAbstractItemView::PositionAtTop);
    lazy.list.scrollToItem(lazy.list.item(600), QAbstractItemView::PositionAtTop);
    const int before = lazy.builds;
    QApplication::processEvents();
    expect(lazy.icons.isBuilt(lazy.list.item(600)) && lazy.icons.isBuilt(lazy.list.item(598)) &&
               !lazy.icons.isBuilt(lazy.list.item(597)) &&
               !lazy.icons.isBuilt(lazy.list.item(500)) && lazy.builds - before <= inView + 5,
           "several scroll steps before the next pass build the final rows in view only");

    // Playback: the current row moves on one by one.
    const int beforePlayback = lazy.builds;
    for (int row = 601; row <= 640; ++row) {
        lazy.list.setCurrentRow(row);
        QApplication::processEvents();
    }
    expect(lazy.builds - beforePlayback <= 40 + 2 && lazy.icons.isBuilt(lazy.list.item(640)),
           "moving the current row builds each new row once");

    const int rowInView = lazy.list.currentRow();
    const int beforeInvalidate = lazy.builds;
    lazy.icons.invalidate(lazy.list.item(rowInView));
    expect(lazy.builds == beforeInvalidate + 1 && lazy.icons.isBuilt(lazy.list.item(rowInView)),
           "invalidating a row in view rebuilds it at once");
    lazy.icons.invalidate(lazy.list.item(3));
    expect(lazy.builds == beforeInvalidate + 1 && !lazy.icons.isBuilt(lazy.list.item(3)) &&
               lazy.list.item(3)->icon().cacheKey() == lazy.icons.placeholder().cacheKey(),
           "invalidating a row out of view puts the placeholder back");
    lazy.icons.invalidate(lazy.list.item(900));
    expect(lazy.builds == beforeInvalidate + 1, "invalidating a row that was never built is free");
    lazy.list.scrollToItem(lazy.list.item(3), QAbstractItemView::PositionAtTop);
    QApplication::processEvents();
    // Row 3 and the rows the first view did not reach.
    expect(lazy.icons.isBuilt(lazy.list.item(3)) && lazy.builds > beforeInvalidate + 1 &&
               lazy.builds <= beforeInvalidate + 2 + 3,
           "it is rebuilt when it comes into view again");

    const int beforeEnsure = lazy.builds;
    const int builtBefore = lazy.builtRows();
    lazy.icons.ensureBuilt(0, lazy.list.count() - 1);
    expect(lazy.builtRows() == lazy.list.count() &&
               lazy.builds - beforeEnsure == lazy.list.count() - builtBefore,
           "ensureBuilt builds exactly what is missing");

    lazy.list.clear();
    lazy.icons.scheduleRowsInView();
    lazy.icons.ensureBuilt(0, 10);
    QApplication::processEvents();
    expect(lazy.builtRows() == 0, "an empty list builds nothing");
}

}  // namespace

// A plain click on a frame of a multi-frame selection keeps the selection and only makes the
// frame current (Win32); a plain click on another frame selects it alone.
void testFrameListClicks()
{
    colorizing::app::FrameListWidget list;
    list.setSelectionMode(QAbstractItemView::ExtendedSelection);
    list.useStripLayout(QSize(176, 130));
    // Drag events go to the viewport: it has to take drops, which the icon mode's Static
    // movement turns off (issue #15: every dragged frame went back).
    expect(list.dragEnabled() && list.viewport()->acceptDrops() &&
               list.viewMode() == QListView::IconMode,
           "the frame strip takes drags and drops in its viewport");
    list.resize(900, 160);
    for (int row = 0; row < 6; ++row) {
        list.addItem(QStringLiteral("frame %1").arg(row));
    }
    list.show();
    QApplication::processEvents();
    const auto click = [&list](int row) {
        const QPoint at = list.visualItemRect(list.item(row)).center();
        QMouseEvent press(QEvent::MouseButtonPress, at, list.viewport()->mapToGlobal(at),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, at, list.viewport()->mapToGlobal(at),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(list.viewport(), &release);
    };
    for (int row = 1; row <= 3; ++row) {
        list.item(row)->setSelected(true);
    }
    list.setCurrentRow(1, QItemSelectionModel::NoUpdate);
    click(2);
    expect(list.currentRow() == 2 && list.selectedItems().size() == 3,
           "a click inside a multi-frame selection keeps it and moves the current frame");
    click(5);
    expect(list.currentRow() == 5 && list.selectedItems().size() == 1 &&
               list.item(5)->isSelected(),
           "a click outside it selects that frame alone");
    click(5);
    expect(list.selectedItems().size() == 1, "a click on the only selected frame keeps it");

    // Issue #18: a double click selects the frame alone, as in the Win32 editor.
    for (int row = 1; row <= 3; ++row) {
        list.item(row)->setSelected(true);
    }
    list.setCurrentRow(1, QItemSelectionModel::NoUpdate);
    const QPoint at = list.visualItemRect(list.item(2)).center();
    click(2);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, at, list.viewport()->mapToGlobal(at),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list.viewport(), &doubleClick);
    expect(list.currentRow() == 2 && list.selectedItems().size() == 1 &&
               list.item(2)->isSelected(),
           "a double click leaves the frame alone selected");
}

// Issue #13: the open sections of a side bar are resized by dragging the border between two of
// them; the shares are kept with the open sections.
void testSectionStackResize()
{
    // Settings of their own: without an organization name QSettings stores nothing on
    // Windows.
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("SectionStackTests"));
    const QString key = QStringLiteral("testSectionStackResize");
    {
        colorizing::app::SectionStack stack;
        for (const char* name : {"A", "B", "C"}) {
            stack.addTab(new QLabel(QString::fromLatin1(name)), QString::fromLatin1(name));
        }
        stack.restoreOpenSections(key, {QStringLiteral("A"), QStringLiteral("C")});
        expect(stack.sectionWeight(0) == stack.sectionWeight(2), "open sections start equal");
        expect(stack.resizeBelow(0, 30), "A has an open section below it, C (B is closed)");
        expect(stack.sectionWeight(0) == 130 && stack.sectionWeight(2) == 70 &&
                   stack.sectionWeight(1) == 100,
               "the border moves between A and C, the closed B keeps its share");
        expect(!stack.resizeBelow(2, 10), "the last open section has no border below it");
        stack.resizeBelow(0, 1000);
        expect(stack.sectionWeight(2) == 40, "a section keeps a smallest height");
        stack.resizeBelow(0, -1000);
        expect(stack.sectionWeight(0) == 40, "also above");
        stack.resizeBelow(0, 50);
    }
    {
        colorizing::app::SectionStack stack;
        for (const char* name : {"A", "B", "C"}) {
            stack.addTab(new QLabel(QString::fromLatin1(name)), QString::fromLatin1(name));
        }
        stack.restoreOpenSections(key, {});
        expect(stack.isSectionOpen(0) && stack.isSectionOpen(2) && stack.sectionWeight(0) == 90 &&
                   stack.sectionWeight(2) == 110,
               "the shares come back with the open sections");
        stack.resetSectionSizes();
        expect(stack.sectionWeight(0) == stack.sectionWeight(2), "and can be made equal again");
    }
    {
        // Hidden sections (those the canvas shown has no use for) keep their state.
        colorizing::app::SectionStack stack;
        for (const char* name : {"A", "B", "C"}) {
            stack.addTab(new QLabel(QString::fromLatin1(name)), QString::fromLatin1(name));
        }
        stack.setSectionOpen(0, true);
        stack.setSectionOpen(1, true);
        stack.setSectionOpen(2, true);
        stack.setSectionHidden(1, true);
        expect(stack.isSectionHidden(1) && stack.isSectionOpen(1),
               "a hidden section stays open underneath");
        const int before = stack.sectionWeight(1);
        expect(stack.resizeBelow(0, 20) && stack.sectionWeight(1) == before &&
                   stack.sectionWeight(2) == 80,
               "the border below A moves against C, past the hidden B");
        stack.setSectionHidden(0, true);
        expect(!stack.resizeBelow(0, 10), "a hidden section has no border");
        stack.setSectionHidden(0, false);
        stack.setSectionHidden(1, false);
        expect(!stack.isSectionHidden(1) && stack.isSectionOpen(1),
               "shown again, the section is open as before");
    }
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

void testCanvasHistory()
{
    using Kind = app::CanvasPlace::Kind;
    const auto frame = [](std::size_t index) { return app::CanvasPlace{Kind::Frame, index, index}; };
    const auto all = [](const app::CanvasPlace&) { return true; };
    app::CanvasHistory history;
    history.note(frame(3), false);
    history.note(frame(4), false);
    history.note(frame(5), false);
    expect(history.size() == 1 && !history.canGoBack(),
           "stepping through frames notes only where it rests");
    history.note(frame(9), true);
    history.note(app::CanvasPlace{Kind::Background, 2, 9}, false);
    history.note(app::CanvasPlace{Kind::Background, 2, 10}, false);
    expect(history.size() == 3 && history.current()->frame == 10,
           "a background is one place whatever the frame; its frame follows");
    history.note(frame(10), false);
    expect(history.size() == 4, "back on the frame canvas, the frame counts at once");
    history.note(app::CanvasPlace{Kind::Sprite, 1, 10}, false);

    std::optional<app::CanvasPlace> place = history.back(all);
    expect(place && place->kind == Kind::Frame && place->index == 10, "back: the frame");
    place = history.back(all);
    expect(place && place->kind == Kind::Background && place->index == 2 && place->frame == 10,
           "back: the background, with the frame to come back to");
    place = history.forward(all);
    expect(place && place->kind == Kind::Frame && history.canGoForward(), "forward again");
    history.note(frame(10), true);
    expect(history.canGoForward(), "showing a place notes nothing new");
    history.note(frame(20), true);
    expect(!history.canGoForward() && history.size() == 5,
           "a new place drops the places ahead");

    // Places that no longer exist are skipped and dropped.
    const auto withoutBackgrounds = [](const app::CanvasPlace& candidate) {
        return candidate.kind != Kind::Background;
    };
    history.back(all);
    place = history.back(withoutBackgrounds);
    expect(place && place->kind == Kind::Frame && place->index == 9 && history.size() == 4,
           "back skips a deleted background");
    place = history.forward(all);
    expect(place && place->kind == Kind::Frame && place->index == 10, "and forward again");

    app::CanvasHistory full;
    for (std::size_t index = 0; index < app::CanvasHistory::kMaxPlaces + 20; ++index) {
        full.note(frame(index), true);
    }
    expect(full.size() == app::CanvasHistory::kMaxPlaces, "the history keeps the last places");

    // The timeline is part of a frame's place: the frame in a scene's timeline is another one.
    app::CanvasHistory timeline;
    timeline.note(frame(6), true);
    app::CanvasPlace inScene = frame(120);
    inScene.filter = 8;
    inScene.filterScene = 50100;
    timeline.note(inScene, true);
    app::CanvasPlace sameFrameAllFrames = frame(120);
    timeline.note(sameFrameAllFrames, true);
    expect(timeline.size() == 3, "the same frame in another timeline is another place");
    timeline.back(all);
    place = timeline.back(all);
    expect(place && place->index == 6 && place->filter == 0 && place->filterScene == -1,
           "back from a scene's timeline: the frame and the timeline before");
    place = timeline.forward(all);
    expect(place && place->filter == 8 && place->filterScene == 50100,
           "forward: the scene's timeline again");
    // Leave Scene: past any number of a scene's frames to the timeline before.
    for (std::size_t index = 121; index < 140; ++index) {
        app::CanvasPlace sceneFrame = inScene;
        sceneFrame.index = sceneFrame.frame = index;
        timeline.note(sceneFrame, true);
    }
    const auto notScene = [](const app::CanvasPlace& candidate) {
        return candidate.filterScene < 0;
    };
    place = timeline.backTo(all, notScene);
    expect(place && place->index == 6 && place->filterScene < 0 && timeline.canGoForward(),
           "Leave Scene goes back past the scene's frames in one step");
    expect(!timeline.backTo(all, notScene), "and nothing before it matches any more");
    full.clear();
    expect(full.size() == 0 && !full.canGoBack(), "a project opened starts a new history");
}

void runPlaneLabelsWidgetTests()
{
    testCanvasHistory();
    testLabels();
    testFramePanelActions();
    testListLabelsOfPanels();
    testThumbnailBadges();
    testLazyListIcons();
    testLazyStripIcons();
    testFrameListClicks();
    testSectionStackResize();
}
