#include "CanvasEditController.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"

using colorizing::app::CanvasEditContext;
using colorizing::app::CanvasEditController;
using colorizing::app::CanvasEditLayer;
using colorizing::app::CanvasEditOutputs;
using colorizing::app::CanvasPixelOverlay;
using colorizing::app::PaintTarget;
using colorizing::document::ProjectDocument;
using colorizing::edit::DrawingTool;
using colorizing::edit::EditKind;
using colorizing::edit::EditTarget;
using colorizing::edit::PaletteGradientDirection;
using colorizing::edit::PaletteGradientPattern;
using colorizing::edit::PaletteGradientShape;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::Point;
using colorizing::render::Resolution;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: canvas edit controller: " << message << '\n';
        std::exit(1);
    }
}

constexpr uint16_t kDrawColor = 0xf81f;

// Frame 0: HD, mask 0, dynamic data, background 0. Frame 1: SD only, mask 1, a background ID
// that does not exist. Frame 2: HD, mask 0, dynamic data. Frame 3: SD only, no mask.
ProjectDocument makeDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "canvas_edit";
    snapshot.comparisonMasks.assign(2, document::makeIndexedFrame(kSdFrameSize, 0));
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(kSdFrameSize, 0x07e0),
        document::makeRgb565Frame(kHdFrameSize, 0x001f),
    });
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(kSdFrameSize, 0x0800),
    });
    for (uint16_t paletteIndex = 0; paletteIndex < 2; ++paletteIndex) {
        document::PaletteRecord palette;
        for (std::size_t color = 0; color < document::kPaletteColorCount; ++color) {
            palette.colors.push_back(static_cast<uint16_t>((paletteIndex + 1) * 0x100 + color));
        }
        snapshot.palettes.push_back(palette);
    }
    for (int frameIndex = 0; frameIndex < 4; ++frameIndex) {
        snapshot.frames.push_back(document::ProjectFrameSnapshot{
            document::makeIndexedFrame(kSdFrameSize, 0),
            document::makeRgb565Frame(kSdFrameSize, 0),
            std::nullopt,
            0,
            255,
        });
    }
    const document::FrameDynamicData dynamicData{
        document::makeIndexedFrame(kSdFrameSize, 255),
        document::makeIndexedFrame(kHdFrameSize, 255),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0),
    };
    const auto backgroundData = [](uint16_t backgroundId) {
        return document::FrameBackgroundData{
            backgroundId,
            document::makeIndexedFrame(kSdFrameSize, 0),
            document::makeIndexedFrame(kHdFrameSize, 0),
        };
    };
    snapshot.frames[0].colorizedHd = document::makeRgb565Frame(kHdFrameSize, 0);
    snapshot.frames[0].comparisonMaskId = 0;
    snapshot.frames[0].dynamicData = dynamicData;
    snapshot.frames[0].backgroundData = backgroundData(0);
    snapshot.frames[1].comparisonMaskId = 1;
    snapshot.frames[1].backgroundData = backgroundData(0xffff);
    snapshot.frames[2].colorizedHd = document::makeRgb565Frame(kHdFrameSize, 0);
    snapshot.frames[2].comparisonMaskId = 0;
    snapshot.frames[2].dynamicData = dynamicData;
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

// Stands in for the main window: applies the commands and records every output.
struct Owner {
    explicit Owner(ProjectDocument& editedDocument)
        : document(editedDocument)
    {
    }

    [[nodiscard]] CanvasEditOutputs outputs()
    {
        CanvasEditOutputs out;
        out.commit = [this](std::unique_ptr<edit::EditCommand> command) {
            expect(finished == 0, "commits come before commitsFinished");
            command->apply(document);
            commits.push_back(command->target());
        };
        out.commitsFinished = [this]() { ++finished; };
        out.showStatus = [this](const QString& text, int timeoutMs) {
            status = text.toStdString();
            statusMs = timeoutMs;
        };
        out.pickColor = [this](uint16_t rgb565) { pickedColors.push_back(rgb565); };
        out.pickDynamicSet = [this](uint8_t dynamicSet) { pickedSets.push_back(dynamicSet); };
        out.showPreview = [this](CanvasPixelOverlay overlay) { preview = std::move(overlay); };
        out.clearPreview = [this]() {
            preview.reset();
            ++previewClears;
        };
        out.showSelectionOverlay = [this](CanvasPixelOverlay overlay) {
            selectionOverlay = std::move(overlay);
        };
        out.clearSelectionOverlay = [this]() {
            selectionOverlay.reset();
            ++selectionOverlayClears;
        };
        out.selectionChanged = [this]() { ++selectionChanges; };
        out.selectionDirectionModeChanged = [this](bool active) {
            directionModes.push_back(active);
        };
        out.captureSelectionChanged = [this]() { ++captureChanges; };
        return out;
    }

    // Forgets what one user action produced; the overlays stay, as on the canvas.
    void next()
    {
        commits.clear();
        finished = 0;
        status.clear();
        statusMs = 0;
        pickedColors.clear();
        pickedSets.clear();
        selectionChanges = 0;
        directionModes.clear();
        captureChanges = 0;
        previewClears = 0;
        selectionOverlayClears = 0;
    }

    [[nodiscard]] bool committed(EditKind kind,
                                 const std::vector<std::size_t>& indices,
                                 Resolution resolution = Resolution::Sd) const
    {
        if (commits.size() != indices.size() || finished != (indices.empty() ? 0 : 1)) {
            return false;
        }
        for (std::size_t position = 0; position < indices.size(); ++position) {
            const EditTarget& target = commits[position];
            if (target.kind != kind || target.index != indices[position] ||
                target.resolution != resolution) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool silent() const
    {
        return commits.empty() && finished == 0 && status.empty();
    }

    ProjectDocument& document;
    std::vector<EditTarget> commits;
    int finished = 0;
    std::string status;
    int statusMs = 0;
    std::vector<uint16_t> pickedColors;
    std::vector<uint8_t> pickedSets;
    std::optional<CanvasPixelOverlay> preview;
    int previewClears = 0;
    std::optional<CanvasPixelOverlay> selectionOverlay;
    int selectionOverlayClears = 0;
    int selectionChanges = 0;
    std::vector<bool> directionModes;
    int captureChanges = 0;
};

CanvasEditContext makeContext(const ProjectDocument& document,
                              CanvasEditLayer layer,
                              DrawingTool tool,
                              std::vector<std::size_t> selectedFrames)
{
    CanvasEditContext context;
    context.document = &document;
    if (!selectedFrames.empty()) {
        context.currentFrame = selectedFrames.front();
    }
    context.selectedFrames = std::move(selectedFrames);
    context.layer = layer;
    context.tool = tool;
    context.drawColor = kDrawColor;
    context.dynamicSet = 3;
    context.selectedPalette = 1;
    context.gradientEndpoints = []() { return std::pair<int, int>{2, 5}; };
    if (layer == CanvasEditLayer::BackgroundImage) {
        context.paintTarget = PaintTarget::Background;
        context.selectedBackground = 1;
    }
    return context;
}

CanvasHit colorized(int x, int y, Resolution resolution = Resolution::Sd)
{
    return CanvasHit{CanvasRegion::ColorizedFrame, resolution, x, y};
}

CanvasHit original(int x, int y)
{
    return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Sd, x, y};
}

bool samePoints(const std::vector<Point>& left, const std::vector<Point>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].x != right[index].x || left[index].y != right[index].y) {
            return false;
        }
    }
    return true;
}

bool isOverlay(const std::optional<CanvasPixelOverlay>& overlay,
               CanvasRegion region,
               Resolution resolution,
               const std::vector<Point>& pixels,
               const QColor& color)
{
    return overlay && overlay->region == region && overlay->resolution == resolution &&
           samePoints(overlay->pixels, pixels) && overlay->color == color;
}

QColor drawColor(int alpha)
{
    QColor color = colorizing::app::rgb565ToColor(kDrawColor);
    color.setAlpha(alpha);
    return color;
}

