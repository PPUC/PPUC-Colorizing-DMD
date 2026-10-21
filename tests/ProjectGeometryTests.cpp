#include "render/ProjectGeometry.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/DrawingTools.h"
#include "edit/EditCommand.h"
#include "render/CanvasLayout.h"

using colorizing::render::CanvasHit;
using colorizing::render::CanvasLayout;
using colorizing::render::CanvasLayoutOptions;
using colorizing::render::CanvasRegion;
using colorizing::render::CanvasRegionLayout;
using colorizing::render::PlaneLayout;
using colorizing::render::Point;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: project geometry: " << message << '\n';
        std::exit(1);
    }
}

bool isPoint(Point point, int x, int y)
{
    return point.x == x && point.y == y;
}

template <typename Call>
bool throwsLogicError(Call call)
{
    try {
        call();
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

bool isSize(Size size, int width, int height)
{
    return size.width == width && size.height == height;
}

void testOrdinaryOriginal()
{
    const ProjectGeometry geometry;
    expect(geometry.layout() == PlaneLayout::Ordinary && geometry == ProjectGeometry(
                                                             PlaneLayout::Ordinary),
           "the default geometry is the ordinary layout");
    expect(isSize(geometry.romFrameSize(), 128, 32), "default original size");
    expect(isSize(geometry.planeSize(Resolution::Sd), 128, 32) &&
               isSize(geometry.planeSize(Resolution::Hd), 256, 64),
           "stored planes of an ordinary project");
    expect(geometry.nativePlane() == Resolution::Sd, "128x32 original is on the SD grid");
    expect(geometry.romFrameDisplayScale() == 2 &&
               geometry.planeDisplayScale(Resolution::Sd) == 2 &&
               geometry.planeDisplayScale(Resolution::Hd) == 1,
           "a 32-row plane is shown doubled, a 64-row plane one to one");
    expect(geometry.isEditable(Resolution::Sd) && geometry.isEditable(Resolution::Hd),
           "an ordinary project has both planes");
    expect(!geometry.original256x64Flag(), "an ordinary project has no 256x64 flag");
    expect(geometry.planeScale(Resolution::Sd) == 1 && geometry.planeScale(Resolution::Hd) == 2,
           "the HD plane is twice the 128x32 original");

    expect(isPoint(geometry.toRomFrame(Resolution::Sd, Point{127, 31}), 127, 31),
           "SD maps one to one");
    expect(isPoint(geometry.toRomFrame(Resolution::Hd, Point{255, 63}), 127, 31) &&
               isPoint(geometry.toRomFrame(Resolution::Hd, Point{11, 6}), 5, 3) &&
               isPoint(geometry.toRomFrame(Resolution::Hd, Point{10, 7}), 5, 3),
           "HD pixels share the original pixel of their 2x2 block");
    expect(isPoint(geometry.toRomFrame(Resolution::Hd, Point{-1, -3}), -1, -2),
           "HD pixels outside the frame stay outside");
}

void testBigOriginal()
{
    const ProjectGeometry geometry(PlaneLayout::NativeExtra);
    expect(isSize(geometry.romFrameSize(), 256, 64), "256x64 original size");
    expect(isSize(geometry.planeSize(Resolution::Sd), 128, 32) &&
               isSize(geometry.planeSize(Resolution::Hd), 256, 64),
           "a 256x64 project stores the same planes as an ordinary one");
    expect(geometry.nativePlane() == Resolution::Hd, "256x64 original is on the HD grid");
    expect(geometry.romFrameDisplayScale() == 1, "256x64 original display scale");
    expect(geometry.isEditable(Resolution::Hd) && !geometry.isEditable(Resolution::Sd),
           "a 256x64 project has the HD plane only");
    expect(geometry.original256x64Flag(), "a 256x64 project carries the header flag");
    expect(geometry.planeScale(Resolution::Hd) == 1, "the HD plane is the 256x64 original's size");

    expect(isPoint(geometry.toRomFrame(Resolution::Hd, Point{0, 0}), 0, 0) &&
               isPoint(geometry.toRomFrame(Resolution::Hd, Point{11, 7}), 11, 7) &&
               isPoint(geometry.toRomFrame(Resolution::Hd, Point{255, 63}), 255, 63),
           "HD maps one to one");
    expect(throwsLogicError([&]() { (void)geometry.toRomFrame(Resolution::Sd, Point{5, 3}); }) &&
               throwsLogicError([&]() { (void)geometry.planeScale(Resolution::Sd); }),
           "nothing maps a 128x32 plane onto a 256x64 original");
}

void testNativeOriginal()
{
    const ProjectGeometry geometry(PlaneLayout::NativeOriginal);
    expect(isSize(geometry.romFrameSize(), 192, 64), "192x64 original size");
    expect(isSize(geometry.planeSize(Resolution::Sd), 192, 64) &&
               isSize(geometry.planeSize(Resolution::Hd), 96, 32),
           "a 192x64 project stores a 192x64 original plane and a 96x32 extra plane");
    expect(geometry.nativePlane() == Resolution::Sd,
           "the 192x64 original is the stored original plane");
    expect(geometry.isEditable(Resolution::Sd) && !geometry.isEditable(Resolution::Hd) &&
               geometry.discardsExtraPlane(),
           "the 96x32 extra plane is discarded");
    expect(!ProjectGeometry().discardsExtraPlane() &&
               !ProjectGeometry(PlaneLayout::NativeExtra).discardsExtraPlane(),
           "the other layouts keep their second plane");
    expect(!geometry.original256x64Flag(), "a 192x64 project has no 256x64 flag");
    expect(geometry.planeScale(Resolution::Sd) == 1, "the original plane is the ROM frame");
    expect(geometry.romFrameDisplayScale() == 1 &&
               geometry.planeDisplayScale(Resolution::Sd) == 1,
           "a 64-row plane is shown one to one");
    expect(isPoint(geometry.toRomFrame(Resolution::Sd, Point{191, 63}), 191, 63) &&
               isPoint(geometry.toRomFrame(Resolution::Sd, Point{129, 33}), 129, 33),
           "the original plane maps one to one");
    expect(throwsLogicError([&]() { (void)geometry.toRomFrame(Resolution::Hd, Point{5, 3}); }) &&
               throwsLogicError([&]() { (void)geometry.planeScale(Resolution::Hd); }),
           "nothing maps the 96x32 plane onto the 192x64 original");
    expect(geometry != ProjectGeometry() && geometry != ProjectGeometry(PlaneLayout::NativeExtra),
           "the layouts differ");
}

void testHeaderGeometries()
{
    const auto fromHeader = [](Size original, Size extra, bool flag) {
        return ProjectGeometry::fromStoredPlanes(original, extra, flag);
    };
    const std::optional<ProjectGeometry> ordinary = fromHeader({128, 32}, {256, 64}, false);
    const std::optional<ProjectGeometry> nativeExtra = fromHeader({128, 32}, {256, 64}, true);
    const std::optional<ProjectGeometry> nativeOriginal = fromHeader({192, 64}, {96, 32}, false);
    expect(ordinary && ordinary->layout() == PlaneLayout::Ordinary, "128x32 + 256x64");
    expect(nativeExtra && nativeExtra->layout() == PlaneLayout::NativeExtra,
           "128x32 + 256x64 with the 256x64 flag");
    expect(nativeOriginal && nativeOriginal->layout() == PlaneLayout::NativeOriginal,
           "192x64 + 96x32");

    expect(!fromHeader({192, 64}, {96, 32}, true), "192x64 with the 256x64 flag is refused");
    expect(!fromHeader({192, 64}, {384, 128}, false), "192x64 with a doubled plane is refused");
    expect(!fromHeader({256, 64}, {128, 32}, false), "256x64 + 128x32 is refused");
    expect(!fromHeader({128, 16}, {256, 32}, false), "128x16 is refused");
    expect(!fromHeader({192, 32}, {384, 64}, false), "192x32 is refused");
    expect(!fromHeader({128, 32}, {128, 32}, false), "an extra plane of the same size is refused");
    expect(!fromHeader({}, {}, false), "empty sizes are refused");

    std::string message;
    expect(!ProjectGeometry::fromStoredPlanes({192, 32}, {384, 64}, false, &message) &&
               message.find("192x32") != std::string::npos &&
               message.find("384x64") != std::string::npos &&
               message.find("192x64 with 96x32") != std::string::npos,
           "the refusal names the sizes found and the supported ones");
}

void testLayoutOfNativeOriginal()
{
    CanvasLayoutOptions options;
    options.geometry = ProjectGeometry(PlaneLayout::NativeOriginal);
    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);
    expect(layout.regions().size() == 2, "colorized and original region");
    expect(layout.displaySize().width == 192 && layout.displaySize().height == 64 + 8 + 64,
           "both regions are 192x64 display pixels at zoom 1");

    const std::optional<CanvasRegionLayout> colorized = layout.region(CanvasRegion::ColorizedFrame);
    const std::optional<CanvasRegionLayout> original = layout.region(CanvasRegion::OriginalFrame);
    expect(colorized && original, "both regions exist");
    expect(isSize(colorized->logicalSize, 192, 64) && colorized->displayScale == 1 &&
               colorized->resolution == Resolution::Sd,
           "the edited plane is the 192x64 original plane");
    expect(isSize(original->logicalSize, 192, 64) && original->displayScale == 1 &&
               original->resolution == Resolution::Sd,
           "the original region is on the same grid");

    const std::optional<CanvasHit> corner = layout.hitTest(Point{191, 63});
    expect(corner && corner->region == CanvasRegion::ColorizedFrame &&
               corner->resolution == Resolution::Sd && corner->x == 191 && corner->y == 63,
           "bottom right pixel of the colorized plane");
    expect(!layout.hitTest(Point{192, 10}), "nothing right of column 191");
    const std::optional<CanvasHit> originalCorner = layout.hitTest(Point{191, 72 + 63});
    expect(originalCorner && originalCorner->region == CanvasRegion::OriginalFrame &&
               originalCorner->x == 191 && originalCorner->y == 63,
           "bottom right pixel of the original");
    expect(!layout.hitTest(Point{100, 72 + 64}), "nothing below the original");
}

void testLayoutShowsBigOriginalAtSdDisplaySize()
{
    CanvasLayoutOptions ordinaryOptions;
    CanvasLayoutOptions options;
    options.geometry = ProjectGeometry(PlaneLayout::NativeExtra);
    const CanvasLayout ordinary = CanvasLayout::buildFrameLayout(ordinaryOptions);
    const CanvasLayout layout = CanvasLayout::buildFrameLayout(options);

    const std::optional<CanvasRegionLayout> region = layout.region(CanvasRegion::OriginalFrame);
    const std::optional<CanvasRegionLayout> ordinaryRegion =
        ordinary.region(CanvasRegion::OriginalFrame);
    expect(region && ordinaryRegion, "both layouts have an original region");
    expect(region->logicalSize.width == 256 && region->logicalSize.height == 64 &&
               region->displayScale == 1 && region->resolution == Resolution::Hd,
           "256x64 original region");
    expect(ordinaryRegion->logicalSize.width == 128 && ordinaryRegion->logicalSize.height == 32 &&
               ordinaryRegion->displayScale == 2 && ordinaryRegion->resolution == Resolution::Sd,
           "128x32 original region is unchanged");
    expect(region->displayRect.x == ordinaryRegion->displayRect.x &&
               region->displayRect.y == ordinaryRegion->displayRect.y &&
               region->displayRect.width == ordinaryRegion->displayRect.width &&
               region->displayRect.height == ordinaryRegion->displayRect.height &&
               layout.displaySize().width == ordinary.displaySize().width &&
               layout.displaySize().height == ordinary.displaySize().height,
           "both originals are displayed at the same place and size");

    const std::optional<CanvasHit> hit = layout.hitTest(Point{255, 72 + 63});
    expect(hit && hit->region == CanvasRegion::OriginalFrame && hit->resolution == Resolution::Hd &&
               hit->x == 255 && hit->y == 63,
           "hits on a 256x64 original are original coordinates");
    const std::optional<CanvasHit> ordinaryHit = ordinary.hitTest(Point{255, 72 + 63});
    expect(ordinaryHit && ordinaryHit->resolution == Resolution::Sd && ordinaryHit->x == 127 &&
               ordinaryHit->y == 31,
           "hits on a 128x32 original are unchanged");
}

document::ProjectDocument makeBigOriginalDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "big_original";
    snapshot.geometry = ProjectGeometry(PlaneLayout::NativeExtra);
    snapshot.comparisonMasks.push_back(document::makeIndexedFrame(kHdFrameSize, 0));
    document::IndexedFrame original = document::makeIndexedFrame(kHdFrameSize, 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 256; ++x) {
            original.pixels[static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)] =
                static_cast<uint8_t>((x + 3 * y) % 16);
        }
    }
    snapshot.frames.push_back(document::ProjectFrameSnapshot{
        original,
        document::makeRgb565Frame(kSdFrameSize, 0),
        document::makeRgb565Frame(kHdFrameSize, 0),
        0,
        0,
    });
    std::string error;
    expect(document::isValidProjectSnapshot(snapshot, &error), "fixture is valid: " + error);
    return document::ProjectDocument::fromSnapshot(snapshot);
}

