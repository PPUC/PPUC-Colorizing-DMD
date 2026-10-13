// The libserum-backed runtime preview: the subset a preview loads, and what libserum renders
// from it for each of the three plane layouts.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectSerializer.h"
#include "render/ProjectGeometry.h"
#include "runtime/RuntimePreview.h"
#include "RuntimeFixtures.h"

using colorizing::document::BackgroundRecord;
using colorizing::document::FrameBackgroundData;
using colorizing::document::FrameDynamicData;
using colorizing::document::FrameSpriteReference;
using colorizing::document::IndexedFrame;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::Rgb565Frame;
using colorizing::document::SnapshotSubsetMap;
using colorizing::document::SpriteDetectionArea;
using colorizing::document::SpriteRecord;
using colorizing::document::kNoTriggerId;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::render::PlaneLayout;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;
using colorizing::runtime::PlaneRequest;
using colorizing::runtime::RuntimeFrame;
using colorizing::runtime::RuntimePlane;
using colorizing::runtime::RuntimePreview;
using colorizing::runtime::RuntimeSubset;
using colorizing::runtime::buildRuntimeSubset;
using colorizing::runtime::kNoDocumentFrame;
using namespace colorizing::runtime_fixtures;

namespace {


std::string g_context;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: runtime preview: " << g_context << ": " << message << '\n';
        std::exit(1);
    }
}

const char* layoutName(PlaneLayout layout)
{
    switch (layout) {
        case PlaneLayout::Ordinary:
            return "128x32";
        case PlaneLayout::NativeExtra:
            return "256x64 original";
        case PlaneLayout::NativeOriginal:
            break;
    }
    return "192x64 original";
}

const RuntimePlane& nativePlane(const RuntimeFrame& frame, const ProjectGeometry& geometry)
{
    expect(frame.ok(), "libserum reported: " + frame.error);
    expect(frame.identified, "the frame is identified");
    const RuntimePlane* plane = frame.plane(nativeRequest(geometry));
    expect(plane != nullptr, "the native plane is returned");
    expect(plane->image.size == geometry.planeSize(geometry.nativePlane()),
           "the native plane has the size of the ROM frame");
    return *plane;
}

template <typename Expected>
void expectPlane(const RuntimePlane& plane, Expected expected, const std::string& what)
{
    std::size_t different = 0;
    std::ostringstream first;
    for (int y = 0; y < plane.image.size.height; ++y) {
        for (int x = 0; x < plane.image.size.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * plane.image.size.width + x;
            const uint16_t wanted = expected(x, y, index);
            if (plane.image.pixels[index] != wanted && different++ == 0) {
                first << " first at (" << x << "," << y << "): got 0x" << std::hex
                      << plane.image.pixels[index] << ", want 0x" << wanted;
            }
        }
    }
    expect(different == 0,
           what + ": " + std::to_string(different) + " pixels differ," + first.str());
}

RuntimeFrame renderNative(const ProjectDocument& document, std::size_t frame)
{
    return RuntimePreview::instance().renderFrame(document, frame,
                                                  nativeRequest(document.geometry()));
}

void testStaticFrame(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 3);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 1);
    const Rgb565Frame& colors = nativeColors(snapshot, 1);
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int, int, std::size_t index) { return colors.pixels[index]; },
                "a static frame shows its own colors");
    expect(frame.frameIndex == std::size_t{1}, "the frame index is the document's");
    expect(frame.triggerId == kNoTriggerId, "no trigger is reported");
    expect(frame.nextRotationMs == 0, "nothing rotates");
    expect(!RuntimePreview::instance().isOpen(), "a still leaves nothing loaded");
    if (layout != PlaneLayout::Ordinary) {
        expect(!frame.rows32, "a project with one plane has no 32-row output");
    }
}