void testFramePixelPointPickAndFlood()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());

    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::Point, {0, 1, 2});
    controller.colorizedPixelClicked(context, colorized(3, 4), false);
    expect(owner.committed(EditKind::FramePixels, {0, 1, 2}), "point paints every selected frame");
    expect(owner.status == "Painted pixel at 3,4 on 3 frame(s)" && owner.statusMs == 2500,
           "multi-frame point status");
    for (std::size_t frameIndex = 0; frameIndex < 3; ++frameIndex) {
        expect(document.colorizedPixel(frameIndex, Resolution::Sd, 3, 4) == kDrawColor,
               "point pixel painted");
    }
    expect(document.colorizedPixel(3, Resolution::Sd, 3, 4) == 0, "unselected frame untouched");

    owner.next();
    controller.colorizedPixelClicked(context, colorized(3, 4), false);
    expect(owner.silent(), "painting the same color again does nothing");

    // Frame 2 still needs the pixel, frames 0 and 1 are skipped; the text counts the selection.
    document.setColorizedPixel(2, Resolution::Sd, 3, 4, 0);
    controller.colorizedPixelClicked(context, colorized(3, 4), true);
    expect(owner.committed(EditKind::FramePixels, {2}), "unchanged frames get no command");
    expect(owner.status == "Painted pixel at 3,4 on 3 frame(s)", "status counts selected frames");
    expect(document.colorizedPixel(2, Resolution::Sd, 3, 4) == kDrawColor,
           "erase does not change what RGB painting does");

    owner.next();
    controller.colorizedPixelClicked(context, colorized(9, 9, Resolution::Hd), false);
    expect(owner.committed(EditKind::FramePixels, {0, 2}, Resolution::Hd),
           "HD painting skips frames without HD data");
    expect(owner.status == "Painted pixel at 9,9 on 3 frame(s)", "HD status");

    owner.next();
    context.selectedFrames = {1};
    context.currentFrame = 1;
    controller.colorizedPixelClicked(context, colorized(5, 5), false);
    expect(owner.committed(EditKind::FramePixels, {1}) && owner.status == "Painted pixel at 5,5",
           "single frame status has no frame count");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(5, 5, Resolution::Hd), false);
    expect(owner.silent(), "HD painting on an SD-only frame does nothing");

    owner.next();
    context.tool = DrawingTool::ColorPicker;
    controller.colorizedPixelClicked(context, colorized(5, 5), false);
    expect(owner.pickedColors == std::vector<uint16_t>{kDrawColor} && owner.commits.empty() &&
               owner.status == "Picked color 0xf81f",
           "picker reports the frame color");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(500, 5), false);
    expect(owner.pickedColors.empty() && !owner.status.empty() && owner.statusMs == 4000,
           "picker outside the frame reports the error");

    // A 2x2 island of another color inside the frame.
    owner.next();
    for (const Point& point : {Point{20, 20}, Point{21, 20}, Point{20, 21}, Point{21, 21}}) {
        document.setColorizedPixel(1, Resolution::Sd, point.x, point.y, 0x1234);
    }
    document.setColorizedPixel(3, Resolution::Sd, 20, 20, 0x1234);
    context.tool = DrawingTool::FloodFill;
    context.selectedFrames = {1, 3};
    controller.colorizedPixelClicked(context, colorized(21, 21), false);
    expect(owner.committed(EditKind::FramePixels, {1, 3}) &&
               owner.status == "Flood filled area on 2 frame(s)",
           "flood fill status");
    expect(document.colorizedPixel(1, Resolution::Sd, 20, 20) == kDrawColor &&
               document.colorizedPixel(1, Resolution::Sd, 22, 21) == 0,
           "flood fill stays inside the area of the current frame");
    expect(document.colorizedPixel(3, Resolution::Sd, 20, 20) == kDrawColor &&
               document.colorizedPixel(3, Resolution::Sd, 21, 21) == kDrawColor,
           "the area of the current frame is painted on the other selected frames");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(21, 21), false);
    expect(owner.silent(), "flood fill on the draw color does nothing");

    owner.next();
    context.document = nullptr;
    context.selectedFrames.clear();
    context.currentFrame.reset();
    for (DrawingTool tool :
         {DrawingTool::Point, DrawingTool::ColorPicker, DrawingTool::FloodFill}) {
        context.tool = tool;
        controller.colorizedPixelClicked(context, colorized(1, 1), false);
    }
    expect(owner.silent() && owner.pickedColors.empty(), "nothing happens without a document");
}

void testFramePixelStrokesAndPreview()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::FilledRectangle, {0, 1});

    const std::vector<Point> rectangle = edit::makeFilledRectanglePixels(Point{2, 2}, Point{5, 4});
    controller.strokePreviewed(context, colorized(2, 2), colorized(5, 4), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     rectangle,
                     drawColor(150)),
           "stroke preview shows the shape in the draw color");
    expect(owner.silent() && !document.isDirty(), "a preview edits nothing");

    controller.strokeFinished(context, colorized(2, 2), colorized(5, 4), false);
    expect(owner.committed(EditKind::FramePixels, {0, 1}) &&
               owner.status == "Painted filled rectangle on 2 frame(s)",
           "filled rectangle stroke");
    for (const Point& point : rectangle) {
        expect(document.colorizedPixel(1, Resolution::Sd, point.x, point.y) == kDrawColor,
               "rectangle pixel painted");
    }
    expect(document.colorizedPixel(1, Resolution::Sd, 6, 4) == 0, "pixel next to the rectangle");

    const std::vector<std::pair<DrawingTool, std::string>> tools = {
        {DrawingTool::Line, "Painted line"},
        {DrawingTool::Rectangle, "Painted rectangle"},
        {DrawingTool::Circle, "Painted circle"},
        {DrawingTool::FilledCircle, "Painted filled circle"},
        {DrawingTool::Ellipse, "Painted ellipse"},
        {DrawingTool::FilledEllipse, "Painted filled ellipse"},
    };
    context.selectedFrames = {3};
    context.currentFrame = 3;
    int row = 0;
    for (const auto& [tool, text] : tools) {
        owner.next();
        context.tool = tool;
        const CanvasHit start = colorized(40, 10 + row);
        const CanvasHit end = colorized(52, 16 + row);
        controller.strokeFinished(context, start, end, false);
        expect(owner.committed(EditKind::FramePixels, {3}) && owner.status == text, text);
        for (const Point& point : edit::makeStrokePixels(tool,
                                                         PaletteGradientShape::Rectangle,
                                                         Point{start.x, start.y},
                                                         Point{end.x, end.y})) {
            expect(document.colorizedPixel(3, Resolution::Sd, point.x, point.y) == kDrawColor,
                   text + " pixel");
        }
        ++row;
    }

    owner.next();
    owner.preview.reset();
    context.tool = DrawingTool::Line;
    controller.strokePreviewed(context, colorized(1, 1), original(5, 5), false);
    controller.strokeFinished(context, colorized(1, 1), original(5, 5), false);
    controller.strokePreviewed(context, colorized(1, 1), colorized(5, 5, Resolution::Hd), false);
    controller.strokeFinished(context, colorized(1, 1), colorized(5, 5, Resolution::Hd), false);
    controller.strokePreviewed(context, original(1, 1), original(5, 5), false);
    controller.strokeFinished(context, original(1, 1), original(5, 5), false);
    expect(owner.silent() && !owner.preview,
           "strokes across regions or resolutions, or on the original frame, are ignored");

    for (DrawingTool tool : {DrawingTool::Point, DrawingTool::FloodFill}) {
        context.tool = tool;
        controller.strokePreviewed(context, colorized(70, 1), colorized(75, 5), false);
        controller.strokeFinished(context, colorized(70, 1), colorized(75, 5), false);
    }
    expect(owner.silent() && !owner.preview, "click tools ignore strokes");
}