void testComparisonMaskEditsCoverBigOriginal()
{
    document::ProjectDocument document = makeBigOriginalDocument();
    document.markClean();
    const edit::EditLayer layer{edit::EditKind::ComparisonMask, 0, Resolution::Sd};
    const std::vector<edit::MaskPixelChange> changes =
        edit::makeMaskChanges(document, layer, {{200, 50}, {255, 63}, {0, 0}}, 1);
    expect(changes.size() == 3, "mask changes beyond 128x32");
    const edit::MaskEditCommand command(layer.kind, layer.index, layer.resolution, changes);
    command.apply(document);
    expect(document.comparisonMaskPixel(0, 200, 50) == 1 &&
               document.comparisonMaskPixel(0, 255, 63) == 1 &&
               document.comparisonMaskPixel(0, 0, 0) == 1 &&
               document.comparisonMaskPixel(0, 100, 25) == 0,
           "mask command edits 256x64 coordinates");
    command.revert(document);
    expect(document.comparisonMaskPixel(0, 200, 50) == 0 &&
               document.comparisonMaskPixel(0, 255, 63) == 0 &&
               document.comparisonMaskPixel(0, 0, 0) == 0,
           "mask command reverts 256x64 coordinates");

    const std::vector<Point> filled =
        edit::floodFillMatching(document.comparisonMask(0).pixels,
                                document.comparisonMask(0).size,
                                Point{130, 40},
                                uint8_t{0});
    expect(filled.size() == 256 * 64, "flood fill covers the whole 256x64 mask");
}