void testBackground(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 2);
    assignBackground(snapshot, 1, addBackground(snapshot));
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 1);
    const Rgb565Frame& colors = nativeColors(snapshot, 1);
    const Rgb565Frame& background = nativeBackground(snapshot, 0);
    const IndexedFrame& original = snapshot.frames[1].originalSd;
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int, int, std::size_t index) {
                    return original.pixels[index] == 0 ? background.pixels[index]
                                                       : colors.pixels[index];
                },
                "the background shows where the original is 0");
}

void testDynamicColors(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 2);
    addDynamicData(snapshot, 0);
    IndexedFrame& mask = nativeDynamicMask(snapshot, 0);
    const int width = mask.size.width;
    for (int y = 4; y < 20; ++y) {
        for (int x = 8; x < 60; ++x) {
            mask.pixels[static_cast<std::size_t>(y) * width + x] =
                static_cast<uint8_t>(x < 30 ? 2 : 5);
        }
    }
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 0);
    const Rgb565Frame& colors = nativeColors(snapshot, 0);
    const IndexedFrame& original = snapshot.frames[0].originalSd;
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int, int, std::size_t index) {
                    return mask.pixels[index] == 255
                               ? colors.pixels[index]
                               : dynamicColor(mask.pixels[index], original.pixels[index]);
                },
                "a dynamic zone takes its set's color for the original's shade");
}

void testDynamicShadows(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 2);
    makeDark(snapshot, 0);
    const int width = snapshot.frames[0].originalSd.size.width;
    const auto at = [width](int x, int y) { return static_cast<std::size_t>(y) * width + x; };
    snapshot.frames[0].originalSd.pixels[at(10, 5)] = 2;
    snapshot.frames[0].originalSd.pixels[at(40, 9)] = 1;
    snapshot.frames[0].originalSd.pixels[at(41, 9)] = 3;
    addDynamicData(snapshot, 0);
    IndexedFrame& mask = nativeDynamicMask(snapshot, 0);
    mask.pixels[at(10, 5)] = 1;
    mask.pixels[at(40, 9)] = 3;
    mask.pixels[at(41, 9)] = 3;
    // Set 1 casts to the right and downwards, set 3 to the left, right and upwards.
    setNativeShadow(snapshot, 0, 1, 0x08 | 0x20);
    setNativeShadow(snapshot, 0, 3, 0x80 | 0x08 | 0x02);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 0);
    const Rgb565Frame& colors = nativeColors(snapshot, 0);
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int x, int y, std::size_t index) {
                    if (index == at(10, 5)) return dynamicColor(1, 2);
                    if (index == at(40, 9)) return dynamicColor(3, 1);
                    if (index == at(41, 9)) return dynamicColor(3, 3);
                    const bool shadow = (x == 11 && y == 5) || (x == 10 && y == 6) ||
                                        (x == 39 && y == 9) || (x == 42 && y == 9) ||
                                        (x == 40 && y == 8) || (x == 41 && y == 8);
                    return shadow ? kShadowColor : colors.pixels[index];
                },
                "a lit dynamic pixel casts its set's shadow, never onto lit content");
}

uint16_t expectedSpritePixel(const ProjectSnapshot& snapshot, std::size_t frame,
                             std::size_t sprite, int sx, int sy, int x, int y, std::size_t index,
                             bool dynamicPixel)
{
    const Rgb565Frame& colors =
        extraIsNative(snapshot.geometry) ? *snapshot.frames[frame].colorizedHd
                                         : snapshot.frames[frame].colorizedSd;
    const int px = x - sx;
    const int py = y - sy;
    if (px < 0 || py < 0 || px >= kSpriteWidth || py >= kSpriteHeight ||
        kSpritePixels[py][px] == kT) {
        return colors.pixels[index];
    }
    if (dynamicPixel && px == 2 && py == 1) {
        return spriteDynamicColor(sprite, snapshot.frames[frame].originalSd.pixels[index]);
    }
    return spriteColor(sprite, px, py);
}