void testBackgroundImageLayer()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::BackgroundImage, DrawingTool::Point, {0, 1, 2});

    controller.colorizedPixelClicked(context, colorized(7, 8), true);
    expect(owner.committed(EditKind::BackgroundImage, {1}) &&
               owner.status == "Painted background pixel at 7,8",
           "background point: one command, no frame count");
    expect(document.backgroundPixel(1, Resolution::Sd, 7, 8) == kDrawColor &&
               document.colorizedPixel(0, Resolution::Sd, 7, 8) == 0,
           "background point edits the selected background only");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(7, 8), false);
    expect(owner.silent(), "background point with the same color");

    owner.next();
    context.tool = DrawingTool::ColorPicker;
    controller.colorizedPixelClicked(context, colorized(0, 0), false);
    expect(owner.pickedColors == std::vector<uint16_t>{0x0800} &&
               owner.status == "Picked background color 0x0800",
           "background picker");

    owner.next();
    context.tool = DrawingTool::FilledRectangle;
    controller.strokePreviewed(context, colorized(10, 10), colorized(12, 11), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     edit::makeFilledRectanglePixels(Point{10, 10}, Point{12, 11}),
                     drawColor(150)),
           "background stroke preview");
    controller.strokeFinished(context, colorized(10, 10), colorized(12, 11), false);
    expect(owner.committed(EditKind::BackgroundImage, {1}) &&
               owner.status == "Painted background filled rectangle",
           "background stroke");
    expect(document.backgroundPixel(1, Resolution::Sd, 12, 11) == kDrawColor, "stroke pixel");

    // The rectangle is an island now: fill it back.
    owner.next();
    context.tool = DrawingTool::FloodFill;
    context.drawColor = 0x0800;
    controller.colorizedPixelClicked(context, colorized(11, 10), false);
    expect(owner.committed(EditKind::BackgroundImage, {1}) &&
               owner.status == "Flood filled background",
           "background flood fill");
    expect(document.backgroundPixel(1, Resolution::Sd, 10, 10) == 0x0800 &&
               document.backgroundPixel(1, Resolution::Sd, 7, 8) == kDrawColor,
           "flood fill covers the island only");

    owner.next();
    context.selectedBackground = 0;
    context.tool = DrawingTool::Point;
    context.drawColor = kDrawColor;
    controller.colorizedPixelClicked(context, colorized(200, 60, Resolution::Hd), false);
    expect(owner.committed(EditKind::BackgroundImage, {0}, Resolution::Hd) &&
               document.backgroundPixel(0, Resolution::Hd, 200, 60) == kDrawColor,
           "HD background point");
}

void testComparisonMaskLayer()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::ComparisonMask, DrawingTool::Point, {0, 1, 2, 3});

    controller.originalPixelClicked(context, original(4, 5), false);
    expect(owner.committed(EditKind::ComparisonMask, {0, 1}) &&
               owner.status == "Painted comparison mask pixel on 2 mask(s)",
           "mask point edits each mask of the selected frames once");
    expect(document.comparisonMaskPixel(0, 4, 5) == 1 && document.comparisonMaskPixel(1, 4, 5) == 1,
           "mask pixels set");

    owner.next();
    context.selectedFrames = {0, 2};
    controller.originalPixelClicked(context, original(4, 5), true);
    expect(owner.committed(EditKind::ComparisonMask, {0}) &&
               owner.status == "Erased comparison mask pixel",
           "frames sharing a mask give one command and no count");
    expect(document.comparisonMaskPixel(0, 4, 5) == 0 && document.comparisonMaskPixel(1, 4, 5) == 1,
           "erase clears the mask pixel");
    owner.next();
    controller.originalPixelClicked(context, original(4, 5), true);
    expect(owner.silent(), "erasing a clear mask pixel");

    owner.next();
    context.selectedFrames = {3};
    context.currentFrame = 3;
    controller.originalPixelClicked(context, original(4, 5), false);
    expect(owner.commits.empty() && owner.status == "No comparison mask assigned",
           "frame without a mask");
    context.tool = DrawingTool::FloodFill;
    owner.next();
    controller.originalPixelClicked(context, original(4, 5), false);
    expect(owner.commits.empty() && owner.status == "No comparison mask assigned",
           "flood fill on a frame without a mask");

    owner.next();
    context.selectedFrames = {1};
    context.currentFrame = 1;
    controller.originalPixelClicked(context, original(100, 20), false);
    expect(owner.committed(EditKind::ComparisonMask, {1}) &&
               owner.status == "Flood filled comparison mask",
           "mask flood fill");
    expect(document.comparisonMaskPixel(1, 0, 0) == 1 &&
               document.comparisonMaskPixel(1, 127, 31) == 1,
           "flood fill sets the whole clear area");
    owner.next();
    controller.originalPixelClicked(context, original(100, 20), false);
    expect(owner.silent(), "flood fill on a set area");
    controller.originalPixelClicked(context, original(100, 20), true);
    expect(owner.committed(EditKind::ComparisonMask, {1}) &&
               owner.status == "Flood erased comparison mask" &&
               document.comparisonMaskPixel(1, 4, 5) == 0,
           "mask flood erase");

    owner.next();
    context.tool = DrawingTool::Point;
    controller.colorizedPixelClicked(context, colorized(4, 5), false);
    expect(owner.silent() && document.colorizedPixel(1, Resolution::Sd, 4, 5) == 0,
           "clicks on the colorized frame do nothing in mask mode");

    context.tool = DrawingTool::FilledRectangle;
    const std::vector<Point> rectangle = edit::makeFilledRectanglePixels(Point{1, 1}, Point{3, 2});
    controller.strokePreviewed(context, original(1, 1), original(3, 2), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::OriginalFrame,
                     Resolution::Sd,
                     rectangle,
                     QColor(255, 0, 0, 150)),
           "mask stroke preview");
    controller.strokePreviewed(context, original(1, 1), original(3, 2), true);
    expect(isOverlay(owner.preview,
                     CanvasRegion::OriginalFrame,
                     Resolution::Sd,
                     rectangle,
                     QColor(0, 0, 0, 150)),
           "mask erase preview");
    controller.strokeFinished(context, original(1, 1), original(3, 2), false);
    expect(owner.committed(EditKind::ComparisonMask, {1}) &&
               owner.status == "Painted comparison mask filled rectangle" &&
               document.comparisonMaskPixel(1, 3, 2) == 1,
           "mask stroke");
    owner.next();
    context.tool = DrawingTool::Line;
    controller.strokeFinished(context, original(1, 1), original(3, 1), true);
    expect(owner.committed(EditKind::ComparisonMask, {1}) &&
               owner.status == "Erased comparison mask line" &&
               document.comparisonMaskPixel(1, 2, 1) == 0 &&
               document.comparisonMaskPixel(1, 2, 2) == 1,
           "mask erase stroke");

    owner.next();
    owner.preview.reset();
    controller.strokePreviewed(context, colorized(1, 1), colorized(3, 1), false);
    controller.strokeFinished(context, colorized(1, 1), colorized(3, 1), false);
    expect(owner.silent() && !owner.preview, "mask mode ignores strokes on the colorized frame");

    context.tool = DrawingTool::PaletteGradient;
    controller.strokeFinished(context, original(1, 1), original(3, 1), false);
    expect(owner.commits.empty() && owner.status == "Palette gradients edit RGB pixels only",
           "no gradients on masks");
}