document::SpriteRecord makeEmptySprite()
{
    const Size cell{static_cast<int>(document::kMaxSpriteWidth),
                    static_cast<int>(document::kMaxSpriteHeight)};
    document::SpriteRecord sprite;
    sprite.originalSd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedSd = document::makeRgb565Frame(cell, 0x5555);
    sprite.maskHd = document::makeIndexedFrame(cell, 255);
    sprite.colorizedHd = document::makeRgb565Frame(cell, 0);
    sprite.detectionDwords.assign(document::kMaxSpriteDetectAreas, 0xffffffff);
    sprite.detectionDwordPositions.assign(document::kMaxSpriteDetectAreas, 0xffff);
    sprite.detectionAreas.assign(document::kMaxSpriteDetectAreas, document::SpriteDetectionArea{});
    return sprite;
}

// Frame 0: no HD frame, background 0 (no HD), sprite 0 (no HD) in slot 3 and again in slot 5.
// Frame 1: HD frame, nothing assigned. The original is (x + 3y) % 16 in both.
document::ProjectDocument makeHdOnlyDocument()
{
    document::ProjectSnapshot snapshot;
    snapshot.colorCount = 16;
    snapshot.romName = "hd_only";
    snapshot.geometry = ProjectGeometry(PlaneLayout::NativeExtra);
    document::IndexedFrame original = document::makeIndexedFrame(kHdFrameSize, 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 256; ++x) {
            original.pixels[static_cast<std::size_t>(y) * 256 + static_cast<std::size_t>(x)] =
                static_cast<uint8_t>((x + 3 * y) % 16);
        }
    }
    snapshot.backgrounds.push_back(
        document::BackgroundRecord{document::makeRgb565Frame(kSdFrameSize, 0x0410)});
    snapshot.sprites.push_back(makeEmptySprite());
    snapshot.sprites[0].originalSd.pixels[0] = 5;
    snapshot.sprites[0].originalSd.pixels[1] = 0;
    snapshot.sprites[0].originalSd.pixels[256 + 1] = 15;
    snapshot.sprites.push_back(makeEmptySprite());
    for (int i = 0; i < 2; ++i) {
        snapshot.frames.push_back(document::ProjectFrameSnapshot{
            original,
            document::makeRgb565Frame(kSdFrameSize, 0x2104),
            std::nullopt,
            0,
            255,
        });
        snapshot.frames.back().spriteReferences.assign(document::kLegacySpritesPerFrame,
                                                       document::FrameSpriteReference{});
    }
    snapshot.frames[0].backgroundData = document::FrameBackgroundData{
        0,
        document::makeIndexedFrame(kSdFrameSize, 1),
        document::makeIndexedFrame(kHdFrameSize, 1),
    };
    snapshot.frames[0].dynamicData = document::FrameDynamicData{
        document::makeIndexedFrame(kSdFrameSize, 3),
        document::makeIndexedFrame(kHdFrameSize, 255),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0x0101),
        std::vector<uint16_t>(document::kMaxDynamicSetsPerFrame * 16, 0x0202),
    };
    snapshot.frames[0].spriteReferences[3].spriteId = 0;
    snapshot.frames[0].spriteReferences[5].spriteId = 0;
    snapshot.frames[1].colorizedHd = document::makeRgb565Frame(kHdFrameSize, 0x4208);
    std::string error;
    expect(document::isValidProjectSnapshot(snapshot, &error), "fixture is valid: " + error);
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

