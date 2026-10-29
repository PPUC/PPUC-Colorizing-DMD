// Where the UI says "HD" and "SD": only in a project that has HD as an extra resolution. The
// frame list builds its thumbnails for the rows in view (LazyListIcons).

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <QAbstractButton>
#include <QApplication>
#include <QWheelEvent>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMouseEvent>
#include <QListWidget>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QStyle>
#include <QSettings>

#include <memory>
#include <QTemporaryDir>
#include <QToolButton>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "Appearance.h"
#include "FrameListWidget.h"
#include "panels/SectionBar.h"
#include "WindowGeometry.h"
#include "FrameThumbnail.h"
#include "CanvasHistory.h"
#include "LazyListIcons.h"
#include "panels/BackgroundPanel.h"
#include "panels/ColorRotationPanel.h"
#include "panels/FramePanel.h"
#include "panels/SpritePanel.h"
#include "PlaneLabels.h"
#include "RecentFolders.h"
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
            document::kLegacySpritesPerFrame,
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
    ProjectDocument named = makeDocument(PlaneLayout::Ordinary);
    expect(app::spriteListCaption(named, 0) == QStringLiteral("0 (HD)") &&
               app::spriteListCaption(named, 1) == QStringLiteral("1"),
           "a sprite without a name is listed by its number");
    named.setSpriteName(0, "Credits0");
    expect(app::spriteListCaption(named, 0) == QStringLiteral("0 - Credits0 (HD)"),
           "a named sprite by its number and name (issue #42)");
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

    // Issue #20: Left and Right step through the frames from the canvas too, where the focus
    // is after a click on a pixel.
    const auto key = [](int code, Qt::KeyboardModifiers modifiers) {
        return QKeyEvent(QEvent::KeyPress, code, modifiers);
    };
    using colorizing::app::FrameListWidget;
    expect(FrameListWidget::isFrameStepKey(key(Qt::Key_Left, Qt::NoModifier)) &&
               FrameListWidget::isFrameStepKey(key(Qt::Key_Right, Qt::ShiftModifier)) &&
               !FrameListWidget::isFrameStepKey(key(Qt::Key_Right, Qt::AltModifier)) &&
               !FrameListWidget::isFrameStepKey(key(Qt::Key_Left, Qt::ControlModifier)) &&
               !FrameListWidget::isFrameStepKey(key(Qt::Key_Up, Qt::NoModifier)),
           "Left and Right step, also with Shift; Alt and Ctrl keep their shortcuts");
    QLineEdit elsewhere;
    elsewhere.show();
    elsewhere.setFocus();
    list.setCurrentRow(2);
    list.stepWithKey(key(Qt::Key_Right, Qt::NoModifier));
    expect(list.currentRow() == 3 && list.selectedItems().size() == 1 &&
               list.item(3)->isSelected(),
           "Right shows the next frame alone, while another widget has the focus");
    list.stepWithKey(key(Qt::Key_Left, Qt::NoModifier));
    list.stepWithKey(key(Qt::Key_Left, Qt::NoModifier));
    expect(list.currentRow() == 1, "Left goes back");

    // Issue #31: the window keeps the canvas selection when the frame changes by a step key,
    // and drops it otherwise; it asks the list while the current frame changes.
    std::vector<bool> stepping;
    const QMetaObject::Connection watch =
        QObject::connect(&list, &QListWidget::currentRowChanged, [&list, &stepping](int) {
            stepping.push_back(list.isSteppingWithKey());
        });
    list.stepWithKey(key(Qt::Key_Right, Qt::NoModifier));
    list.setCurrentRow(5);
    QObject::disconnect(watch);
    expect(stepping == std::vector<bool>{true, false} && !list.isSteppingWithKey(),
           "a step key is told apart from another change of the current frame");

    // Esc reduces a selection of several frames to the frame shown.
    for (int row = 1; row <= 3; ++row) {
        list.item(row)->setSelected(true);
    }
    list.setCurrentRow(2, QItemSelectionModel::NoUpdate);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&list, &escape);
    expect(list.selectedItems().size() == 1 && list.item(2)->isSelected() &&
               list.currentRow() == 2,
           "Esc leaves the current frame alone selected");
    expect(!list.selectCurrentAlone(), "with one frame selected there is nothing to reduce");
}