void testDynamicMaskLayer()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::DynamicMask, DrawingTool::Point, {0, 1, 2});

    controller.colorizedPixelClicked(context, colorized(6, 7), false);
    expect(owner.committed(EditKind::DynamicMask, {0, 2}) &&
               owner.status == "Painted dynamic mask pixel on 3 frame(s)",
           "dynamic point skips frames without dynamic data");
    expect(document.dynamicMaskPixel(0, Resolution::Sd, 6, 7) == 3 &&
               document.dynamicMaskPixel(2, Resolution::Sd, 6, 7) == 3,
           "dynamic set painted");

    owner.next();
    context.tool = DrawingTool::ColorPicker;
    controller.colorizedPixelClicked(context, colorized(6, 7), false);
    expect(owner.pickedSets == std::vector<uint8_t>{3} &&
               owner.status == "Picked dynamic set 3 at 6,7",
           "dynamic set picker");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(7, 7), false);
    expect(owner.pickedSets.empty() && owner.status == "No dynamic set at 7,7",
           "picker on a pixel without a set");

    owner.next();
    context.tool = DrawingTool::Point;
    context.selectedFrames = {0};
    controller.colorizedPixelClicked(context, colorized(6, 7), true);
    expect(owner.committed(EditKind::DynamicMask, {0}) &&
               owner.status == "Erased dynamic mask pixel" &&
               document.dynamicMaskPixel(0, Resolution::Sd, 6, 7) == 255,
           "dynamic erase writes 255");

    owner.next();
    context.selectedFrames = {1};
    context.currentFrame = 1;
    controller.colorizedPixelClicked(context, colorized(6, 7), false);
    expect(owner.silent(), "frame without dynamic data");

    owner.next();
    context.selectedFrames = {2};
    context.currentFrame = 2;
    context.tool = DrawingTool::FilledRectangle;
    controller.strokePreviewed(context, colorized(5, 6), colorized(7, 8), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     edit::makeFilledRectanglePixels(Point{5, 6}, Point{7, 8}),
                     QColor(0, 180, 255, 150)),
           "dynamic stroke preview");
    context.tool = DrawingTool::Rectangle;
    controller.strokeFinished(context, colorized(5, 6), colorized(7, 8), false);
    expect(owner.committed(EditKind::DynamicMask, {2}) &&
               owner.status == "Painted dynamic mask rectangle",
           "dynamic stroke");
    // The ring now encloses the pixel painted first.
    owner.next();
    context.tool = DrawingTool::FloodFill;
    context.dynamicSet = 5;
    controller.colorizedPixelClicked(context, colorized(6, 7), false);
    expect(owner.committed(EditKind::DynamicMask, {2}) &&
               owner.status == "Flood filled dynamic mask" &&
               document.dynamicMaskPixel(2, Resolution::Sd, 5, 6) == 5 &&
               document.dynamicMaskPixel(2, Resolution::Sd, 6, 7) == 5 &&
               document.dynamicMaskPixel(2, Resolution::Sd, 9, 9) == 255,
           "dynamic flood fill");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(6, 7), true);
    expect(owner.committed(EditKind::DynamicMask, {2}) &&
               owner.status == "Flood erased dynamic mask" &&
               document.dynamicMaskPixel(2, Resolution::Sd, 5, 6) == 255,
           "dynamic flood erase");

    owner.next();
    context.tool = DrawingTool::Point;
    context.selectedFrames = {0, 2};
    controller.colorizedPixelClicked(context, colorized(30, 30, Resolution::Hd), false);
    expect(owner.commits.empty() && owner.finished == 0 &&
               document.dynamicMaskPixel(0, Resolution::Hd, 30, 30) == 255 &&
               owner.status == "Dynamic zones are defined on the 128x32 plane and cannot be "
                               "picked or painted on this one",
           "no dynamic point on the HD plane of an ordinary project");
    owner.next();
    context.tool = DrawingTool::FloodFill;
    controller.colorizedPixelClicked(context, colorized(30, 30, Resolution::Hd), false);
    expect(owner.commits.empty() && !owner.status.empty(),
           "no dynamic flood fill on the HD plane of an ordinary project");
    owner.next();
    context.tool = DrawingTool::ColorPicker;
    controller.colorizedPixelClicked(context, colorized(30, 30, Resolution::Hd), false);
    expect(owner.pickedSets.empty() && !owner.status.empty(),
           "no dynamic pick on the HD plane of an ordinary project");
    owner.next();
    context.tool = DrawingTool::Rectangle;
    controller.strokeFinished(context,
                              colorized(30, 30, Resolution::Hd),
                              colorized(34, 34, Resolution::Hd),
                              false);
    expect(owner.commits.empty() && !owner.status.empty() &&
               document.dynamicMaskPixel(0, Resolution::Hd, 30, 30) == 255,
           "no dynamic stroke on the HD plane of an ordinary project");
    context.tool = DrawingTool::Point;

    owner.next();
    controller.originalPixelClicked(context, original(1, 1), false);
    expect(owner.silent(), "clicks on the original frame do nothing outside mask mode");
    context.tool = DrawingTool::PaletteGradient;
    controller.strokeFinished(context, colorized(1, 1), colorized(3, 1), false);
    expect(owner.commits.empty() && owner.status == "Palette gradients edit RGB pixels only",
           "no gradients on the dynamic mask");
}

void testBackgroundMaskLayer()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::BackgroundMask, DrawingTool::Point, {0, 1, 2});

    controller.colorizedPixelClicked(context, colorized(8, 9), false);
    expect(owner.committed(EditKind::BackgroundMask, {0}) &&
               owner.status == "Painted background mask pixel on 3 frame(s)" &&
               document.backgroundMaskPixel(0, Resolution::Sd, 8, 9) == 1,
           "background mask point skips frames without an existing background");

    owner.next();
    controller.colorizedPixelClicked(context, colorized(8, 9), false);
    expect(owner.silent(), "background mask point already set");

    owner.next();
    context.selectedFrames = {1, 2};
    context.currentFrame = 1;
    controller.colorizedPixelClicked(context, colorized(8, 9), false);
    expect(owner.commits.empty() &&
               owner.status == "No selected frame has an assigned background",
           "no frame with a background");

    owner.next();
    context.selectedFrames = {0};
    context.currentFrame = 0;
    controller.colorizedPixelClicked(context, colorized(8, 9), true);
    expect(owner.committed(EditKind::BackgroundMask, {0}) &&
               owner.status == "Erased background mask pixel" &&
               document.backgroundMaskPixel(0, Resolution::Sd, 8, 9) == 0,
           "background mask erase");

    owner.next();
    context.tool = DrawingTool::Ellipse;
    controller.strokePreviewed(context, colorized(20, 5), colorized(30, 11), true);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     edit::makeEllipsePixels(Point{20, 5}, Point{30, 11}),
                     QColor(0, 0, 0, 150)),
           "background mask erase preview");
    controller.strokePreviewed(context, colorized(20, 5), colorized(30, 11), false);
    expect(owner.preview && owner.preview->color == QColor(255, 200, 0, 150),
           "background mask preview color");
    controller.strokeFinished(context, colorized(20, 5), colorized(30, 11), false);
    expect(owner.committed(EditKind::BackgroundMask, {0}) &&
               owner.status == "Painted background mask ellipse",
           "background mask stroke");

    owner.next();
    context.tool = DrawingTool::FloodFill;
    controller.colorizedPixelClicked(context, colorized(25, 8), false);
    expect(owner.committed(EditKind::BackgroundMask, {0}) &&
               owner.status == "Flood filled background mask" &&
               document.backgroundMaskPixel(0, Resolution::Sd, 25, 8) == 1 &&
               document.backgroundMaskPixel(0, Resolution::Sd, 0, 0) == 0,
           "background mask flood fill stays inside the ellipse");
    owner.next();
    controller.colorizedPixelClicked(context, colorized(25, 8), true);
    expect(owner.committed(EditKind::BackgroundMask, {0}) &&
               owner.status == "Flood erased background mask",
           "background mask flood erase");

    owner.next();
    context.tool = DrawingTool::ColorPicker;
    controller.colorizedPixelClicked(context, colorized(25, 8), false);
    expect(owner.silent() && owner.pickedColors.empty(), "no picker on the background mask");
}

edit::PaletteGradient gradientOf(const CanvasEditContext& context,
                                 Point shapeStart,
                                 Point shapeEnd,
                                 Point directionStart,
                                 Point directionEnd)
{
    const std::pair<int, int> endpoints = context.gradientEndpoints();
    return edit::PaletteGradient{
        shapeStart,
        shapeEnd,
        directionStart,
        directionEnd,
        context.gradientDirection,
        context.gradientPattern,
        endpoints.first,
        endpoints.second,
    };
}