void testHdPresenceOfHdOnlyProject()
{
    document::ProjectDocument document = makeHdOnlyDocument();
    expect(document.rendersHdPlaneOnly(), "a 256x64 project renders its HD plane only");
    const document::ProjectDocument ordinary =
        document::ProjectDocument::createEmpty({"ordinary", 1, false});
    expect(!ordinary.rendersHdPlaneOnly() && !ordinary.frameHdGaps(0).any(),
           "an ordinary project has an SD plane and no HD gaps, with or without HD");

    const document::FrameHdGaps gaps = document.frameHdGaps(0);
    expect(gaps.frame && gaps.background && gaps.sprites == std::vector<uint16_t>{0} && gaps.any(),
           "frame, background and sprite without HD are reported, a sprite once");
    expect(!document.frameHdGaps(1).any(), "a frame with HD and nothing assigned is complete");

    expect(throwsLogicError([&]() { document.removeHdFrame(1); }) && document.hasHdFrame(1) &&
               throwsLogicError([&]() { document.upscaleBackgroundMaskToHd(0); }) &&
               !document.isDirty(),
           "HD cannot be removed and nothing is upscaled from the unused SD data");

    // Create HD for the frame and its background, as one undoable step.
    document::FrameHdParts parts;
    parts.backgroundImage = true;
    const std::unique_ptr<edit::EditCommand> command = edit::createFrameHd(document, 0, parts);
    expect(command != nullptr && document.hasHdFrame(0), "Create HD on a 256x64 project");
    const std::vector<uint16_t> plain = document::plainOriginalColors(16);
    const document::Rgb565Frame& created = document.colorizedFrame(0, Resolution::Hd);
    bool isPlainOriginal = created.size.width == 256 && created.size.height == 64;
    for (std::size_t i = 0; i < created.pixels.size(); ++i) {
        isPlainOriginal = isPlainOriginal &&
                          created.pixels[i] == plain[document.originalFrame(0).pixels[i]];
    }
    expect(isPlainOriginal && plain[15] != plain[0],
           "the created HD frame is the native original in its plain colours, pixel for pixel");
    const document::Rgb565Frame& sd = document.colorizedFrame(0, Resolution::Sd);
    expect(sd.size.width == 128 && sd.pixels.front() == 0x2104 && sd.pixels.back() == 0x2104,
           "the stored SD frame is not touched");
    expect(document.dynamicData(0)->maskHd.pixels.front() == 255 &&
               document.dynamicData(0)->colorsHd.front() == 0x0202 &&
               document.dynamicData(0)->maskSd.pixels.front() == 3,
           "dynamic data is neither upscaled nor reset");
    const document::BackgroundRecord& background = document.background(0);
    expect(background.colorizedHd && background.colorizedHd->size.width == 256 &&
               background.colorizedHd->pixels.front() == 0 &&
               background.colorizedHd->pixels.back() == 0 &&
               background.colorizedSd.pixels.front() == 0x0410,
           "the background gets a black HD image; its SD image is kept, not upscaled");
    const document::FrameHdGaps afterFrame = document.frameHdGaps(0);
    expect(!afterFrame.frame && !afterFrame.background &&
               afterFrame.sprites == std::vector<uint16_t>{0},
           "only the sprite is still missing");

    command->revert(document);
    expect(!document.hasHdFrame(0) && !document.background(0).colorizedHd,
           "undoing Create HD removes what it created");
    command->apply(document);
    expect(document.hasHdFrame(0) && document.background(0).colorizedHd, "redo");
    expect(edit::createFrameHd(document, 0, parts) == nullptr &&
               edit::createFrameHd(document, 1, {}) == nullptr,
           "nothing to create when the HD data is there");
    document.setBackgroundPixel(0, Resolution::Hd, 7, 7, 0x1357);
    document.upscaleAssignedBackgroundToHd(0);
    expect(document.backgroundPixel(0, Resolution::Hd, 7, 7) == 0x1357,
           "an existing HD background is never replaced");

    // Create HD for the sprite, from the sprite original.
    const std::unique_ptr<edit::EditCommand> spriteCommand = edit::createSpriteHd(document, 0);
    const document::SpriteRecord& sprite = document.sprite(0);
    expect(spriteCommand != nullptr && sprite.hasHd &&
               sprite.maskHd.pixels == sprite.originalSd.pixels,
           "the HD sprite mask is the shape of the sprite original, one to one");
    expect(sprite.colorizedHd.pixels[0] == plain[5] && sprite.colorizedHd.pixels[1] == plain[0] &&
               sprite.colorizedHd.pixels[256 + 1] == plain[15] &&
               sprite.colorizedHd.pixels[2] == 0 && sprite.colorizedSd.pixels[0] == 0x5555,
           "the HD sprite colours are the plain colours of the sprite original, not its SD "
           "colours");
    expect(!document.frameHdGaps(0).any(), "the frame is complete");
    expect(throwsLogicError([&]() { document.removeHdSprite(0); }) && document.hasHdSprite(0),
           "the HD sprite cannot be removed");
    spriteCommand->revert(document);
    expect(!document.hasHdSprite(0), "undoing Create HD Sprite");
}