void testSpriteWithExactBounds(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 2);
    const std::size_t sprite = addSprite(snapshot);
    makeDark(snapshot, 1);
    const int sx = 61;
    const int sy = 21;
    placeSprite(snapshot, 1, sx, sy);
    // The search area is exactly the sprite: its last column and row are maxX and maxY.
    setSpriteSlot(snapshot, 1, 0, sprite, sx, sy, sx + kSpriteWidth - 1, sy + kSpriteHeight - 1);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 1);
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int x, int y, std::size_t index) {
                    return expectedSpritePixel(snapshot, 1, sprite, sx, sy, x, y, index, false);
                },
                "a sprite whose search area fits it exactly is found and drawn");
}

void testSpriteWithDynamicContent(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 3);
    // Two sprites, so that the second one's number is past a one-frame subset.
    addSprite(snapshot);
    const std::size_t sprite = addSprite(snapshot);
    const bool extra = extraIsNative(snapshot.geometry);
    for (std::size_t id = 0; id < 2; ++id) {
        SpriteRecord& record = snapshot.sprites[id];
        (extra ? record.dynamicMaskHd : record.dynamicMaskSd).pixels[1 * 256 + 2] = 0;
        std::vector<uint16_t>& colors = extra ? record.dynamicColorsHd : record.dynamicColorsSd;
        for (uint8_t value = 0; value < kColorCount; ++value) {
            colors[value] = spriteDynamicColor(id, value);
        }
    }
    makeDark(snapshot, 0);
    placeSprite(snapshot, 0, 20, 10);
    placeSprite(snapshot, 0, 70, 12);
    setSpriteSlot(snapshot, 0, 0, 0, 10, 5, 40, 25);
    setSpriteSlot(snapshot, 0, 1, sprite, 60, 5, 100, 25);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 0);
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int x, int y, std::size_t index) {
                    return x < 50 ? expectedSpritePixel(snapshot, 0, 0, 20, 10, x, y, index, true)
                                  : expectedSpritePixel(snapshot, 0, sprite, 70, 12, x, y, index,
                                                        true);
                },
                "a dynamic sprite pixel takes the sprite's set color for the original's shade");
}

void testSpriteShapeMode(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 2);
    const std::size_t sprite = addShapeSprite(snapshot);
    makeDark(snapshot, 0);
    const int sx = 80;
    const int sy = 14;
    placeShapeSprite(snapshot, 0, sx, sy);
    setSpriteSlot(snapshot, 0, 0, sprite, sx - 6, sy - 4, sx + 20, sy + 10);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame frame = renderNative(document, 0);
    const Rgb565Frame& colors = nativeColors(snapshot, 0);
    expectPlane(nativePlane(frame, snapshot.geometry),
                [&](int x, int y, std::size_t index) {
                    const int px = x - sx;
                    const int py = y - sy;
                    if (px < 0 || py < 0 || px >= kShapeWidth || py >= kShapeHeight) {
                        return colors.pixels[index];
                    }
                    return spriteColor(sprite, px, py);
                },
                "a shape-mode sprite is found whatever the lit shades are");
}

