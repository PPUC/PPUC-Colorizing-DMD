// Rendering, hit testing and editing of a project whose original frames are 192x64: its
// 192x64 original plane is the only one shown and edited, the stored 96x32 extra plane is
// discarded.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QToolButton>

#include "CanvasEditController.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "FramePreviewWidget.h"
#include "FrameThumbnail.h"
#include "panels/FramePanel.h"
#include "panels/SpritePanel.h"

using colorizing::app::CanvasEditContext;
using colorizing::app::CanvasEditController;
using colorizing::app::CanvasEditLayer;
using colorizing::app::CanvasEditOutputs;
using colorizing::app::CanvasPixelOverlay;
using colorizing::app::FramePreviewWidget;
using colorizing::document::ProjectDocument;
using colorizing::edit::DrawingTool;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::PlaneLayout;
using colorizing::render::Point;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

constexpr Size kNativeSize{192, 64};
constexpr Size kExtraSize{96, 32};
constexpr uint16_t kBase = 0x2104;
constexpr uint16_t kGreen = 0x07e0;
constexpr uint16_t kCyan = 0x07ff;
constexpr uint16_t kMagenta = 0xf81f;
constexpr uint16_t kWhite = 0xffff;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: 192x64 original: " << message << '\n';
        std::exit(1);
    }
}

std::size_t at(int x, int y)
{
    return static_cast<std::size_t>(y) * 192 + static_cast<std::size_t>(x);
}

// Everything not listed is 0. Magenta is in the 96x32 data only: it must never be seen and
// never be written back.
//
// Frame 0:
//   original 1,2,3,1 from (150,40), (130,5)=1, (191,63)=2
//   dynamic set 2 at (130,5), whose color for original 1 is green
//   comparison mask 0: (191,63)
//   slot 0: sprite 0 (original 1,2,3,1, cyan) with the whole frame as bounds
//   background 0, its mask empty
//   flagged 96x32 frame, 96x32 dynamic mask all set 1, flagged 96x32 sprite and background
// Frame 1: nothing but the original 1,2,3,1 from (186,60).
ProjectDocument makeDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 4;
    snapshot.romName = "native_original_preview";
    snapshot.geometry = ProjectGeometry(PlaneLayout::NativeOriginal);
    snapshot.comparisonMasks.assign(2, document::makeIndexedFrame(kNativeSize, 0));
    snapshot.comparisonMasks[0].pixels[at(191, 63)] = 1;

    const Size cell{static_cast<int>(document::kMaxSpriteWidth),
                    static_cast<int>(document::kMaxSpriteHeight)};
    document::SpriteRecord sprite;
    sprite.hasHd = true;
    sprite.originalSd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedSd = document::makeRgb565Frame(cell, 0);
    sprite.maskHd = document::makeIndexedFrame(cell, 0);
    sprite.colorizedHd = document::makeRgb565Frame(cell, kMagenta);
    const uint8_t shape[4] = {1, 2, 3, 1};
    uint32_t dword = 0;
    for (int x = 0; x < 4; ++x) {
        sprite.originalSd.pixels[static_cast<std::size_t>(x)] = shape[x];
        sprite.colorizedSd.pixels[static_cast<std::size_t>(x)] = kCyan;
        dword |= static_cast<uint32_t>(shape[x]) << (8 * x);
    }
    sprite.detectionDwords = {dword, 0xffffffff, 0xffffffff, 0xffffffff};
    sprite.detectionDwordPositions = {0, 0xffff, 0xffff, 0xffff};
    sprite.detectionAreas = {
        document::SpriteDetectionArea{0, 0, 4, 1},
        document::SpriteDetectionArea{},
        document::SpriteDetectionArea{},
        document::SpriteDetectionArea{},
    };
    snapshot.sprites.push_back(sprite);

    document::ProjectFrameSnapshot frame;
    frame.originalSd = document::makeIndexedFrame(kNativeSize, 0);
    frame.colorizedSd = document::makeRgb565Frame(kNativeSize, kBase);
    frame.comparisonMaskId = 0;
    document::FrameDynamicData dynamicData;
    dynamicData.maskSd = document::makeIndexedFrame(kNativeSize, 255);
    dynamicData.maskHd = document::makeIndexedFrame(kExtraSize, 255);
    dynamicData.colorsSd.assign(document::kMaxDynamicSetsPerFrame * 4, 0);
    dynamicData.colorsHd.assign(document::kMaxDynamicSetsPerFrame * 4, kMagenta);
    frame.dynamicData = dynamicData;
    document::FrameBackgroundData backgroundData;
    backgroundData.maskSd = document::makeIndexedFrame(kNativeSize, 0);
    backgroundData.maskHd = document::makeIndexedFrame(kExtraSize, 1);
    frame.backgroundData = backgroundData;
    frame.spriteReferences.assign(document::kMaxSpritesPerFrame,
                                  document::FrameSpriteReference{255, 0, 0, 191, 63});
    snapshot.frames.assign(2, frame);

    document::ProjectFrameSnapshot& first = snapshot.frames[0];
    for (int x = 0; x < 4; ++x) {
        first.originalSd.pixels[at(150 + x, 40)] = shape[x];
        snapshot.frames[1].originalSd.pixels[at(186 + x, 60)] = shape[x];
    }
    first.originalSd.pixels[at(130, 5)] = 1;
    first.originalSd.pixels[at(191, 63)] = 2;
    first.colorizedHd = document::makeRgb565Frame(kExtraSize, kMagenta);
    first.dynamicData->maskSd.pixels[at(130, 5)] = 2;
    first.dynamicData->colorsSd[2 * 4 + 1] = kGreen;
    first.dynamicData->maskHd = document::makeIndexedFrame(kExtraSize, 1);
    first.spriteReferences[0].spriteId = 0;
    first.backgroundData->backgroundId = 0;
    document::BackgroundRecord background;
    background.colorizedSd = document::makeRgb565Frame(kNativeSize, kWhite);
    background.colorizedHd = document::makeRgb565Frame(kExtraSize, kMagenta);
    snapshot.backgrounds.push_back(background);
    return ProjectDocument::fromSnapshot(snapshot);
}