// Issue #34: a turn of the mouse wheel moves the strip by one whole frame, so that the first
// frame shown is always shown in full, where frame 0 begins.
void testStripWheelStepsByFrame()
{
    using colorizing::app::FrameListWidget;
    FrameListWidget list;
    list.useStripLayout(QSize(176, 130));
    list.resize(900, 160);
    for (int row = 0; row < 60; ++row) {
        list.addItem(QStringLiteral("frame %1").arg(row));
    }
    list.show();
    QApplication::processEvents();
    QScrollBar* bar = list.horizontalScrollBar();
    // Where a frame begins in the strip's content.
    const auto contentLeft = [&list, bar](int row) {
        return list.visualRect(list.model()->index(row, 0)).left() + bar->value();
    };
    const int step = contentLeft(1) - contentLeft(0);
    const auto wheel = [&list](int deltaY) {
        const QPointF at(100, 60);
        QWheelEvent event(at, list.viewport()->mapToGlobal(at.toPoint()), QPoint(),
                          QPoint(0, deltaY), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(list.viewport(), &event);
    };
    wheel(-120);
    expect(bar->value() == step, "a turn down moves one frame on");
    wheel(-360);
    expect(bar->value() == 4 * step, "three turns, three frames");
    wheel(120);
    expect(bar->value() == 3 * step, "a turn up moves one back");
    // From a place between two frames (the scroll bar held), the next turn lands on a frame.
    bar->setSliderDown(true);
    bar->setValue(3 * step + step / 3);
    wheel(-120);
    expect(bar->value() == 4 * step, "from between frames to the next whole one");
    bar->setSliderDown(false);
    // A trackpad's small steps add up to a frame.
    wheel(-40);
    wheel(-40);
    expect(bar->value() == 4 * step, "small steps wait");
    wheel(-40);
    expect(bar->value() == 5 * step, "and move one frame once they make a turn");

    // Any other scrolling ends on a whole frame too: going right on the next frame start (a
    // frame scrolled into view at the right edge stays in full view), going left on the one
    // before, a dragged scroll bar where it is let go.
    bar->setValue(5 * step + step / 3);
    expect(bar->value() == 6 * step, "scrolled right, on the next frame");
    bar->setValue(5 * step + step / 3);
    expect(bar->value() == 5 * step, "scrolled left, on the frame before");
    bar->setSliderDown(true);
    bar->setValue(7 * step + step / 4);
    expect(bar->value() == 7 * step + step / 4, "a dragged scroll bar moves freely");
    bar->setSliderDown(false);
    expect(bar->value() == 7 * step, "let go, it settles on the nearest frame");
}

// Issue #24: the same frames are outlined in the strip and ticked on its scroll bar, and
// Ctrl+Left and Ctrl+Right look through them while the current frame stays.
void testMarkedFramesInTheStrip()
{
    using colorizing::app::FrameListWidget;
    FrameListWidget list;
    list.setSelectionMode(QAbstractItemView::ExtendedSelection);
    list.useStripLayout(QSize(176, 130));
    list.resize(900, 160);
    for (int row = 0; row < 60; ++row) {
        list.addItem(QStringLiteral("frame %1").arg(row));
    }
    list.show();
    list.setCurrentRow(0);
    QApplication::processEvents();

    const auto key = [](int code, Qt::KeyboardModifiers modifiers) {
        return QKeyEvent(QEvent::KeyPress, code, modifiers);
    };
    expect(FrameListWidget::isMarkedRowStepKey(key(Qt::Key_Left, Qt::ControlModifier)) &&
               FrameListWidget::isMarkedRowStepKey(key(Qt::Key_Right, Qt::ControlModifier)) &&
               !FrameListWidget::isMarkedRowStepKey(
                   key(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier)) &&
               !FrameListWidget::isMarkedRowStepKey(key(Qt::Key_Right, Qt::NoModifier)),
           "Ctrl+Left and Ctrl+Right, without other modifiers, look through the marked frames");

    const QColor mark = colorizing::app::markedFrameColor();
    const auto hasMark = [&mark](const QImage& image) {
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == mark) {
                    return true;
                }
            }
        }
        return false;
    };
    expect(!hasMark(list.horizontalScrollBar()->grab().toImage()),
           "without marked frames the scroll bar has no ticks");

    list.setMarkedRows({3, 20, 41});
    QApplication::processEvents();
    expect(hasMark(list.horizontalScrollBar()->grab().toImage()),
           "the scroll bar has a tick for each marked frame");

    QKeyEvent next = key(Qt::Key_Right, Qt::ControlModifier);
    QApplication::sendEvent(&list, &next);
    expect(list.shownMarkedRow() == 3 && list.currentRow() == 0, "Ctrl+Right goes to frame 3");
    QApplication::sendEvent(&list, &next);
    QApplication::processEvents();
    const QRect shown = list.visualItemRect(list.item(20));
    expect(list.shownMarkedRow() == 20 && list.currentRow() == 0 &&
               list.selectedItems().size() == 1 && list.item(0)->isSelected() &&
               list.viewport()->rect().contains(shown),
           "the next one is scrolled into view; the current frame and the selection stay");
    const QImage viewport = list.viewport()->grab().toImage();
    expect(viewport.pixelColor(shown.left() + 1, shown.center().y()) == mark,
           "the frame shown is outlined");
    QApplication::sendEvent(&list, &next);
    QApplication::sendEvent(&list, &next);
    expect(list.shownMarkedRow() == 41 && list.showMarkedRow(1) == -1,
           "behind the last marked frame nothing moves");
    QKeyEvent previous = key(Qt::Key_Left, Qt::ControlModifier);
    QApplication::sendEvent(&list, &previous);
    expect(list.shownMarkedRow() == 20 && list.currentRow() == 0, "Ctrl+Left goes back");

    list.setCurrentRow(5);
    expect(list.shownMarkedRow() == -1, "another current frame starts from itself again");
    expect(list.showMarkedRow(-1) == 3, "from frame 5 back to frame 3");
    list.setMarkedRows({});
    QApplication::processEvents();
    expect(list.shownMarkedRow() == -1 && !hasMark(list.horizontalScrollBar()->grab().toImage()),
           "the marks go with the rows");
}