void testColorRotation(PlaneLayout layout)
{
    constexpr uint16_t kFirst = 0x1234;
    constexpr uint16_t kSecond = 0x4321;
    constexpr uint16_t kDelayMs = 40;
    ProjectSnapshot snapshot = makeProject(layout, 2);
    Rgb565Frame& colors = nativeColors(snapshot, 0);
    std::fill(colors.pixels.begin(), colors.pixels.end(), kFirst);
    colors.pixels[7] = 0x0666;
    std::vector<uint16_t> rotations(4 * 64, 0);
    rotations[0] = 2;
    rotations[1] = kDelayMs;
    rotations[2] = kFirst;
    rotations[3] = kSecond;
    (extraIsNative(snapshot.geometry) ? snapshot.frames[0].colorRotationsHd
                                      : snapshot.frames[0].colorRotationsSd) = rotations;
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const auto expectColors = [&](const RuntimeFrame& frame, uint16_t rotating,
                                  const std::string& what) {
        expectPlane(nativePlane(frame, snapshot.geometry),
                    [&](int, int, std::size_t index) {
                        return index == 7 ? uint16_t{0x0666} : rotating;
                    },
                    what);
    };

    const RuntimeFrame still = renderNative(document, 0);
    expectColors(still, kFirst, "a still shows the rotation at its first step");
    expect(still.nextRotationMs > 0 && still.nextRotationMs <= kDelayMs,
           "a still says when the rotation is due");
    const RuntimePlane& plane = nativePlane(still, snapshot.geometry);
    expect(plane.rotates() && plane.rotations[0].length == 2 &&
               plane.rotations[0].delayMs == kDelayMs && plane.rotations[1].length == 0,
           "the plane carries its rotation slots");

    RuntimePreview& preview = RuntimePreview::instance();
    std::string error;
    expect(preview.openSequence(document, {0, 1}, nativeRequest(snapshot.geometry), &error),
           "the sequence opens: " + error);
    expectColors(preview.renderSequenceFrame(0), kFirst, "the sequence starts at the first step");
    std::this_thread::sleep_for(std::chrono::milliseconds(kDelayMs + 20));
    RuntimeFrame rotated = preview.rotate();
    expect(!rotated.unchanged, "a rotation step is reported once its delay has passed");
    expectColors(rotated, kSecond, "the rotating color moved one step, the others stayed");
    expect(rotated.nextRotationMs > 0 && rotated.nextRotationMs <= kDelayMs,
           "the next step is announced");
    std::this_thread::sleep_for(std::chrono::milliseconds(kDelayMs + 20));
    rotated = preview.rotate();
    expectColors(rotated, kFirst, "the rotation wraps at its length");
    // Another frame, and back: the rotation starts over.
    expect(preview.renderSequenceFrame(1).identified, "the other frame is identified");
    expectColors(preview.renderSequenceFrame(0), kFirst, "a frame shown again starts over");
    preview.close();
    expect(!preview.isOpen(), "closing releases the colorization");
}

// Ordinary projects only: what a 64-row display gets.
void testRows64OfOrdinaryProject()
{
    ProjectSnapshot snapshot = makeProject(PlaneLayout::Ordinary, 3);
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    snapshot.frames[1].colorizedHd = makeRgb565Frame(hd);
    for (std::size_t i = 0; i < pixels(hd); ++i) {
        snapshot.frames[1].colorizedHd->pixels[i] = colorHd(1, i);
    }
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    RuntimePreview& preview = RuntimePreview::instance();

    // No HD frame: libserum upscales the 128x32 picture. Every pixel of this one has its
    // own color, so nothing is rounded but the frame's corners, which meet the black outside.
    RuntimeFrame frame = preview.renderFrame(document, 0, PlaneRequest::Rows64);
    expect(frame.ok() && frame.identified, "the frame without HD is identified");
    expect(!frame.rows32, "a 64-row request returns no 32-row plane");
    expect(frame.rows64 && frame.rows64->image.size == hd, "the upscaled plane is 256x64");
    expect(!frame.extraPlaneAvailable, "no loaded frame has HD content");
    expectPlane(*frame.rows64,
                [&](int x, int y, std::size_t) {
                    if ((x == 0 || x == hd.width - 1) && (y == 0 || y == hd.height - 1)) {
                        return uint16_t{0};
                    }
                    return colorSd(0, static_cast<std::size_t>(y / 2) * 128 + x / 2);
                },
                "a frame without HD comes out upscaled on the 64-row plane");

    frame = preview.renderFrame(document, 1, PlaneRequest::Rows64);
    expect(frame.ok() && frame.rows64 && frame.extraPlaneAvailable, "the HD frame is returned");
    expectPlane(*frame.rows64, [&](int, int, std::size_t index) { return colorHd(1, index); },
                "a frame with HD shows its HD colors on the 64-row plane");

    frame = preview.renderFrame(document, 1, PlaneRequest::Both);
    expect(frame.ok() && frame.rows32 && frame.rows64, "both planes are returned on request");
    expectPlane(*frame.rows32, [&](int, int, std::size_t index) { return colorSd(1, index); },
                "the 32-row plane of the same call");
    expectPlane(*frame.rows64, [&](int, int, std::size_t index) { return colorHd(1, index); },
                "the 64-row plane of the same call");

    frame = preview.renderFrame(document, 1, PlaneRequest::Rows32);
    expect(frame.ok() && frame.rows32 && !frame.rows64, "a 32-row request returns that plane");
}