QImage grab(FramePreviewWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();
    return image;
}

// Where a canvas region is on the widget, found through the widget's own hit test.
struct RegionLocator {
    QPoint origin;
    int cell = 1;

    [[nodiscard]] QPoint operator()(int x, int y) const
    {
        return origin + QPoint(x * cell, y * cell);
    }
};

RegionLocator locate(const FramePreviewWidget& widget, CanvasRegion region)
{
    for (int py = 0; py < widget.height(); ++py) {
        for (int px = 0; px < widget.width(); ++px) {
            const std::optional<CanvasHit> hit = widget.hitTestAt(QPoint(px, py));
            if (!hit || hit->region != region) {
                continue;
            }
            expect(hit->x == 0 && hit->y == 0, "a region starts with its first pixel");
            RegionLocator locator{QPoint(px, py), 1};
            while (widget.hitTestAt(QPoint(px + locator.cell, py))->x == 0) {
                ++locator.cell;
            }
            return locator;
        }
    }
    expect(false, "region is not on the widget");
    return {};
}

bool containsColor(const QImage& image, uint16_t rgb565)
{
    const QColor color = colorizing::app::rgb565ToColor(rgb565);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) == color) {
                return true;
            }
        }
    }
    return false;
}

void testCanvasShowsAndHitsTheNativePlane()
{
    ProjectDocument document = makeDocument();
    FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setFrameIndex(0);

    for (const Resolution asked : {Resolution::Sd, Resolution::Hd}) {
        widget.setActiveResolution(asked);
        for (const CanvasRegion region :
             {CanvasRegion::ColorizedFrame, CanvasRegion::OriginalFrame}) {
            const RegionLocator position = locate(widget, region);
            const std::optional<CanvasHit> last = widget.hitTestAt(position(191, 63));
            expect(last && last->region == region && last->resolution == Resolution::Sd &&
                       last->x == 191 && last->y == 63,
                   "the bottom right pixel of a region is 191,63 on the original plane");
            const std::optional<CanvasHit> right = widget.hitTestAt(position(192, 63));
            const std::optional<CanvasHit> below = widget.hitTestAt(position(191, 64));
            expect(!right && (!below || below->region != region),
                   "a region ends at column 191 and row 63");
            const std::optional<CanvasHit> inner = widget.hitTestAt(position(129, 33));
            expect(inner && inner->x == 129 && inner->y == 33,
                   "pixels beyond 128x32 are hit at their own coordinates");
        }
    }
    const RegionLocator colorized = locate(widget, CanvasRegion::ColorizedFrame);
    const RegionLocator original = locate(widget, CanvasRegion::OriginalFrame);
    expect(colorized.cell == original.cell && colorized.origin.x() == original.origin.x(),
           "a plane pixel is as large as the original pixel below it");

    const QImage image = grab(widget);
    const auto shown = [&](int x, int y) { return image.pixelColor(colorized(x, y)); };
    const auto color = [](uint16_t rgb565) { return colorizing::app::rgb565ToColor(rgb565); };
    expect(shown(0, 0) == color(kBase) && shown(191, 63) == color(kBase) &&
               shown(129, 5) == color(kBase),
           "static pixels of the 192x64 frame");
    expect(shown(130, 5) == color(kGreen), "dynamic lookup beyond column 127");
    expect(shown(150, 40) == color(kCyan) && shown(153, 40) == color(kCyan) &&
               shown(149, 40) == color(kBase) && shown(154, 40) == color(kBase) &&
               shown(150, 41) == color(kBase),
           "the sprite is matched at 150,40 and drawn there one to one");
    expect(!containsColor(image, kMagenta), "nothing of the 96x32 plane is drawn");
    const auto shownOriginal = [&](int x, int y) { return image.pixelColor(original(x, y)); };
    expect(shownOriginal(150, 40) == colorizing::app::originalIndexToColor(1) &&
               shownOriginal(152, 40) == colorizing::app::originalIndexToColor(3) &&
               shownOriginal(191, 63) == colorizing::app::originalIndexToColor(2) &&
               shownOriginal(190, 63) == colorizing::app::originalIndexToColor(0),
           "original pixels are drawn one to one");

    const document::Rgb565Frame composed =
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false);
    expect(composed.size == kNativeSize && composed.pixels[at(130, 5)] == kGreen &&
               std::count(composed.pixels.begin(), composed.pixels.end(), kCyan) == 4,
           "the composed frame is 192x64");
    bool refused = false;
    try {
        (void)colorizing::app::composeResolvedFrame(document, 0, Resolution::Hd, false);
    } catch (const std::logic_error&) {
        refused = true;
    }
    expect(refused, "the 96x32 plane is never composed");
}