// Issue #2: a window opens where and as large as it was left.
void testWindowGeometryIsKept()
{
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("WindowGeometryTests"));
    {
        QWidget window;
        window.resize(400, 300);
        colorizing::app::keepWindowGeometry(&window, QStringLiteral("test"));
        expect(window.size() == QSize(400, 300), "nothing stored: the window keeps its size");
        window.show();
        window.resize(640, 480);
        QApplication::processEvents();
        window.close();
    }
    {
        QWidget window;
        window.resize(400, 300);
        colorizing::app::keepWindowGeometry(&window, QStringLiteral("test"));
        expect(window.size() == QSize(640, 480), "the next one opens as large as it was left");
        QWidget other;
        other.resize(200, 100);
        colorizing::app::keepWindowGeometry(&other, QStringLiteral("other"));
        expect(other.size() == QSize(200, 100), "each window has its own");
    }
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

// Each kind of file dialog starts in the folder its kind was last opened from, also after a
// restart; a folder that is gone falls back.
void testRecentFoldersPerKind()
{
    namespace folders = colorizing::app::RecentFolders;
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("RecentFoldersTests"));
    const QString fallback = QStringLiteral("/fallback");
    expect(folders::folder(folders::kImages, fallback) == fallback,
           "nothing stored: the fallback");
    QDir(directory.path()).mkpath(QStringLiteral("images"));
    QDir(directory.path()).mkpath(QStringLiteral("dumps"));
    const QString images = QDir(directory.path()).absoluteFilePath(QStringLiteral("images"));
    const QString dumps = QDir(directory.path()).absoluteFilePath(QStringLiteral("dumps"));
    folders::remember(folders::kImages, images + QStringLiteral("/title.png"));
    folders::remember(folders::kFrameDumps, dumps + QStringLiteral("/txt.gz"));
    folders::remember(folders::kFrameDumps, QString());
    expect(folders::folder(folders::kImages, fallback) == images, "the images' folder");
    expect(folders::folder(folders::kFrameDumps, fallback) == dumps,
           "the dumps' own folder, a cancelled dialog changes nothing");
    expect(folders::folder(folders::kMovieClips, fallback) == fallback,
           "a kind not opened yet has the fallback");
    QDir(images).removeRecursively();
    expect(folders::folder(folders::kImages, fallback) == fallback,
           "a folder that is gone: the fallback");
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