bool samePlane(const std::optional<RuntimePlane>& lhs, const std::optional<RuntimePlane>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (lhs->image.size != rhs->image.size || lhs->image.pixels != rhs->image.pixels) {
        return false;
    }
    for (std::size_t slot = 0; slot < lhs->rotations.size(); ++slot) {
        if (lhs->rotations[slot].length != rhs->rotations[slot].length ||
            lhs->rotations[slot].delayMs != rhs->rotations[slot].delayMs) {
            return false;
        }
    }
    return true;
}

void expectSameOutput(const RuntimeFrame& lhs, const RuntimeFrame& rhs, const std::string& what)
{
    expect(lhs.ok() && rhs.ok(), what + ": no error (" + lhs.error + rhs.error + ")");
    expect(lhs.identified == rhs.identified && lhs.frameIndex == rhs.frameIndex,
           what + ": the same frame is identified");
    expect(samePlane(lhs.rows32, rhs.rows32), what + ": the 32-row planes are equal");
    expect(samePlane(lhs.rows64, rhs.rows64), what + ": the 64-row planes are equal");
    expect((lhs.nextRotationMs > 0) == (rhs.nextRotationMs > 0),
           what + ": both rotate or neither does");
}

// The subset must not change what libserum renders: every frame comes out the same from
// the whole project written as one cROM, from one subset of all frames, and from a subset
// of that frame alone.
void testSubsetRendersLikeTheWholeProject(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeFullProject(layout);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const ProjectSnapshot whole = document.toSnapshot();
    std::ostringstream stream;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(stream, whole, error), "the project is written");
    const std::string bytes = stream.str();
    const std::vector<uint8_t> crom(bytes.begin(), bytes.end());

    RuntimePreview& preview = RuntimePreview::instance();
    std::vector<std::size_t> all;
    for (std::size_t f = 0; f < document.frameCount(); ++f) {
        all.push_back(f);
    }
    for (PlaneRequest request :
         {PlaneRequest::Rows32, PlaneRequest::Rows64, PlaneRequest::Both}) {
        std::vector<RuntimeFrame> fromWhole;
        expect(preview.openCrom(crom, snapshot.geometry, document.frameCount(), request, &error),
               "the whole project loads: " + error);
        for (std::size_t f : all) {
            fromWhole.push_back(preview.colorize(document.originalFrame(f)));
        }
        preview.close();

        std::size_t withPicture = 0;
        expect(preview.openSequence(document, all, request, &error),
               "the sequence loads: " + error);
        for (std::size_t f : all) {
            const std::string frameText = "frame " + std::to_string(f);
            expectSameOutput(preview.renderSequenceFrame(f), fromWhole[f],
                             frameText + ", sequence against whole project");
            expect(preview.sequenceHasFrame(f), frameText + " is part of the sequence");
            withPicture += fromWhole[f].rows32 || fromWhole[f].rows64 ? 1 : 0;
        }
        preview.close();
        expect(!preview.sequenceHasFrame(0), "a closed sequence has no frames");

        for (std::size_t f : all) {
            expectSameOutput(preview.renderFrame(document, f, request), fromWhole[f],
                             "frame " + std::to_string(f) + ", still against whole project");
        }
        // The frame without 256x64 data is the one without a picture.
        expect(withPicture == all.size() - (layout == PlaneLayout::NativeExtra ? 1 : 0),
               "every frame that can be rendered has a picture");
    }
}