struct Editing {
    ProjectDocument document = makeDocument();
    FramePreviewWidget widget;
    std::vector<std::unique_ptr<edit::EditCommand>> commands;
    CanvasEditController controller;
    CanvasEditContext context;

    Editing()
        : controller(makeOutputs())
    {
        widget.resize(640, 420);
        widget.setDocument(&document);
        widget.setFrameIndex(0);
        context.document = &document;
        context.currentFrame = 0;
        context.selectedFrames = {0};
    }

    CanvasEditOutputs makeOutputs()
    {
        CanvasEditOutputs outputs;
        outputs.commit = [this](std::unique_ptr<edit::EditCommand> command) {
            command->apply(document);
            commands.push_back(std::move(command));
        };
        outputs.showPreview = [this](CanvasPixelOverlay overlay) {
            widget.setPreviewPixels(overlay.region,
                                    overlay.resolution,
                                    std::move(overlay.pixels),
                                    overlay.color);
        };
        return outputs;
    }

    [[nodiscard]] CanvasHit hit(CanvasRegion region, int x, int y) const
    {
        const std::optional<CanvasHit> found = widget.hitTestAt(locate(widget, region)(x, y));
        expect(found && found->region == region && found->x == x && found->y == y,
               "widget hit at " + std::to_string(x) + "," + std::to_string(y));
        return *found;
    }