void testPaletteGradientTool()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::PaletteGradient, {0, 1});
    context.gradientDirection = PaletteGradientDirection::TopToBottom;

    const Point start{10, 2};
    const Point end{15, 9};
    controller.strokeFinished(context, colorized(10, 2), colorized(15, 9), false);
    expect(owner.committed(EditKind::FramePixels, {0, 1}) &&
               owner.status == "Filled palette gradient on 2 frame(s)" &&
               !controller.isFreeGradientPending(),
           "a fixed direction fills with one stroke");
    const edit::PaletteGradient gradient = gradientOf(context, start, end, start, end);
    for (const Point& point : edit::makeFilledRectanglePixels(start, end)) {
        expect(document.colorizedPixel(1, Resolution::Sd, point.x, point.y) ==
                   document.paletteColor(1, edit::paletteGradientColorIndex(gradient, point)),
               "gradient pixel has its palette color");
    }
    expect(document.colorizedPixel(1, Resolution::Sd, 10, 2) == 0x202 &&
               document.colorizedPixel(1, Resolution::Sd, 15, 9) == 0x205,
           "gradient runs between the two palette endpoints");

    owner.next();
    context.gradientShape = PaletteGradientShape::Circle;
    controller.strokePreviewed(context, colorized(40, 5), colorized(50, 15), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     edit::makeFilledCirclePixels(Point{40, 5}, Point{50, 15}),
                     drawColor(150)),
           "gradient preview follows the gradient shape");
    controller.colorizedPixelClicked(context, colorized(40, 5), false);
    expect(owner.silent(), "a click with the gradient tool paints nothing");

    owner.next();
    context.selectedPalette.reset();
    controller.strokeFinished(context, colorized(40, 5), colorized(50, 15), false);
    expect(owner.commits.empty() && owner.status == "No palette gradient source selected",
           "gradient without a palette");

    owner.next();
    CanvasEditContext background =
        makeContext(document, CanvasEditLayer::BackgroundImage, DrawingTool::PaletteGradient, {0});
    controller.strokeFinished(background, colorized(1, 1), colorized(4, 1), false);
    expect(owner.committed(EditKind::BackgroundImage, {1}) &&
               owner.status == "Filled palette gradient" &&
               document.backgroundPixel(1, Resolution::Sd, 1, 1) == 0x202 &&
               document.backgroundPixel(1, Resolution::Sd, 4, 1) == 0x205,
           "background gradient");
    owner.next();
    background.selectedPalette.reset();
    controller.strokeFinished(background, colorized(1, 1), colorized(4, 1), false);
    expect(owner.commits.empty() && owner.status == "No background or palette selected",
           "background gradient without a palette");
}

void testFreePaletteGradientTwoSteps()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::PaletteGradient, {0, 1});
    context.gradientDirection = PaletteGradientDirection::Free;

    const Point shapeStart{10, 2};
    const Point shapeEnd{17, 5};
    const std::vector<Point> shape = edit::makeFilledRectanglePixels(shapeStart, shapeEnd);
    controller.strokeFinished(context, colorized(10, 2), colorized(17, 5), false);
    expect(controller.isFreeGradientPending() && owner.commits.empty() &&
               owner.status == "Draw gradient direction line",
           "the first stroke only marks the area");
    const auto showsPendingArea = [&]() {
        return isOverlay(owner.preview,
                         CanvasRegion::ColorizedFrame,
                         Resolution::Sd,
                         shape,
                         drawColor(120));
    };
    expect(showsPendingArea(), "the pending area stays on the canvas");

    owner.next();
    std::vector<Point> withLine = shape;
    const std::vector<Point> line = edit::makeLinePixels(Point{17, 3}, Point{10, 3});
    withLine.insert(withLine.end(), line.begin(), line.end());
    controller.strokePreviewed(context, colorized(17, 3), colorized(10, 3), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     withLine,
                     drawColor(145)),
           "the direction stroke previews area and line");
    controller.colorizedPixelClicked(context, colorized(12, 3), false);
    expect(showsPendingArea() && owner.silent() && controller.isFreeGradientPending(),
           "a click shows the pending area again");

    controller.strokeFinished(context, colorized(17, 3), colorized(10, 3), false);
    expect(owner.committed(EditKind::FramePixels, {0, 1}) &&
               owner.status == "Filled palette gradient on 2 frame(s)" &&
               !controller.isFreeGradientPending() && !owner.preview,
           "the second stroke fills the area along its line");
    const edit::PaletteGradient gradient =
        gradientOf(context, shapeStart, shapeEnd, Point{17, 3}, Point{10, 3});
    for (const Point& point : shape) {
        expect(document.colorizedPixel(0, Resolution::Sd, point.x, point.y) ==
                   document.paletteColor(1, edit::paletteGradientColorIndex(gradient, point)),
               "free gradient pixel");
    }
    expect(document.colorizedPixel(0, Resolution::Sd, 17, 2) == 0x202 &&
               document.colorizedPixel(0, Resolution::Sd, 10, 5) == 0x205,
           "the free gradient follows the direction line");
    expect(document.colorizedPixel(0, Resolution::Sd, 18, 3) == 0, "nothing outside the area");

    // The target changes between the two strokes.
    owner.next();
    controller.strokeFinished(context, colorized(30, 2), colorized(35, 5), false);
    CanvasEditContext background =
        makeContext(document, CanvasEditLayer::BackgroundImage, DrawingTool::PaletteGradient, {0});
    background.gradientDirection = PaletteGradientDirection::Free;
    owner.next();
    controller.strokeFinished(background, colorized(30, 3), colorized(35, 3), false);
    expect(owner.commits.empty() && owner.status == "Free gradient selection cancelled" &&
               !controller.isFreeGradientPending() && !owner.preview,
           "a pending frame gradient is not filled into a background");

    owner.next();
    controller.strokeFinished(background, colorized(30, 2), colorized(35, 5), false);
    expect(controller.isFreeGradientPending() && owner.status == "Draw gradient direction line",
           "background gradient pending");
    background.selectedBackground = 0;
    owner.next();
    controller.strokeFinished(background, colorized(30, 3), colorized(35, 3), false);
    expect(owner.commits.empty() && owner.status == "Free gradient selection cancelled",
           "another background cancels the pending gradient");

    owner.next();
    controller.strokeFinished(background, colorized(30, 2), colorized(35, 5), false);
    controller.strokeFinished(background, colorized(30, 3), colorized(35, 3), false);
    expect(owner.committed(EditKind::BackgroundImage, {0}) &&
               owner.status == "Filled palette gradient" &&
               document.backgroundPixel(0, Resolution::Sd, 30, 5) == 0x202 &&
               document.backgroundPixel(0, Resolution::Sd, 35, 2) == 0x205,
           "free background gradient");

    // The resolution changes between the two strokes.
    owner.next();
    controller.strokeFinished(context, colorized(60, 2), colorized(65, 5), false);
    owner.next();
    controller.strokeFinished(context,
                              colorized(60, 3, Resolution::Hd),
                              colorized(65, 3, Resolution::Hd),
                              false);
    expect(owner.commits.empty() && owner.status == "Free gradient selection cancelled",
           "another resolution cancels the pending gradient");

    owner.next();
    controller.strokeFinished(context, colorized(60, 2), colorized(65, 5), false);
    owner.next();
    controller.cancelFreeGradient();
    expect(!controller.isFreeGradientPending() && !owner.preview && owner.previewClears == 1 &&
               owner.silent(),
           "cancelling drops the pending area and its preview");

    // The paint target says Background while no background is selected: the area is remembered
    // for the background, so the direction stroke on the frame layer never fills it.
    owner.next();
    context.paintTarget = PaintTarget::Background;
    controller.strokeFinished(context, colorized(60, 2), colorized(65, 5), false);
    expect(controller.isFreeGradientPending(), "pending with an unusable background target");
    owner.next();
    controller.strokeFinished(context, colorized(60, 3), colorized(65, 3), false);
    expect(owner.commits.empty() && owner.status == "Free gradient selection cancelled",
           "existing behavior: unusable background target cancels the free gradient");
}