uint32_t readU32(const std::vector<uint8_t>& bytes, std::size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

void testSubsetBuilding(PlaneLayout layout)
{
    const ProjectSnapshot snapshot = makeFullProject(layout);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const Size sd = snapshot.geometry.planeSize(Resolution::Sd);
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);

    SnapshotSubsetMap map;
    const ProjectSnapshot subset = document.toSubsetSnapshot({4, 1, 3}, &map);
    expect(map.frames == std::vector<std::size_t>({4, 1, 3}), "frames keep the requested order");
    expect(map.comparisonMasks == std::vector<std::size_t>({2}), "only the used mask is taken");
    expect(map.backgrounds == std::vector<std::size_t>({0, 2}),
           "backgrounds are numbered in order of first use");
    expect(map.sprites == std::vector<std::size_t>({1, 0, 2}),
           "sprites are numbered in order of first use");
    expect(subset.frames.size() == 3 && subset.comparisonMasks.size() == 1 &&
               subset.backgrounds.size() == 2 && subset.sprites.size() == 3,
           "nothing but the referenced items is in the subset");
    expect(subset.geometry == snapshot.geometry && subset.colorCount == snapshot.colorCount &&
               subset.cromRevision == snapshot.cromRevision,
           "geometry, color count and revision are the project's");
    expect(subset.palettes.empty() && subset.unsupportedPayloads.empty(),
           "palettes and payloads are left out");
    expect(colorizing::document::isValidProjectSnapshot(subset), "the subset is a valid project");

    expect(subset.frames[0].comparisonMaskId == 255, "a frame without mask stays without");
    expect(subset.frames[1].comparisonMaskId == 0, "mask 2 became mask 0");
    expect(subset.comparisonMasks[0].pixels == document.comparisonMask(2).pixels,
           "the mask's pixels are the document's");
    expect(subset.frames[0].backgroundData->backgroundId == 0 &&
               subset.frames[1].backgroundData->backgroundId == 1,
           "background IDs are remapped");
    expect(subset.backgrounds[1].colorizedSd.pixels == document.background(2).colorizedSd.pixels,
           "the background's pixels are the document's");
    expect(!subset.frames[2].backgroundData, "a frame without background data stays without");
    const std::vector<FrameSpriteReference>& slots = subset.frames[0].spriteReferences;
    expect(slots[0].spriteId == 0 && slots[1].spriteId == 1 && slots[2].spriteId == 255,
           "sprite slots keep their place and get the new IDs");
    expect(slots[0].minX == 80 && slots[0].minY == 10 && slots[0].maxX == 110 &&
               slots[0].maxY == 30,
           "sprite slots keep their search area");
    expect(subset.frames[2].spriteReferences[0].spriteId == 2, "sprite 2 stays sprite 2 here");
    expect(subset.sprites[0].shapeMode == 1 &&
               subset.sprites[0].originalSd.pixels == document.sprite(1).originalSd.pixels &&
               subset.sprites[0].detectionDwords == document.sprite(1).detectionDwords,
           "a sprite comes with its detection data");
    expect(subset.frames[1].originalSd.pixels == document.originalFrame(1).pixels,
           "frames keep their ROM frame");
    expect(subset.frames[2].colorizedSd.size == sd, "the original plane keeps its size");
    if (layout == PlaneLayout::NativeOriginal) {
        expect(!subset.frames[0].colorizedHd && subset.sprites[0].maskHd.size == kSpriteSize &&
                   subset.frames[1].backgroundData->maskHd.size == hd,
               "the discarded plane is written as no extra data");
    }

    bool threw = false;
    try {
        (void)document.toSubsetSnapshot({0, document.frameCount()});
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "a frame that does not exist is refused");

    // The cROM: counts in the header, and a size that depends on the subset alone.
    const RuntimeSubset built = buildRuntimeSubset(document, {4, 1, 3});
    const std::size_t padding = built.map.frames.size() - 3;
    expect(layout == PlaneLayout::NativeExtra ? padding == 3 : padding == 0,
           "frames are put in front only where a sprite's 256x64 dynamic data needs them");
    for (std::size_t i = 0; i < padding; ++i) {
        expect(built.map.frames[i] == kNoDocumentFrame, "such a frame stands for no frame");
    }
    expect(readU32(built.crom, 64) == 20 * 4, "the header revision is the project's");
    expect(readU32(built.crom, 68) == static_cast<uint32_t>(sd.width) &&
               readU32(built.crom, 72) == static_cast<uint32_t>(sd.height) &&
               readU32(built.crom, 76) == static_cast<uint32_t>(hd.width) &&
               readU32(built.crom, 80) == static_cast<uint32_t>(hd.height),
           "the header has the project's plane sizes");
    expect(readU32(built.crom, 84) == 3 + padding && readU32(built.crom, 92) == 1 &&
               readU32(built.crom, 96) == 3,
           "the header counts the subset's frames, masks and sprites");
    expect(built.originals.size() == built.map.frames.size() &&
               built.originals.back().pixels == document.originalFrame(3).pixels,
           "the ROM frames to feed come with the subset");

    ProjectSnapshot larger = makeFullProject(layout);
    for (int copy = 0; copy < 40; ++copy) {
        ProjectFrameSnapshot frame = larger.frames[0];
        frame.originalSd.pixels[100 + static_cast<std::size_t>(copy)] ^= 1;
        larger.frames.push_back(std::move(frame));
        addBackground(larger);
    }
    const ProjectDocument largerDocument = ProjectDocument::fromSnapshot(larger);
    const RuntimeSubset fromLarger = buildRuntimeSubset(largerDocument, {4, 1, 3});
    expect(fromLarger.crom == built.crom,
           "the subset of the same frames is the same bytes in a larger project");
    std::ostringstream wholeStream;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(wholeStream, largerDocument.toSnapshot(), error),
           "the larger project is written");
    const RuntimeSubset single = buildRuntimeSubset(largerDocument, {5});
    expect(single.crom.size() * 20 < wholeStream.str().size(),
           "a one-frame subset is a small part of the project");
}