void testNativeSpriteCapture()
{
    document::ProjectDocument document = makeHdOnlyDocument();
    document::Rgb565Frame colours = document::makeRgb565Frame(kHdFrameSize, 0);
    for (std::size_t i = 0; i < colours.pixels.size(); ++i) {
        colours.pixels[i] = static_cast<uint16_t>(i + 1);
    }

    // 5x3 from the odd position 201,41, beyond the 128x32 range in both directions.
    document.replaceSpriteFromFrame(1, document.originalFrame(1), colours, 201, 41, 5, 3);
    const document::SpriteRecord& sprite = document.sprite(1);
    bool aligned = sprite.hasHd;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            const std::size_t spriteIndex = static_cast<std::size_t>(y) * 256 +
                                            static_cast<std::size_t>(x);
            const std::size_t frameIndex = static_cast<std::size_t>(41 + y) * 256 +
                                           static_cast<std::size_t>(201 + x);
            const uint8_t value = static_cast<uint8_t>((201 + x + 3 * (41 + y)) % 16);
            // Unlit pixels are not part of a sprite captured from bounds.
            aligned = aligned &&
                      sprite.originalSd.pixels[spriteIndex] == (value == 0 ? 255 : value) &&
                      sprite.maskHd.pixels[spriteIndex] == sprite.originalSd.pixels[spriteIndex] &&
                      sprite.colorizedHd.pixels[spriteIndex] ==
                          (value == 0 ? 0 : colours.pixels[frameIndex]);
        }
    }
    expect(aligned, "captured original, HD mask and HD colours are the same native pixels");
    expect(sprite.originalSd.pixels[5] == 255 && sprite.originalSd.pixels[3 * 256] == 255,
           "nothing outside the bounds is captured");
    expect(sprite.colorizedSd.pixels.size() == 256 * 64 && sprite.colorizedSd.pixels[0] == 0 &&
               sprite.dynamicMaskSd.pixels.size() == 256 * 64 &&
               sprite.dynamicMaskSd.pixels[0] == 255 &&
               sprite.dynamicColorsSd.size() == document::kMaxDynamicSetsPerSprite * 16 &&
               sprite.dynamicColorsSd[0] == 0,
           "unused SD sprite data: colours 0, dynamic mask 255, dynamic colours 0, usual sizes");

    document.setSpriteDetectionArea(1, 0, document::SpriteDetectionArea{0, 0, 5, 3});
    expect(document.sprite(1).detectionDwords[0] != 0xffffffff &&
               document.sprite(1).detectionDwordPositions[0] != 0xffff,
           "detection dwords are calculated from the native sprite original");

    document.replaceSpriteFromFrameSelection(1,
                                             document.originalFrame(1),
                                             colours,
                                             {Point{255, 63}, Point{253, 61}});
    const document::SpriteRecord& selected = document.sprite(1);
    expect(selected.originalSd.pixels[2 * 256 + 2] == (255 + 3 * 63) % 16 &&
               selected.originalSd.pixels[0] == (253 + 3 * 61) % 16 &&
               selected.maskHd.pixels == selected.originalSd.pixels &&
               selected.colorizedHd.pixels[2 * 256 + 2] == colours.pixels[63 * 256 + 255] &&
               selected.colorizedHd.pixels[0] == colours.pixels[61 * 256 + 253] &&
               selected.originalSd.pixels[1] == 255 && selected.hasHd,
           "a selection is captured pixel for pixel, the last native pixel included");

    const document::IndexedFrame sdOriginal = document::makeIndexedFrame(kSdFrameSize, 1);
    bool refused = false;
    try {
        document.replaceSpriteFromFrame(1, sdOriginal, colours, 0, 0, 4, 4);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, "original and colours of a capture must be the same plane size");
}

}  // namespace

void runProjectGeometryTests()
{
    testOrdinaryOriginal();
    testBigOriginal();
    testNativeOriginal();
    testHeaderGeometries();
    testLayoutOfNativeOriginal();
    testLayoutShowsBigOriginalAtSdDisplaySize();
    testComparisonMaskEditsCoverBigOriginal();
    testHdPresenceOfHdOnlyProject();
    testNativeSpriteCapture();
}