    void undoAll()
    {
        while (!commands.empty()) {
            commands.back()->revert(document);
            commands.pop_back();
        }
    }
};

void testPaintAndMaskEditsAtTheEdges()
{
    Editing editing;
    ProjectDocument& document = editing.document;
    const QImage before = grab(editing.widget);

    editing.context.drawColor = kWhite;
    editing.controller.colorizedPixelClicked(
        editing.context, editing.hit(CanvasRegion::ColorizedFrame, 191, 63), false);
    editing.controller.colorizedPixelClicked(
        editing.context, editing.hit(CanvasRegion::ColorizedFrame, 129, 33), false);
    expect(editing.commands.size() == 2 &&
               document.colorizedPixel(0, Resolution::Sd, 191, 63) == kWhite &&
               document.colorizedPixel(0, Resolution::Sd, 129, 33) == kWhite &&
               document.colorizedPixel(0, Resolution::Sd, 190, 63) == kBase,
           "points painted at the right and bottom edge");
    editing.context.tool = DrawingTool::FilledRectangle;
    editing.controller.strokeFinished(editing.context,
                                      editing.hit(CanvasRegion::ColorizedFrame, 180, 0),
                                      editing.hit(CanvasRegion::ColorizedFrame, 191, 3),
                                      false);
    expect(document.colorizedPixel(0, Resolution::Sd, 191, 0) == kWhite &&
               document.colorizedPixel(0, Resolution::Sd, 180, 3) == kWhite &&
               document.colorizedPixel(0, Resolution::Sd, 179, 0) == kBase &&
               document.colorizedPixel(0, Resolution::Sd, 191, 4) == kBase,
           "a filled rectangle up to the right edge");

    editing.context.tool = DrawingTool::Point;
    editing.context.layer = CanvasEditLayer::ComparisonMask;
    editing.controller.originalPixelClicked(
        editing.context, editing.hit(CanvasRegion::OriginalFrame, 190, 62), false);
    expect(document.comparisonMaskPixel(0, 190, 62) == 1 &&
               document.comparisonMaskPixel(0, 95, 31) == 0 &&
               document.comparisonMask(0).size == kNativeSize,
           "the clicked original pixel is the edited 192x64 mask pixel");
    editing.controller.originalPixelClicked(
        editing.context, editing.hit(CanvasRegion::OriginalFrame, 191, 63), true);
    expect(document.comparisonMaskPixel(0, 191, 63) == 0, "erasing the last mask pixel");

    editing.context.layer = CanvasEditLayer::DynamicMask;
    editing.context.dynamicSet = 5;
    editing.controller.colorizedPixelClicked(
        editing.context, editing.hit(CanvasRegion::ColorizedFrame, 191, 0), false);
    expect(document.dynamicMaskPixel(0, Resolution::Sd, 191, 0) == 5 &&
               document.dynamicMaskPixel(0, Resolution::Sd, 130, 5) == 2,
           "dynamic mask edit at the right edge");

    editing.context.layer = CanvasEditLayer::BackgroundMask;
    editing.context.tool = DrawingTool::FilledRectangle;
    editing.controller.strokeFinished(editing.context,
                                      editing.hit(CanvasRegion::ColorizedFrame, 128, 32),
                                      editing.hit(CanvasRegion::ColorizedFrame, 191, 63),
                                      false);
    expect(document.backgroundMaskPixel(0, Resolution::Sd, 191, 63) == 1 &&
               document.backgroundMaskPixel(0, Resolution::Sd, 128, 32) == 1 &&
               document.backgroundMaskPixel(0, Resolution::Sd, 127, 32) == 0,
           "background mask shape beyond 128x32");

    const document::ProjectSnapshot edited = document.toSnapshot();
    expect(!edited.frames[0].colorizedHd && !edited.frames[0].inactiveHd &&
               !edited.backgrounds[0].colorizedHd &&
               edited.frames[0].dynamicData->maskHd.size == kExtraSize &&
               std::all_of(edited.frames[0].dynamicData->maskHd.pixels.begin(),
                           edited.frames[0].dynamicData->maskHd.pixels.end(),
                           [](uint8_t value) { return value == 255; }) &&
               std::all_of(edited.frames[0].dynamicData->colorsHd.begin(),
                           edited.frames[0].dynamicData->colorsHd.end(),
                           [](uint16_t color) { return color == 0; }) &&
               std::all_of(edited.frames[0].backgroundData->maskHd.pixels.begin(),
                           edited.frames[0].backgroundData->maskHd.pixels.end(),
                           [](uint8_t value) { return value == 1; }),
           "the 96x32 data is discarded: an edited project saves no extra data");

    editing.undoAll();
    editing.widget.update();
    expect(document.colorizedPixel(0, Resolution::Sd, 191, 63) == kBase &&
               document.comparisonMaskPixel(0, 191, 63) == 1 &&
               document.dynamicMaskPixel(0, Resolution::Sd, 191, 0) == 255 &&
               grab(editing.widget) == before,
           "after undo the document and the canvas are as before");
}