void testReporting()
{
    ProjectSnapshot snapshot = makeProject(PlaneLayout::Ordinary, 3);
    snapshot.frames[1].triggerId = 4711;
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    RuntimePreview& preview = RuntimePreview::instance();

    RuntimeFrame frame = preview.renderFrame(document, 1, PlaneRequest::Rows32);
    expect(frame.ok() && frame.triggerId == 4711, "the frame's trigger is reported");
    expect(frame.cost.cromBytes > 0 && frame.cost.totalMs() > 0.0, "the cost is reported");

    frame = preview.renderFrame(document, 7, PlaneRequest::Rows32);
    expect(!frame.ok() && !frame.identified && !frame.rows32,
           "a frame that does not exist is an error, not a picture");
    expect(!preview.isOpen(), "a failed still leaves nothing loaded");

    std::string error;
    expect(!preview.openCrom(std::vector<uint8_t>(5000, 0x5a), snapshot.geometry, 1,
                             PlaneRequest::Both, &error),
           "bytes that are no cROM do not load");
    expect(error.find("Serum_LoadFromMemory") != std::string::npos,
           "the error is what libserum says: " + error);
    expect(!preview.isOpen(), "a failed load leaves nothing loaded");
    frame = preview.colorize(document.originalFrame(0));
    expect(!frame.ok(), "nothing can be colorized without a colorization");
    expect(!preview.rotate().ok(), "nothing can be rotated without a colorization");

    // A ROM frame the colorization does not know, and one of the wrong size.
    expect(preview.openSequence(document, {0, 2}, PlaneRequest::Both, &error),
           "the sequence opens: " + error);
    frame = preview.colorize(document.originalFrame(1));
    expect(frame.ok() && !frame.identified && !frame.frameIndex && !frame.rows32 && !frame.rows64,
           "an unknown ROM frame is reported as not identified, without a picture");
    frame = preview.renderSequenceFrame(1);
    expect(!frame.ok(), "a frame outside the sequence is an error");
    frame = preview.colorize(makeIndexedFrame(Size{256, 64}));
    expect(!frame.ok(), "a ROM frame of another size is refused");
    frame = preview.renderSequenceFrame(2);
    expect(frame.ok() && frame.frameIndex == std::size_t{2}, "the sequence still works");
    // A still closes the sequence: one colorization at a time.
    expect(preview.renderFrame(document, 0, PlaneRequest::Rows32).ok(), "a still renders");
    expect(!preview.isOpen(), "the still closed the sequence");

    // A 256x64 project's frame without 256x64 data: identified, no picture.
    ProjectSnapshot big = makeProject(PlaneLayout::NativeExtra, 2);
    big.frames[0].colorizedHd.reset();
    const ProjectDocument bigDocument = ProjectDocument::fromSnapshot(big);
    frame = preview.renderFrame(bigDocument, 0, PlaneRequest::Rows64);
    expect(frame.ok() && frame.identified && !frame.rows64 && !frame.rows32,
           "a 256x64 frame without its data is identified and has no picture");
    // Whatever is asked for, such a project has its one plane.
    frame = preview.renderFrame(bigDocument, 1, PlaneRequest::Rows32);
    expect(frame.ok() && frame.rows64 && !frame.rows32,
           "a one-plane project returns its plane for any request");
}