void testCanvasSelection()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::Selection, {0, 1});
    context.gradientDirection = PaletteGradientDirection::LeftToRight;

    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.commits.empty() && owner.status == "No canvas selection",
           "fill without selection");
    owner.next();
    controller.toggleSelectionDirectionMode();
    expect(!controller.isSelectionDirectionMode() &&
               owner.directionModes == std::vector<bool>{false} && owner.selectionChanges == 1 &&
               owner.status.empty(),
           "direction mode cannot be entered without a selection");

    owner.next();
    const Point start{20, 4};
    const Point end{27, 6};
    const std::vector<Point> area = edit::makeFilledRectanglePixels(start, end);
    controller.strokePreviewed(context, colorized(20, 4), colorized(27, 6), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     area,
                     QColor(0, 220, 180, 130)),
           "selection stroke preview");
    controller.strokeFinished(context, colorized(27, 6), colorized(20, 4), false);
    expect(controller.hasSelection() && owner.commits.empty() &&
               owner.status == "Selected canvas area" && owner.statusMs == 2000 &&
               owner.selectionChanges == 1 && owner.directionModes == std::vector<bool>{false},
           "selection stroke selects without editing");
    expect(isOverlay(owner.selectionOverlay,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     area,
                     QColor(0, 220, 180, 95)),
           "selection overlay holds the area row by row");

    owner.next();
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.committed(EditKind::FramePixels, {0, 1}) &&
               owner.status == "Filled selection gradient" && controller.hasSelection(),
           "fill selection on every selected frame");
    // The stroke ran from bottom right to top left; a fixed direction ignores that.
    const edit::PaletteGradient gradient = gradientOf(context, end, start, end, start);
    for (const Point& point : area) {
        expect(document.colorizedPixel(1, Resolution::Sd, point.x, point.y) ==
                   document.paletteColor(1, edit::paletteGradientColorIndex(gradient, point)),
               "selection gradient pixel");
    }
    expect(document.colorizedPixel(0, Resolution::Sd, 20, 4) == 0x202 &&
               document.colorizedPixel(0, Resolution::Sd, 27, 6) == 0x205,
           "selection gradient runs left to right");
    owner.next();
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.silent(), "filling again changes nothing");

    owner.next();
    context.selectedPalette.reset();
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.commits.empty() && owner.status == "No palette selected", "fill without palette");
    context.selectedPalette = 0;

    owner.next();
    context.gradientDirection = PaletteGradientDirection::Free;
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.commits.empty() && owner.status == "Set a selection direction line first" &&
               owner.statusMs == 3000,
           "free fill needs a direction line");

    // Direction mode: the next stroke of a drawing tool is the direction line.
    owner.next();
    controller.toggleSelectionDirectionMode();
    expect(controller.isSelectionDirectionMode() && controller.isSelectionDirectionModeActive() &&
               owner.directionModes == std::vector<bool>{true} &&
               owner.status == "Draw selection gradient direction line",
           "direction mode on");
    owner.next();
    controller.toggleSelectionDirectionMode();
    expect(!controller.isSelectionDirectionMode() &&
               owner.directionModes == std::vector<bool>{false} && owner.status.empty(),
           "direction mode off");
    controller.toggleSelectionDirectionMode();

    owner.next();
    context.tool = DrawingTool::Line;
    controller.colorizedPixelClicked(context, colorized(1, 1), false);
    context.tool = DrawingTool::Point;
    controller.colorizedPixelClicked(context, colorized(1, 1), false);
    expect(owner.silent() && document.colorizedPixel(0, Resolution::Sd, 1, 1) == 0,
           "clicks do not paint in direction mode");

    std::vector<Point> areaWithLine = area;
    const std::vector<Point> line = edit::makeLinePixels(Point{27, 5}, Point{20, 5});
    areaWithLine.insert(areaWithLine.end(), line.begin(), line.end());
    context.tool = DrawingTool::Line;
    owner.preview.reset();
    controller.strokePreviewed(context, colorized(27, 5), colorized(20, 5), false);
    expect(!owner.preview && isOverlay(owner.selectionOverlay,
                                       CanvasRegion::ColorizedFrame,
                                       Resolution::Sd,
                                       areaWithLine,
                                       QColor(0, 220, 180, 120)),
           "direction stroke previews in the selection overlay");
    controller.strokeFinished(context, original(27, 5), original(20, 5), false);
    expect(owner.silent() && controller.isSelectionDirectionMode(),
           "a direction line on another region is ignored");
    controller.strokeFinished(context, colorized(27, 5), colorized(20, 5), false);
    expect(owner.commits.empty() && owner.status == "Set selection gradient direction" &&
               !controller.isSelectionDirectionMode() &&
               owner.directionModes == std::vector<bool>{false},
           "direction stroke sets the line and leaves direction mode");
    expect(isOverlay(owner.selectionOverlay,
                     CanvasRegion::ColorizedFrame,
                     Resolution::Sd,
                     areaWithLine,
                     QColor(0, 220, 180, 95)),
           "selection overlay shows the direction line");

    owner.next();
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.committed(EditKind::FramePixels, {0, 1}) &&
               owner.status == "Filled selection gradient" &&
               document.colorizedPixel(0, Resolution::Sd, 27, 4) == 0x102 &&
               document.colorizedPixel(0, Resolution::Sd, 20, 6) == 0x105,
           "free fill follows the direction line");

    // HD selection: frames without HD data are skipped.
    owner.next();
    context.tool = DrawingTool::Selection;
    context.gradientDirection = PaletteGradientDirection::LeftToRight;
    controller.strokeFinished(context,
                              colorized(100, 40, Resolution::Hd),
                              colorized(103, 41, Resolution::Hd),
                              false);
    owner.next();
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.committed(EditKind::FramePixels, {0}, Resolution::Hd) &&
               document.colorizedPixel(0, Resolution::Hd, 103, 41) == 0x105,
           "HD selection fill");

    owner.next();
    controller.clearSelection(context);
    expect(!controller.hasSelection() && !owner.selectionOverlay &&
               owner.selectionOverlayClears == 1 && owner.selectionChanges == 1 &&
               owner.directionModes == std::vector<bool>{false} && owner.status.empty(),
           "clear selection");

    owner.next();
    controller.strokeFinished(context, original(1, 1), original(4, 4), false);
    expect(!controller.hasSelection() && owner.silent(), "no selection on the original frame");

    // Leaving direction mode silently keeps the selection.
    controller.strokeFinished(context, colorized(1, 1), colorized(4, 4), false);
    controller.toggleSelectionDirectionMode();
    owner.next();
    controller.leaveSelectionDirectionMode();
    expect(!controller.isSelectionDirectionMode() && controller.hasSelection() &&
               owner.directionModes.empty() && owner.selectionChanges == 0,
           "leaving direction mode tells nobody");

    // A new selection ends direction mode.
    controller.toggleSelectionDirectionMode();
    owner.next();
    controller.strokeFinished(context, colorized(2, 2), colorized(3, 3), false);
    expect(!controller.isSelectionDirectionMode() &&
               owner.directionModes == std::vector<bool>{false},
           "selecting again leaves direction mode");
}

void testCanvasSelectionOnBackground()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::BackgroundImage, DrawingTool::Selection, {0, 1});
    context.gradientShape = PaletteGradientShape::Ellipse;

    controller.strokeFinished(context, colorized(50, 10), colorized(60, 16), false);
    std::vector<Point> ellipse = edit::makeFilledEllipsePixels(Point{50, 10}, Point{60, 16});
    expect(controller.hasSelection() && owner.selectionOverlay &&
               owner.selectionOverlay->pixels.size() <= ellipse.size() &&
               !owner.selectionOverlay->pixels.empty(),
           "selection uses the gradient shape");

    owner.next();
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.committed(EditKind::BackgroundImage, {1}) &&
               owner.status == "Filled selection gradient" &&
               document.backgroundPixel(1, Resolution::Sd, 55, 13) != 0x0800 &&
               document.colorizedPixel(0, Resolution::Sd, 55, 13) == 0,
           "a background selection fills the background");

    owner.next();
    context.selectedBackground = 0;
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.commits.empty() && owner.status == "Canvas selection cleared" &&
               !controller.hasSelection() && owner.selectionChanges == 1,
           "the selection does not move to another background");

    // Mask modes paint the selection shape instead of selecting (existing behavior).
    owner.next();
    CanvasEditContext mask =
        makeContext(document, CanvasEditLayer::BackgroundMask, DrawingTool::Selection, {0});
    controller.strokeFinished(mask, colorized(1, 1), colorized(2, 2), false);
    expect(!controller.hasSelection() && owner.committed(EditKind::BackgroundMask, {0}) &&
               owner.status == "Selected canvas area",
           "existing behavior: the selection tool paints masks");
}