// The calls MainWindow::captureSelectedSpriteFromCurrentFrame makes.
void testSpriteCaptureAndMatchBeyondColumn127()
{
    Editing editing;
    ProjectDocument& document = editing.document;
    const uint16_t colors[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    for (int x = 0; x < 4; ++x) {
        document.setColorizedPixel(0, Resolution::Sd, 150 + x, 40, colors[x]);
    }
    document.clearFrameSpriteReference(0, 0);

    editing.context.spriteCaptureSelection = true;
    editing.context.tool = DrawingTool::Line;
    editing.controller.strokeFinished(editing.context,
                                      editing.hit(CanvasRegion::OriginalFrame, 150, 40),
                                      editing.hit(CanvasRegion::OriginalFrame, 153, 40),
                                      false);
    const std::vector<Point> selection = editing.controller.captureSelectionPixels(0);
    expect(selection.size() == 4 && selection.front().x == 150 && selection.back().x == 153 &&
               selection.front().y == 40,
           "the capture selection is on the 192x64 grid");

    document.replaceSpriteFromFrameSelection(
        0,
        document.originalFrame(0),
        colorizing::app::composeResolvedFrame(document, 0, Resolution::Sd, false),
        selection);
    const document::SpriteRecord& sprite = document.sprite(0);
    const uint8_t shape[4] = {1, 2, 3, 1};
    for (int x = 0; x < 4; ++x) {
        const std::size_t index = static_cast<std::size_t>(x);
        expect(sprite.originalSd.pixels[index] == shape[x] &&
                   sprite.colorizedSd.pixels[index] == colors[x],
               "the sprite holds the captured original and the 192x64 colors");
    }
    expect(!sprite.hasHd && sprite.originalSd.pixels[4] == 255, "no HD sprite is created");
    document.setSpriteDetectionArea(0, 0, document::SpriteDetectionArea{0, 0, 4, 1});

    // Frame 1 shows the same shape from (186,60). (At the last column the preview would miss
    // it: its bounds check is off by one in every project.)
    document.setFrameSpriteReference(1, 0, document::FrameSpriteReference{0, 129, 33, 191, 63});
    const document::Rgb565Frame matched =
        colorizing::app::composeResolvedFrame(document, 1, Resolution::Sd, false);
    for (int x = 0; x < 4; ++x) {
        expect(matched.pixels[at(186 + x, 60)] == colors[x],
               "the captured sprite is matched at 186,60 and drawn in its colors");
    }
    expect(matched.pixels[at(185, 60)] == kBase && matched.pixels[at(190, 60)] == kBase &&
               matched.pixels[at(186, 61)] == kBase,
           "at its own size");
    document.setFrameSpriteReference(1, 0, document::FrameSpriteReference{0, 0, 0, 127, 31});
    const document::Rgb565Frame outside =
        colorizing::app::composeResolvedFrame(document, 1, Resolution::Sd, false);
    expect(outside.pixels[at(186, 60)] == kBase, "128x32 bounds do not reach a sprite at 186,60");

    const document::ProjectSnapshot edited = document.toSnapshot();
    expect(!edited.sprites[0].hasHd &&
               std::all_of(edited.sprites[0].colorizedHd.pixels.begin(),
                           edited.sprites[0].colorizedHd.pixels.end(),
                           [](uint16_t pixel) { return pixel == 0; }) &&
               std::all_of(edited.sprites[0].maskHd.pixels.begin(),
                           edited.sprites[0].maskHd.pixels.end(),
                           [](uint8_t value) { return value == 255; }),
           "the stored 96x32 sprite data is discarded, also after a capture");
}

QToolButton* buttonWithText(QWidget& parent, const QString& text)
{
    for (QToolButton* button : parent.findChildren<QToolButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    expect(false, "button " + text.toStdString());
    return nullptr;
}

void testPanelsAndThumbnail()
{
    const ProjectDocument document = makeDocument();

    colorizing::app::FramePanel framePanel;
    colorizing::app::FramePanelState frameState;
    frameState.document = &document;
    frameState.frameIndex = 0;
    frameState.hasSelectedFrames = true;
    QToolButton* create = buttonWithText(framePanel, QStringLiteral("Create HD"));
    QToolButton* remove = buttonWithText(framePanel, QStringLiteral("Remove HD"));
    framePanel.refresh(frameState);
    expect(create->isHidden() && remove->isHidden(),
           "there is no extra plane: Create HD and Remove HD are hidden");
    framePanel.showSpriteHdActions(false, PlaneLayout::NativeOriginal);
    expect(create->isHidden() && remove->isHidden(), "the sprite actions are hidden as well");
    framePanel.showSpriteHdActions(false);
    expect(!create->isHidden() && !remove->isHidden() && create->isEnabled(),
           "an ordinary project can create an HD sprite");

    colorizing::app::SpritePanel spritePanel;
    colorizing::app::SpritePanelState spriteState;
    spriteState.document = &document;
    spriteState.frameIndex = 0;
    spriteState.spriteIndex = 0;
    spritePanel.refresh(spriteState);
    int ranged = 0;
    for (QSpinBox* spinBox : spritePanel.findChildren<QSpinBox*>()) {
        if (!spinBox->toolTip().startsWith(QStringLiteral("Sprite slot bounding box"))) {
            continue;
        }
        const bool horizontal = spinBox->toolTip().endsWith(QLatin1Char('X'));
        expect(spinBox->minimum() == 0 && spinBox->maximum() == (horizontal ? 191 : 63),
               "slot bounds range over 0..191 and 0..63");
        ++ranged;
    }
    expect(ranged == 4, "four slot bound fields");

    // A 3:1 frame keeps its shape: 114 of the 152 pixels a 4:1 frame fills, centred.
    const QImage thumbnail = colorizing::app::makeFrameThumbnail(document, 0, QSize(160, 84))
                                 .pixmap(QSize(160, 84))
                                 .toImage();
    const QColor frameColor = colorizing::app::rgb565ToColor(kBase);
    const QColor backdrop(18, 20, 22);
    expect(thumbnail.pixelColor(10, 20) == backdrop && thumbnail.pixelColor(150, 20) == backdrop &&
               thumbnail.pixelColor(26, 20) == frameColor &&
               thumbnail.pixelColor(134, 20) == frameColor,
           "the thumbnail shows the 192x64 frame in its proportions");
    for (int y = 0; y < 20; ++y) {
        for (int x = 100; x < thumbnail.width(); ++x) {
            expect(thumbnail.pixelColor(x, y) != QColor(35, 95, 160),
                   "a frame whose file had a 96x32 frame has no HD badge");
        }
    }
}

}  // namespace

void runNativeOriginalWidgetTests()
{
    testCanvasShowsAndHitsTheNativePlane();
    testPaintAndMaskEditsAtTheEdges();
    testSpriteCaptureAndMatchBeyondColumn127();
    testPanelsAndThumbnail();
}