void testPlaneRequests()
{
    using colorizing::runtime::effectivePlaneRequest;
    using colorizing::runtime::planeRequestsFor;
    const ProjectGeometry ordinary(PlaneLayout::Ordinary);
    expect(planeRequestsFor(ordinary) ==
               std::vector<PlaneRequest>({PlaneRequest::Rows32, PlaneRequest::Rows64}),
           "an ordinary project has two planes to look at");
    expect(effectivePlaneRequest(ordinary, PlaneRequest::Both) == PlaneRequest::Both,
           "an ordinary project is asked for what was requested");
    for (PlaneLayout layout : {PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        const ProjectGeometry geometry(layout);
        expect(planeRequestsFor(geometry) == std::vector<PlaneRequest>({PlaneRequest::Rows64}),
               "a one-plane project has its native plane only");
        expect(effectivePlaneRequest(geometry, PlaneRequest::Rows32) == PlaneRequest::Rows64,
               "a one-plane project is asked for its native plane");
    }
}

void testDocumentRevision()
{
    ProjectDocument document = ProjectDocument::fromSnapshot(makeProject(PlaneLayout::Ordinary, 2));
    const uint64_t before = document.revision();
    document.setColorizedPixel(0, Resolution::Sd, 3, 3, 0x1111);
    expect(document.revision() > before, "an edit raises the document revision");
    const uint64_t afterEdit = document.revision();
    document.markClean();
    expect(document.revision() == afterEdit, "saving does not change the revision");
    document.setTriggerId(1, 12);
    expect(document.revision() > afterEdit, "a frame attribute raises the revision too");
}

}  // namespace

void runRuntimePreviewTests()
{
    g_context = "plane requests";
    testPlaneRequests();
    g_context = "document revision";
    testDocumentRevision();
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        g_context = layoutName(layout);
        testStaticFrame(layout);
        testBackground(layout);
        testDynamicColors(layout);
        testDynamicShadows(layout);
        testSpriteWithExactBounds(layout);
        testSpriteWithDynamicContent(layout);
        testSpriteShapeMode(layout);
        testColorRotation(layout);
        testSubsetBuilding(layout);
        testSubsetRendersLikeTheWholeProject(layout);
    }
    g_context = "64-row plane of a 128x32 project";
    testRows64OfOrdinaryProject();
    g_context = "reporting";
    testReporting();
}