// Issue #23: a combo box with more entries than it shows drops down a list that scrolls; the
// popup of Fusion and the Windows styles showed all of them and ran past the screen.
void testLongComboBoxesScroll()
{
    std::unique_ptr<QStyle> style(
        colorizing::app::createEditorStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QComboBox shortBox;
    QComboBox longBox;
    for (int i = 0; i < 5; ++i) {
        shortBox.addItem(QString::number(i));
    }
    for (int i = 0; i < 60; ++i) {
        longBox.addItem(QString::number(i));
    }
    // As QComboBox asks: with the option of a box that cannot be edited.
    QStyleOptionComboBox option;
    option.initFrom(&longBox);
    option.editable = false;
    std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
    expect(fusion->styleHint(QStyle::SH_ComboBox_Popup, &option, &longBox) != 0,
           "Fusion itself pops up every entry");
    expect(style->styleHint(QStyle::SH_ComboBox_Popup, &option, &longBox) == 0,
           "a long list drops down as a scrolling list");
    expect(style->styleHint(QStyle::SH_ComboBox_Popup, &option, &shortBox) ==
               fusion->styleHint(QStyle::SH_ComboBox_Popup, &option, &shortBox),
           "a short one looks as the style has it");

    // The sections box drops down as wide as its longest name; the box stays narrow. The
    // trigger box is gone (the frame filter's With triggers).
    colorizing::app::SectionBar bar;
    std::vector<colorizing::app::SectionBarEntry> sections;
    for (int i = 0; i < 40; ++i) {
        sections.push_back({QStringLiteral("Wildfire Jackpot plus extra ball %1").arg(i),
                            static_cast<std::size_t>(i * 10)});
    }
    bar.setContent(true, sections);
    const QList<QComboBox*> boxes = bar.findChildren<QComboBox*>();
    const QString longest = QStringLiteral("Wildfire Jackpot plus extra ball 39 (frame 390)");
    expect(boxes.size() == 1 &&
               boxes[0]->view()->minimumWidth() >=
                   QFontMetrics(boxes[0]->view()->font()).horizontalAdvance(longest),
           "one box, its list as wide as the longest section name");
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

// The author arranges a side bar: sections moved (in the context menu or by dragging a header)
// and optional ones hidden; both come back at the next start.
void testSectionStackArrangement()
{
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("SectionStackTests"));
    const QString key = QStringLiteral("testSectionStackArrangement");
    const auto make = [](colorizing::app::SectionStack& stack, const QStringList& names) {
        for (const QString& name : names) {
            stack.addTab(new QLabel(name), name);
        }
        stack.setSectionOptional(3, true);
    };
    {
        colorizing::app::SectionStack stack;
        make(stack, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                     QStringLiteral("D")});
        stack.restoreOpenSections(key, {QStringLiteral("A"), QStringLiteral("C")});
        stack.moveSection(2, 0);
        expect(stack.sectionOrder() == QStringList({QStringLiteral("C"), QStringLiteral("A"),
                                                    QStringLiteral("B"), QStringLiteral("D")}) &&
                   stack.tabText(2) == QStringLiteral("C") && stack.indexOf(stack.widget(2)) == 2,
               "C moves to the top; its index stays");
        expect(stack.resizeBelow(2, 20) && stack.sectionWeight(2) == 120 &&
                   stack.sectionWeight(0) == 80 && !stack.resizeBelow(0, 10),
               "the border below C now moves against A, and A, last open, has none");
        stack.setSectionHiddenByAuthor(1, true);
        expect(!stack.isSectionHiddenByAuthor(1), "a section that is not optional stays");
        expect(stack.contextMenuEntries(3).contains(QStringLiteral("Hide D")) &&
                   !stack.contextMenuEntries(1).contains(QStringLiteral("Hide B")),
               "the menu offers to hide an optional section");
        stack.setSectionHiddenByAuthor(3, true);
        expect(stack.isSectionHiddenByAuthor(3) && !stack.isSectionHidden(3) &&
                   stack.contextMenuEntries(0).contains(QStringLiteral("Show D")),
               "D hidden by the author, apart from what the editor hides, and offered again");
    }
    {
        // The next start, with a section added since.
        colorizing::app::SectionStack stack;
        make(stack, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                     QStringLiteral("D"), QStringLiteral("E")});
        stack.restoreOpenSections(key, {});
        expect(stack.sectionOrder() == QStringList({QStringLiteral("C"), QStringLiteral("A"),
                                                    QStringLiteral("B"), QStringLiteral("D"),
                                                    QStringLiteral("E")}) &&
                   stack.isSectionHiddenByAuthor(3),
               "the order and the hidden section come back, the new one at its own place");
        stack.resetArrangement();
        expect(stack.sectionOrder() == QStringList({QStringLiteral("A"), QStringLiteral("B"),
                                                    QStringLiteral("C"), QStringLiteral("D"),
                                                    QStringLiteral("E")}) &&
                   !stack.isSectionHiddenByAuthor(3),
               "restored: the first order, everything shown");

        // Dragging B's header above A's: B moves, and is not opened by the drag.
        stack.resize(240, 400);
        stack.show();
        QApplication::processEvents();
        const QList<QToolButton*> headers = stack.findChildren<QToolButton*>();
        QToolButton* a = headers[0];
        QToolButton* b = headers[1];
        const bool bOpen = stack.isSectionOpen(1);
        const auto send = [](QWidget* target, QEvent::Type type, const QPoint& global,
                             Qt::MouseButtons buttons) {
            const QPoint local = target->mapFromGlobal(global);
            QMouseEvent event(type, local, global, Qt::LeftButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(target, &event);
        };
        const QPoint start = b->mapToGlobal(b->rect().center());
        const QPoint target = a->mapToGlobal(QPoint(a->width() / 2, 1));
        send(b, QEvent::MouseButtonPress, start, Qt::LeftButton);
        send(b, QEvent::MouseMove, start - QPoint(0, 10), Qt::LeftButton);
        send(b, QEvent::MouseMove, target, Qt::LeftButton);
        send(b, QEvent::MouseButtonRelease, target, Qt::NoButton);
        expect(stack.sectionOrder().front() == QStringLiteral("B") &&
                   stack.isSectionOpen(1) == bOpen,
               "a dragged header moves its section and leaves it open or closed");
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
    testMarkedFramesInTheStrip();
    testStripWheelStepsByFrame();
    testSectionStackResize();
    testLongComboBoxesScroll();
    testWindowGeometryIsKept();
    testRecentFoldersPerKind();
    testSectionStackArrangement();
}