void testSpriteCaptureSelection()
{
    ProjectDocument document = makeDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::ComparisonMask, DrawingTool::Point, {1});
    context.spriteCaptureSelection = true;
    const QColor magenta(255, 0, 255, 110);

    controller.originalPixelClicked(context, original(10, 5), false);
    controller.originalPixelClicked(context, original(11, 5), false);
    expect(owner.commits.empty() && owner.status.empty() && owner.captureChanges == 2 &&
               !document.isDirty() && document.comparisonMaskPixel(1, 10, 5) == 0,
           "capture painting wins over the comparison mask and edits no document data");
    expect(controller.captureSelectionFrame() == std::optional<std::size_t>(1) &&
               samePoints(controller.captureSelectionPixels(1), {Point{10, 5}, Point{11, 5}}) &&
               controller.captureSelectionPixels(0).empty(),
           "capture selection belongs to the current frame");
    expect(isOverlay(owner.selectionOverlay,
                     CanvasRegion::OriginalFrame,
                     Resolution::Sd,
                     {Point{10, 5}, Point{11, 5}},
                     magenta),
           "capture selection overlay");

    owner.next();
    controller.originalPixelClicked(context, original(11, 5), false);
    expect(owner.captureChanges == 0, "marking a marked pixel reports nothing");
    controller.originalPixelClicked(context, original(11, 5), true);
    expect(owner.captureChanges == 1 &&
               samePoints(controller.captureSelectionPixels(1), {Point{10, 5}}),
           "erase unmarks");
    owner.next();
    context.tool = DrawingTool::FloodFill;
    controller.originalPixelClicked(context, original(30, 5), false);
    expect(owner.captureChanges == 0 && owner.silent(), "only the point tool marks by click");

    context.tool = DrawingTool::FilledRectangle;
    const std::vector<Point> rectangle =
        edit::makeFilledRectanglePixels(Point{20, 8}, Point{22, 9});
    controller.strokePreviewed(context, original(20, 8), original(22, 9), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::OriginalFrame,
                     Resolution::Sd,
                     rectangle,
                     QColor(255, 0, 255, 150)),
           "capture stroke preview");
    controller.strokePreviewed(context, original(20, 8), original(22, 9), true);
    expect(owner.preview && owner.preview->color == QColor(0, 0, 0, 150), "capture erase preview");
    controller.strokeFinished(context, original(20, 8), original(22, 9), false);
    expect(owner.captureChanges == 1 && controller.captureSelectionPixels(1).size() == 7 &&
               owner.commits.empty(),
           "capture stroke marks its pixels");
    owner.next();
    context.tool = DrawingTool::Line;
    controller.strokeFinished(context, original(20, 8), original(22, 8), true);
    expect(owner.captureChanges == 1 && controller.captureSelectionPixels(1).size() == 4,
           "capture erase stroke");
    owner.next();
    owner.preview.reset();
    controller.strokePreviewed(context, colorized(20, 8), colorized(22, 8), false);
    controller.strokeFinished(context, colorized(20, 8), colorized(22, 8), false);
    expect(owner.silent() && owner.captureChanges == 0 && !owner.preview,
           "capture mode ignores strokes on the colorized frame");
    context.tool = DrawingTool::FilledRectangle;
    controller.strokeFinished(context, original(120, 30), original(140, 40), false);
    expect(controller.captureSelectionPixels(1).size() == 4 + 16,
           "a stroke leaving the frame marks the part inside");

    // A click on the colorized frame still edits the layer below.
    owner.next();
    CanvasEditContext frameContext =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::Point, {1});
    frameContext.spriteCaptureSelection = true;
    controller.colorizedPixelClicked(frameContext, colorized(2, 2), false);
    expect(owner.committed(EditKind::FramePixels, {1}), "colorized clicks paint in capture mode");

    // Clearing a canvas selection must not take the capture overlay away.
    owner.next();
    controller.clearSelection(frameContext);
    expect(owner.selectionOverlay && owner.selectionOverlayClears == 0,
           "the overlay stays while the capture selection is painted");

    owner.next();
    controller.showCaptureSelection(0);
    expect(!owner.selectionOverlay && owner.selectionOverlayClears == 1,
           "another frame shows no capture selection");
    controller.showCaptureSelection(1);
    expect(owner.selectionOverlay && owner.selectionOverlay->pixels.size() == 20 &&
               owner.selectionOverlay->color == magenta,
           "the capture selection comes back with its frame");

    // Painting on another frame starts over.
    owner.next();
    frameContext.selectedFrames = {2};
    frameContext.currentFrame = 2;
    controller.originalPixelClicked(frameContext, original(1, 1), false);
    expect(controller.captureSelectionFrame() == std::optional<std::size_t>(2) &&
               controller.captureSelectionPixels(1).empty() &&
               samePoints(controller.captureSelectionPixels(2), {Point{1, 1}}),
           "one capture selection at a time");

    owner.next();
    controller.clearCaptureSelection();
    expect(!controller.captureSelectionFrame() && controller.captureSelectionPixels(2).empty() &&
               !owner.selectionOverlay && owner.captureChanges == 1,
           "clear capture selection");
    controller.originalPixelClicked(frameContext, original(1, 1), false);
    owner.next();
    controller.resetCaptureSelection();
    expect(!controller.captureSelectionFrame() && owner.captureChanges == 0 &&
               owner.selectionOverlay.has_value(),
           "reset drops the selection without telling the owner");

    // Without capture mode the original frame edits the comparison mask again.
    owner.next();
    context.spriteCaptureSelection = false;
    context.tool = DrawingTool::Point;
    controller.originalPixelClicked(context, original(10, 5), false);
    expect(owner.committed(EditKind::ComparisonMask, {1}), "mask painting without capture mode");
}

// Frame 0: no HD, mask 0. Frame 1: HD, mask 1. Originals and masks are 256x64.
ProjectDocument makeBigOriginalDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "canvas_edit_256";
    snapshot.geometry =
        colorizing::render::ProjectGeometry(colorizing::render::PlaneLayout::NativeExtra);
    snapshot.comparisonMasks.assign(2, document::makeIndexedFrame(kHdFrameSize, 0));
    for (uint8_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
        snapshot.frames.push_back(document::ProjectFrameSnapshot{
            document::makeIndexedFrame(kHdFrameSize, 0),
            document::makeRgb565Frame(kSdFrameSize, 0),
            std::nullopt,
            0,
            frameIndex,
        });
    }
    snapshot.frames[1].colorizedHd = document::makeRgb565Frame(kHdFrameSize, 0);
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

CanvasHit bigOriginal(int x, int y)
{
    return CanvasHit{CanvasRegion::OriginalFrame, Resolution::Hd, x, y};
}

std::size_t setMaskPixelCount(const ProjectDocument& document, std::size_t maskIndex)
{
    const document::IndexedFrame& mask = document.comparisonMask(maskIndex);
    std::size_t count = 0;
    for (uint8_t value : mask.pixels) {
        count += value != 0 ? 1 : 0;
    }
    return count;
}

void testComparisonMaskOnBigOriginal()
{
    ProjectDocument document = makeBigOriginalDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::ComparisonMask, DrawingTool::Point, {0});

    controller.originalPixelClicked(context, bigOriginal(200, 50), false);
    owner.next();
    controller.originalPixelClicked(context, bigOriginal(255, 63), false);
    expect(document.comparisonMaskPixel(0, 200, 50) == 1 &&
               document.comparisonMaskPixel(0, 255, 63) == 1 &&
               document.comparisonMaskPixel(0, 100, 25) == 0 &&
               setMaskPixelCount(document, 0) == 2 && setMaskPixelCount(document, 1) == 0,
           "mask points are original coordinates on a 256x64 original");
    owner.next();
    controller.originalPixelClicked(context, bigOriginal(200, 50), true);
    expect(owner.committed(EditKind::ComparisonMask, {0}) &&
               owner.status == "Erased comparison mask pixel" &&
               document.comparisonMaskPixel(0, 200, 50) == 0,
           "mask erase on a 256x64 original");

    owner.next();
    context.tool = DrawingTool::FilledRectangle;
    const std::vector<Point> rectangle =
        edit::makeFilledRectanglePixels(Point{130, 40}, Point{133, 41});
    controller.strokePreviewed(context, bigOriginal(130, 40), bigOriginal(133, 41), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::OriginalFrame,
                     Resolution::Hd,
                     rectangle,
                     QColor(255, 0, 0, 150)),
           "mask stroke preview is in original coordinates on the original's grid");
    controller.strokeFinished(context, bigOriginal(130, 40), bigOriginal(133, 41), false);
    expect(owner.committed(EditKind::ComparisonMask, {0}) &&
               owner.status == "Painted comparison mask filled rectangle" &&
               document.comparisonMaskPixel(0, 133, 41) == 1 &&
               document.comparisonMaskPixel(0, 134, 41) == 0 &&
               setMaskPixelCount(document, 0) == 1 + 8,
           "mask stroke on a 256x64 original");
    owner.next();
    context.tool = DrawingTool::Line;
    controller.strokeFinished(context, bigOriginal(130, 40), bigOriginal(133, 40), true);
    expect(owner.committed(EditKind::ComparisonMask, {0}) &&
               setMaskPixelCount(document, 0) == 1 + 4,
           "mask erase stroke on a 256x64 original");

    // Frame 1 has a clear mask of its own.
    owner.next();
    context.tool = DrawingTool::FloodFill;
    context.selectedFrames = {1};
    context.currentFrame = 1;
    controller.originalPixelClicked(context, bigOriginal(250, 60), false);
    expect(owner.committed(EditKind::ComparisonMask, {1}) &&
               owner.status == "Flood filled comparison mask" &&
               setMaskPixelCount(document, 1) == 256 * 64,
           "mask flood fill covers 256x64");
    owner.next();
    controller.originalPixelClicked(context, bigOriginal(250, 60), true);
    expect(owner.committed(EditKind::ComparisonMask, {1}) && setMaskPixelCount(document, 1) == 0,
           "mask flood erase covers 256x64");
}

void testSpriteCaptureSelectionOnBigOriginal()
{
    ProjectDocument document = makeBigOriginalDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::ComparisonMask, DrawingTool::Point, {0});
    context.spriteCaptureSelection = true;
    const QColor magenta(255, 0, 255, 110);

    controller.originalPixelClicked(context, bigOriginal(11, 7), false);
    expect(owner.captureChanges == 1 && owner.commits.empty() && !document.isDirty() &&
               samePoints(controller.captureSelectionPixels(0), {Point{11, 7}}),
           "a click marks the clicked pixel of the 256x64 original");
    expect(isOverlay(owner.selectionOverlay,
                     CanvasRegion::OriginalFrame,
                     Resolution::Hd,
                     {Point{11, 7}},
                     magenta),
           "a marked pixel is shown as that one pixel, not as a 2x2 block");
    owner.next();
    controller.originalPixelClicked(context, bigOriginal(10, 6), false);
    expect(owner.captureChanges == 1 &&
               samePoints(controller.captureSelectionPixels(0), {Point{10, 6}, Point{11, 7}}),
           "the neighbouring original pixel is a pixel of its own");
    owner.next();
    controller.originalPixelClicked(context, original(11, 7), false);
    expect(owner.captureChanges == 0 && controller.captureSelectionPixels(0).size() == 2,
           "hits on the SD grid do not belong to a 256x64 original");
    controller.originalPixelClicked(context, bigOriginal(255, 63), false);
    expect(samePoints(controller.captureSelectionPixels(0),
                      {Point{10, 6}, Point{11, 7}, Point{255, 63}}),
           "the last original pixel can be marked");
    controller.originalPixelClicked(context, bigOriginal(254, 62), true);
    expect(controller.captureSelectionPixels(0).size() == 3,
           "erasing a neighbour leaves a marked pixel alone");
    controller.originalPixelClicked(context, bigOriginal(255, 63), true);
    controller.originalPixelClicked(context, bigOriginal(10, 6), true);
    expect(samePoints(controller.captureSelectionPixels(0), {Point{11, 7}}),
           "erase unmarks exactly the clicked pixel");

    owner.next();
    context.tool = DrawingTool::FilledRectangle;
    const std::vector<Point> rectangle =
        edit::makeFilledRectanglePixels(Point{21, 11}, Point{23, 12});
    controller.strokePreviewed(context, bigOriginal(21, 11), bigOriginal(23, 12), false);
    expect(isOverlay(owner.preview,
                     CanvasRegion::OriginalFrame,
                     Resolution::Hd,
                     rectangle,
                     QColor(255, 0, 255, 150)),
           "capture stroke preview shows the original pixels of the stroke");
    owner.preview.reset();
    controller.strokePreviewed(context, original(21, 11), original(22, 12), false);
    expect(!owner.preview, "no capture preview for hits on the SD grid");
    controller.strokeFinished(context, original(21, 11), original(22, 12), false);
    expect(owner.captureChanges == 0, "no capture stroke for hits on the SD grid");
    controller.strokeFinished(context, bigOriginal(21, 11), bigOriginal(23, 12), false);
    expect(owner.captureChanges == 1 &&
               samePoints(controller.captureSelectionPixels(0),
                          {Point{11, 7},
                           Point{21, 11},
                           Point{22, 11},
                           Point{23, 11},
                           Point{21, 12},
                           Point{22, 12},
                           Point{23, 12}}),
           "capture stroke marks the original pixels it touches, odd ones included");

    // What MainWindow does on a frame change.
    owner.selectionOverlay.reset();
    controller.showCaptureSelection(0);
    expect(owner.selectionOverlay && owner.selectionOverlay->resolution == Resolution::Hd &&
               owner.selectionOverlay->pixels.size() == 7,
           "showing the capture selection again keeps the original's grid");
    controller.showCaptureSelection(1);
    expect(!owner.selectionOverlay, "no capture overlay on another frame");

    // The same controller on an ordinary project afterwards: back on the 128x32 grid.
    ProjectDocument ordinary = makeDocument();
    CanvasEditContext ordinaryContext =
        makeContext(ordinary, CanvasEditLayer::ComparisonMask, DrawingTool::Point, {0});
    ordinaryContext.spriteCaptureSelection = true;
    controller.resetCaptureSelection();
    controller.originalPixelClicked(ordinaryContext, original(127, 31), false);
    controller.originalPixelClicked(ordinaryContext, bigOriginal(200, 50), false);
    expect(samePoints(controller.captureSelectionPixels(0), {Point{127, 31}}),
           "the capture selection follows the original size of the document");
}

// Frame 1 has HD; frame 0 has none, which such a project cannot render or edit.
void testFramePixelsOnHdOnlyProject()
{
    ProjectDocument document = makeBigOriginalDocument();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::Point, {1});
    context.drawColor = 0x07e0;
    const CanvasHit hdHit{CanvasRegion::ColorizedFrame, Resolution::Hd, 201, 51};
    controller.colorizedPixelClicked(context, hdHit, false);
    expect(owner.committed(EditKind::FramePixels, {1}, Resolution::Hd) &&
               document.colorizedPixel(1, Resolution::Hd, 201, 51) == 0x07e0,
           "frame pixels of an HD-only project are painted on the HD plane");
    const document::Rgb565Frame sdBefore = document.colorizedFrame(1, Resolution::Sd);
    owner.next();
    context.tool = DrawingTool::FilledRectangle;
    controller.strokeFinished(context,
                              CanvasHit{CanvasRegion::ColorizedFrame, Resolution::Hd, 0, 0},
                              CanvasHit{CanvasRegion::ColorizedFrame, Resolution::Hd, 255, 63},
                              false);
    expect(document.colorizedPixel(1, Resolution::Hd, 255, 63) == 0x07e0 &&
               document.colorizedFrame(1, Resolution::Sd).pixels == sdBefore.pixels,
           "painting the HD plane leaves the stored SD frame untouched");
}

void testOutputsAreOptional()
{
    ProjectDocument document = makeDocument();
    CanvasEditController controller;
    CanvasEditContext context =
        makeContext(document, CanvasEditLayer::FramePixels, DrawingTool::Selection, {0});
    controller.strokePreviewed(context, colorized(1, 1), colorized(3, 3), false);
    controller.strokeFinished(context, colorized(1, 1), colorized(3, 3), false);
    controller.fillSelectionWithPaletteGradient(context);
    controller.toggleSelectionDirectionMode();
    controller.clearSelection(context);
    context.tool = DrawingTool::Point;
    controller.colorizedPixelClicked(context, colorized(1, 1), false);
    context.spriteCaptureSelection = true;
    controller.originalPixelClicked(context, original(1, 1), false);
    controller.clearCaptureSelection();
    expect(!document.isDirty(), "a controller without outputs edits nothing");
}

}  // namespace

void runCanvasEditControllerTests()
{
    testFramePixelPointPickAndFlood();
    testFramePixelStrokesAndPreview();
    testBackgroundImageLayer();
    testComparisonMaskLayer();
    testDynamicMaskLayer();
    testBackgroundMaskLayer();
    testPaletteGradientTool();
    testFreePaletteGradientTwoSteps();
    testCanvasSelection();
    testCanvasSelectionOnBackground();
    testSpriteCaptureSelection();
    testComparisonMaskOnBigOriginal();
    testSpriteCaptureSelectionOnBigOriginal();
    testFramePixelsOnHdOnlyProject();
    testOutputsAreOptional();
}
