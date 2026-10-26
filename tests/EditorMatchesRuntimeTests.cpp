// The editor's own renderer (app/FrameImageUtils) against libserum: on the plane that has the
// ROM frame's size both have to show the same pixels, for every kind of content and in every
// plane layout. On the 256x64 plane of a 128x32 project they have to where libserum's upscale
// of the 128x32 content rounds nothing.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "runtime/RuntimePreview.h"
#include "RuntimeFixtures.h"

using colorizing::app::SpriteMatch;
using colorizing::app::applyColorRotations;
using colorizing::app::composeResolvedFrame;
using colorizing::app::matchFrameSprites;
using colorizing::document::ProjectDocument;
using colorizing::runtime::RuntimeFrame;
using colorizing::runtime::RuntimePlane;
using colorizing::runtime::RuntimePreview;
using namespace colorizing::runtime_fixtures;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: editor renderer against libserum: " << message << '\n';
        std::exit(1);
    }
}

PlaneRequest nativeRows(const ProjectGeometry& geometry)
{
    return geometry.layout() == PlaneLayout::Ordinary ? PlaneRequest::Rows32
                                                      : PlaneRequest::Rows64;
}

std::size_t differingPixels(const Rgb565Frame& editor, const RuntimePlane& runtime)
{
    if (editor.size != runtime.image.size) {
        return editor.pixels.size();
    }
    std::size_t differing = 0;
    for (std::size_t i = 0; i < editor.pixels.size(); ++i) {
        differing += editor.pixels[i] != runtime.image.pixels[i] ? 1 : 0;
    }
    return differing;
}

// Frames 9 to 11, on top of the full project:
//   9: sprite 0 in slot 0, slot 1 empty, the shape sprite in slot 2. libserum's sprite list
//      ends at the empty slot, so the shape sprite, although it is in the frame, is not drawn.
//  10: sprite 0 in slots 0 and 1 with different search areas and a copy of it in each. Only
//      the first slot is searched.
//  11: sprite 0 twice inside one search area: both are drawn.
ProjectSnapshot makeProjectWithSpriteListCases(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeFullProject(layout);
    for (std::size_t frame = 9; frame < 12; ++frame) {
        snapshot.frames.push_back(snapshot.frames[8]);
        makeDark(snapshot, frame);
    }
    placeSprite(snapshot, 9, 12, 3);
    placeShapeSprite(snapshot, 9, 90, 20);
    setSpriteSlot(snapshot, 9, 0, 0, 2, 0, 60, 20);
    setSpriteSlot(snapshot, 9, 2, 1, 80, 10, 110, 30);

    placeSprite(snapshot, 10, 12, 3);
    placeSprite(snapshot, 10, 80, 14);
    setSpriteSlot(snapshot, 10, 0, 0, 2, 0, 60, 20);
    setSpriteSlot(snapshot, 10, 1, 0, 70, 10, 110, 30);

    placeSprite(snapshot, 11, 12, 3);
    placeSprite(snapshot, 11, 40, 9);
    setSpriteSlot(snapshot, 11, 0, 0, 2, 0, 60, 20);
    return snapshot;
}

void testNativePlane(PlaneLayout layout)
{
    const ProjectSnapshot snapshot = makeProjectWithSpriteListCases(layout);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const Resolution native = document.geometry().nativePlane();
    std::size_t withoutPicture = 0;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const std::string text = std::string(layout == PlaneLayout::Ordinary      ? "128x32"
                                             : layout == PlaneLayout::NativeExtra ? "256x64"
                                                                                  : "192x64") +
                                 " project, frame " + std::to_string(frame);
        const RuntimeFrame runtime = RuntimePreview::instance().renderFrame(
            document, frame, nativeRows(document.geometry()));
        expect(runtime.ok() && runtime.identified, text + ": libserum identifies the frame");
        const Rgb565Frame editor = composeResolvedFrame(document, frame, native, true);
        const RuntimePlane* plane = runtime.plane(nativeRows(document.geometry()));
        if (!plane) {
            ++withoutPicture;
            expect(std::all_of(editor.pixels.begin(), editor.pixels.end(),
                               [](uint16_t pixel) { return pixel == 0; }),
                   text + ": the editor shows nothing where libserum shows nothing");
            continue;
        }
        const std::size_t differing = differingPixels(editor, *plane);
        expect(differing == 0, text + ": " + std::to_string(differing) +
                                   " pixels differ between the editor and libserum");
    }
    expect(withoutPicture == (layout == PlaneLayout::NativeExtra ? 1u : 0u),
           "only the frame without 256x64 data has no picture");

    // What the sprite list cases mean for the matcher.
    const std::vector<SpriteMatch> afterGap = matchFrameSprites(document, 9);
    expect(afterGap.size() == 1 && afterGap[0].spriteIndex == 0,
           "a sprite behind an empty slot is not looked for");
    const std::vector<SpriteMatch> listedTwice = matchFrameSprites(document, 10);
    expect(listedTwice.size() == 1 && listedTwice[0].frameX == 12 && listedTwice[0].frameY == 3,
           "a sprite listed twice is looked for in its first search area only");
    const std::vector<SpriteMatch> twoCopies = matchFrameSprites(document, 11);
    expect(twoCopies.size() == 2 && twoCopies[0].frameY == 3 && twoCopies[1].frameY == 9,
           "every place a sprite is found at is reported, in scan order");
    const std::vector<SpriteMatch> exact = matchFrameSprites(document, 3);
    expect(exact.size() == 1 && exact[0].frameX == 30 && exact[0].frameY == 8 &&
               exact[0].width == kSpriteWidth && exact[0].height == kSpriteHeight,
           "a sprite that fills its search area is found");
    const std::vector<SpriteMatch> shape = matchFrameSprites(document, 4);
    expect(shape.size() == 2 && shape[0].spriteIndex == 1 && shape[0].frameX == 90 &&
               shape[1].spriteIndex == 0,
           "a shape-mode sprite is found by its lit pixels, sprites in slot order");
}

// More sprites than libserum 2.8 allowed (cROM format version 1): 300 sprites, a frame whose
// found sprite is the 300th, in its slot 40.
void testLibserum29Project()
{
    ProjectSnapshot snapshot = makeFullProject(PlaneLayout::Ordinary);
    for (auto& frame : snapshot.frames) {
        if (!frame.spriteReferences.empty()) {
            frame.spriteReferences.resize(colorizing::document::kSpritesPerFrame);
        }
    }
    while (snapshot.sprites.size() < 300) {
        addSprite(snapshot);
    }
    // Frame 3 shows sprite 2 at 30,8; slots 0 to 38 look for the shape sprite where it is not,
    // slot 39 for sprite 299, a copy of sprite 2 in other colors, where it is.
    for (std::size_t slot = 0; slot < 39; ++slot) {
        setSpriteSlot(snapshot, 3, slot, 1, 0, 0, 20, 5);
    }
    setSpriteSlot(snapshot, 3, 39, 299, 30, 8, 35, 11);
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    expect(document.frameSpriteReferences(3).size() == 128, "a project has 128 sprite slots");

    const std::vector<SpriteMatch> found = matchFrameSprites(document, 3);
    expect(found.size() == 1 && found[0].spriteIndex == 299 && found[0].frameX == 30,
           "the editor finds sprite 299 of slot 39");
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const std::string text = "libserum 2.9 project, frame " + std::to_string(frame);
        const RuntimeFrame runtime =
            RuntimePreview::instance().renderFrame(document, frame, PlaneRequest::Rows32);
        expect(runtime.ok() && runtime.identified, text + ": libserum identifies the frame");
        const RuntimePlane* plane = runtime.plane(PlaneRequest::Rows32);
        expect(plane != nullptr, text + ": libserum shows it");
        const Rgb565Frame editor = composeResolvedFrame(document, frame, Resolution::Sd, true);
        const std::size_t differing = differingPixels(editor, *plane);
        expect(differing == 0, text + ": " + std::to_string(differing) +
                                   " pixels differ between the editor and libserum");
    }
    // Sprite 299 drawn, in its own colors.
    const RuntimeFrame withSprite =
        RuntimePreview::instance().renderFrame(document, 3, PlaneRequest::Rows32);
    const RuntimePlane* plane = withSprite.plane(PlaneRequest::Rows32);
    bool drawn = false;
    for (int y = 0; y < kSpriteHeight; ++y) {
        for (int x = 0; x < kSpriteWidth; ++x) {
            if (kSpritePixels[y][x] != kT) {
                drawn = drawn || plane->image.pixels[static_cast<std::size_t>(8 + y) * 128 +
                                                     static_cast<std::size_t>(30 + x)] ==
                                     spriteColor(299, x, y);
            }
        }
    }
    expect(drawn, "libserum draws sprite 299 in its colors");
}

// The 256x64 plane of a 128x32 project.
void testHdPlaneOfOrdinaryProject()
{
    ProjectSnapshot snapshot = makeFullProject(PlaneLayout::Ordinary);
    // Frame 3 gets an HD frame; its sprite (2) has no HD art and a dynamic pixel.
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    snapshot.frames[3].colorizedHd = makeRgb565Frame(hd);
    for (std::size_t i = 0; i < pixels(hd); ++i) {
        snapshot.frames[3].colorizedHd->pixels[i] = static_cast<uint16_t>(0x4000 + i % 0x4000);
    }
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);

    // An HD frame with nothing but its own colors: the same pixels as libserum. Wherever the
    // ROM frame decides (background, dynamic content, sprites) libserum reads it upscaled
    // and rounds edges, which the editor does not reproduce.
    {
        const RuntimeFrame runtime =
            RuntimePreview::instance().renderFrame(document, 6, PlaneRequest::Rows64);
        expect(runtime.ok() && runtime.rows64, "libserum renders the HD frame");
        expect(differingPixels(composeResolvedFrame(document, 6, Resolution::Hd, true),
                               *runtime.rows64) == 0,
               "an HD frame with static content equals libserum's");
    }

    // A sprite without HD art on an HD frame is the 128x32 sprite with every pixel doubled,
    // its dynamic pixel included. libserum rounds the edges of that upscale, so only the
    // editor's own picture is checked here.
    const Rgb565Frame editor = composeResolvedFrame(document, 3, Resolution::Hd, true);
    const auto at = [&](int x, int y) {
        return editor.pixels[static_cast<std::size_t>(y) * hd.width + x];
    };
    const int sx = 30;
    const int sy = 8;
    for (int py = 0; py < kSpriteHeight; ++py) {
        for (int px = 0; px < kSpriteWidth; ++px) {
            const int x = (sx + px) * 2;
            const int y = (sy + py) * 2;
            const uint16_t expected =
                kSpritePixels[py][px] == kT
                    ? snapshot.frames[3].colorizedHd->pixels[static_cast<std::size_t>(y) *
                                                                 hd.width + x]
                    : px == 2 && py == 1 ? spriteDynamicColor(2, kSpritePixels[py][px])
                                         : spriteColor(2, px, py);
            if (kSpritePixels[py][px] == kT) {
                expect(at(x, y) == expected, "a transparent sprite pixel shows the HD frame");
                continue;
            }
            expect(at(x, y) == expected && at(x + 1, y) == expected &&
                       at(x, y + 1) == expected && at(x + 1, y + 1) == expected,
                   "a sprite without HD is drawn doubled on an HD frame");
        }
    }
    const RuntimeFrame runtime =
        RuntimePreview::instance().renderFrame(document, 3, PlaneRequest::Rows64);
    expect(runtime.ok() && runtime.rows64, "libserum renders the frame with the sprite");
    // Outside the sprite's edges, which libserum rounds, nothing differs.
    expect(differingPixels(editor, *runtime.rows64) <= 4 * (kSpriteWidth + kSpriteHeight),
           "the doubled sprite differs from libserum's upscale at its edges at most");
}

// Dynamic zones on the 256x64 plane of a 128x32 project. libserum renders them at 128x32
// with the 128x32 mask and colors and upscales that with Scale2x; the editor doubles pixels.
// The two agree wherever Scale2x has nothing to round, so this picture is built of rows:
// the ROM frame, the 128x32 colors, the dynamic zones and both background masks are the same
// along every row. Scale2x replaces a pixel only if its left and right neighbors differ,
// which never happens here, so every 128x32 pixel becomes a plain 2x2 block and the whole
// plane can be compared. The 256x64 frame and background are not upscaled and vary freely.
ProjectSnapshot makeRowProject()
{
    ProjectSnapshot snapshot = makeProject(PlaneLayout::Ordinary, 1);
    const Size sd = snapshot.geometry.planeSize(Resolution::Sd);
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    ProjectFrameSnapshot& frame = snapshot.frames[0];
    // Shades by row: 0 0 1 2 3 0 2 0 ..., so zones have lit and unlit rows next to each other.
    constexpr std::array<uint8_t, 8> kRowShades{0, 0, 1, 2, 3, 0, 2, 0};
    for (int y = 0; y < sd.height; ++y) {
        for (int x = 0; x < sd.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * sd.width + x;
            frame.originalSd.pixels[index] = kRowShades[static_cast<std::size_t>(y) % 8];
            frame.colorizedSd.pixels[index] = static_cast<uint16_t>(0x1000 + y * 37);
        }
    }
    frame.colorizedHd = makeRgb565Frame(hd);
    for (std::size_t i = 0; i < pixels(hd); ++i) {
        frame.colorizedHd->pixels[i] = colorHd(0, i);
    }

    FrameDynamicData& dynamic = addDynamicData(snapshot, 0);
    const auto zoneRows = [&](int first, int last, uint8_t set) {
        for (int y = first; y <= last; ++y) {
            std::fill_n(dynamic.maskSd.pixels.begin() + static_cast<std::ptrdiff_t>(y) * sd.width,
                        sd.width, set);
        }
    };
    zoneRows(1, 6, 2);     // lit and unlit rows, no shadow
    zoneRows(9, 12, 5);    // casts a shadow up and down
    zoneRows(17, 20, 7);   // under the background, see below
    zoneRows(26, 28, 9);   // shadow from the stored 256x64 tables, see the callers
    dynamic.shadowDirectionsSd[5] = 0x02 | 0x20;
    dynamic.shadowColorsSd[5] = kShadowColor;

    // A background with a 256x64 image. Its 128x32 mask covers rows 16 to 18, its 256x64
    // mask rows 34 to 39 (128x32 rows 17 to 19) and rows 60 to 63, outside every zone.
    assignBackground(snapshot, 0, addBackground(snapshot));
    FrameBackgroundData& background = *snapshot.frames[0].backgroundData;
    std::fill(background.maskSd.pixels.begin(), background.maskSd.pixels.end(), uint8_t{0});
    std::fill(background.maskHd.pixels.begin(), background.maskHd.pixels.end(), uint8_t{0});
    std::fill_n(background.maskSd.pixels.begin() + 16 * sd.width, 3 * sd.width, uint8_t{1});
    std::fill_n(background.maskHd.pixels.begin() + 34 * hd.width, 6 * hd.width, uint8_t{1});
    std::fill_n(background.maskHd.pixels.begin() + 60 * hd.width, 4 * hd.width, uint8_t{1});
    return snapshot;
}

Rgb565Frame expectHdPlaneEqualsRuntime(const ProjectSnapshot& snapshot, const std::string& text)
{
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RuntimeFrame runtime =
        RuntimePreview::instance().renderFrame(document, 0, PlaneRequest::Rows64);
    expect(runtime.ok() && runtime.identified && runtime.rows64,
           text + ": libserum renders the 64-row plane");
    Rgb565Frame editor = composeResolvedFrame(document, 0, Resolution::Hd, true);
    const std::size_t differing = differingPixels(editor, *runtime.rows64);
    expect(differing == 0, text + ": " + std::to_string(differing) +
                               " pixels of the 256x64 plane differ between the editor and "
                               "libserum");
    return editor;
}

// Dynamic sets that color the SD picture only (libserum 2.9): on the HD picture of a frame
// with an HD frame of its own, their zones show that HD frame, without a shadow; the SD
// picture keeps them.
void testOriginalOnlyDynamicSets()
{
    ProjectSnapshot snapshot = makeRowProject();
    for (auto& frame : snapshot.frames) {
        frame.spriteReferences.resize(colorizing::document::kSpritesPerFrame);
    }
    // Set 2 (rows 1 to 6) and set 5 (rows 9 to 12, with a shadow).
    snapshot.frames[0].originalOnlyDynamicSets = (1u << 2) | (1u << 5);
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    const Size sd = snapshot.geometry.planeSize(Resolution::Sd);
    const auto hdStatic = [&](int x, int y) {
        return snapshot.frames[0].colorizedHd->pixels[static_cast<std::size_t>(y) * hd.width + x];
    };
    const auto at = [&](const Rgb565Frame& frame, int x, int y) {
        return frame.pixels[static_cast<std::size_t>(y) * frame.size.width + x];
    };
    const Rgb565Frame picture =
        expectHdPlaneEqualsRuntime(snapshot, "sets that color the SD picture only");
    expect(at(picture, 10, 4) == hdStatic(10, 4) && at(picture, 10, 2) == hdStatic(10, 2) &&
               at(picture, 50, 20) == hdStatic(50, 20),
           "their zones show the HD frame, lit or not");
    expect(at(picture, 50, 19) == hdStatic(50, 19) && at(picture, 50, 26) == hdStatic(50, 26),
           "they cast no shadow on the HD picture");
    expect(at(picture, 30, 36) == dynamicColor(7, 1),
           "the other sets stay on the HD picture");
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const Rgb565Frame sdPicture = composeResolvedFrame(document, 0, Resolution::Sd, true);
    expect(sdPicture.size == sd && at(sdPicture, 10, 2) == dynamicColor(2, 1) &&
               at(sdPicture, 50, 13) == kShadowColor,
           "the SD picture keeps them, shadow included");
    expect(document.effectiveOriginalOnlyDynamicSets(0) == ((1u << 2) | (1u << 5)),
           "they apply to a frame with an HD frame");
}

void testHdDynamicZonesOfOrdinaryProject()
{
    const ProjectSnapshot snapshot = makeRowProject();
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    const Rgb565Frame picture =
        expectHdPlaneEqualsRuntime(snapshot, "HD statics with 128x32 dynamic zones");
    const auto at = [&](const Rgb565Frame& frame, int x, int y) {
        return frame.pixels[static_cast<std::size_t>(y) * hd.width + x];
    };
    const auto hdStatic = [&](int x, int y) {
        return snapshot.frames[0].colorizedHd->pixels[static_cast<std::size_t>(y) * hd.width + x];
    };
    const auto hdBackground = [&](int x, int y) {
        return snapshot.backgrounds[0].colorizedHd->pixels[static_cast<std::size_t>(y) *
                                                               hd.width + x];
    };
    // What the equality above stands for, pixel by pixel (128x32 row r is rows 2r and 2r+1).
    expect(at(picture, 10, 0) == hdStatic(10, 0) && at(picture, 11, 1) == hdStatic(11, 1),
           "outside the zones the 256x64 frame shows");
    expect(at(picture, 10, 4) == dynamicColor(2, 1) && at(picture, 11, 5) == dynamicColor(2, 1) &&
               at(picture, 200, 8) == dynamicColor(2, 3),
           "a lit zone pixel has its set's color for the ROM shade, doubled");
    expect(at(picture, 10, 2) == dynamicColor(2, 0) && at(picture, 10, 10) == dynamicColor(2, 0),
           "an unlit zone pixel has its set's color for shade 0");
    // Zone 5: rows 9 (shade 0), 10 (1), 11 (2), 12 (3); the shadow is one 256x64 row above
    // the first lit row and one below the last, and never on lit dynamic content.
    expect(at(picture, 50, 19) == kShadowColor && at(picture, 50, 18) == dynamicColor(5, 0) &&
               at(picture, 50, 20) == dynamicColor(5, 1) && at(picture, 50, 25) ==
                                                                dynamicColor(5, 3) &&
               at(picture, 50, 26) == kShadowColor && at(picture, 50, 27) == hdStatic(50, 27),
           "a shadow is one 256x64 pixel wide around the upscaled zone");
    // Zone 7, rows 17 to 20 with shades 0 1 2 3. Row 17 is unlit under the 128x32 background
    // mask, so the layer leaves it to the 256x64 picture: the 256x64 background where its
    // mask is set.
    expect(at(picture, 30, 34) == hdBackground(30, 34) && at(picture, 30, 35) ==
                                                              hdBackground(30, 35),
           "an unlit zone pixel under both background masks shows the 256x64 background");
    expect(at(picture, 30, 36) == dynamicColor(7, 1) && at(picture, 30, 38) == dynamicColor(7, 2),
           "a lit zone pixel covers the 256x64 background");
    // Row 16: unlit, 128x32 background mask set, not a zone, 256x64 mask not set.
    expect(at(picture, 30, 32) == hdStatic(30, 32),
           "the 128x32 background is never drawn on the 256x64 plane");
    // Rows 60 to 63 are 128x32 rows 30 (shade 2) and 31 (shade 0), 256x64 mask set.
    expect(at(picture, 30, 60) == hdStatic(30, 60) && at(picture, 30, 62) == hdBackground(30, 62),
           "outside the zones the 256x64 background shows through unlit pixels only");

    // The stored 256x64 dynamic mask and colors are not used: other zones, other sets and
    // other colors there give the same picture, in the editor and in libserum. A file has
    // the 256x64 frame's pixels under that mask as 0 (the save rule of the Win32 editor, kept
    // by the serializer), so the frame is compared with those pixels at 0 in both projects.
    ProjectSnapshot withStoredHdData = snapshot;
    FrameDynamicData& stored = *withStoredHdData.frames[0].dynamicData;
    for (std::size_t i = 0; i < stored.maskHd.pixels.size(); ++i) {
        stored.maskHd.pixels[i] = i % 3 == 0 ? uint8_t{255} : static_cast<uint8_t>(i % 11);
    }
    for (std::size_t i = 0; i < stored.colorsHd.size(); ++i) {
        stored.colorsHd[i] = static_cast<uint16_t>(0x0700 + i);
    }
    // A set with a 128x32 shadow direction does not take the 256x64 one either.
    stored.shadowDirectionsHd[5] = 0xff;
    stored.shadowColorsHd[5] = 0x1234;
    ProjectSnapshot withoutStoredHdData = snapshot;
    for (std::size_t i = 0; i < stored.maskHd.pixels.size(); ++i) {
        if (stored.maskHd.pixels[i] != 255) {
            withStoredHdData.frames[0].colorizedHd->pixels[i] = 0;
            withoutStoredHdData.frames[0].colorizedHd->pixels[i] = 0;
        }
    }
    const Rgb565Frame withStored =
        expectHdPlaneEqualsRuntime(withStoredHdData, "stored 256x64 dynamic mask and colors");
    const Rgb565Frame withoutStored =
        expectHdPlaneEqualsRuntime(withoutStoredHdData, "no stored 256x64 dynamic data");
    expect(withStored.pixels == withoutStored.pixels,
           "stored 256x64 dynamic masks and colors do not change the picture");
    const ProjectDocument storedDocument = ProjectDocument::fromSnapshot(withStoredHdData);
    expect(composeResolvedFrame(storedDocument, 0, Resolution::Sd, true).pixels ==
               composeResolvedFrame(ProjectDocument::fromSnapshot(snapshot), 0, Resolution::Sd,
                                    true)
                   .pixels,
           "nor the 128x32 picture");

    // What the stored 256x64 mask still does, through that save rule: a 256x64 pixel painted
    // under it is written as 0. The canvas shows the painted color until the project is
    // reopened; libserum, given what a save writes, shows 0 where no zone covers the pixel.
    // (1, 0) is under the stored mask and outside every 128x32 zone.
    ProjectSnapshot paintedUnderStoredMask = snapshot;
    paintedUnderStoredMask.frames[0].dynamicData->maskHd = stored.maskHd;
    const ProjectDocument paintedDocument = ProjectDocument::fromSnapshot(paintedUnderStoredMask);
    const RuntimeFrame saved =
        RuntimePreview::instance().renderFrame(paintedDocument, 0, PlaneRequest::Rows64);
    expect(saved.ok() && saved.rows64 && at(saved.rows64->image, 1, 0) == 0 &&
               at(composeResolvedFrame(paintedDocument, 0, Resolution::Hd, true), 1, 0) ==
                   hdStatic(1, 0) &&
               hdStatic(1, 0) != 0,
           "a 256x64 pixel under the stored 256x64 dynamic mask is saved as 0");

    // The one thing libserum does read of them (CheckDynaShadow, GenerateExtraPlaneShadows):
    // a set without a 128x32 shadow direction takes direction and color from the 256x64
    // tables when it renders the 64-row plane. Zone 9 is rows 26 (shade 1), 27 (2), 28 (3).
    ProjectSnapshot withStoredShadow = snapshot;
    FrameDynamicData& shadow = *withStoredShadow.frames[0].dynamicData;
    shadow.shadowDirectionsHd[9] = 0x02;
    shadow.shadowColorsHd[9] = 0x0bad;
    const Rgb565Frame withShadow =
        expectHdPlaneEqualsRuntime(withStoredShadow, "shadow from the stored 256x64 tables");
    expect(at(withShadow, 70, 51) == 0x0bad && at(picture, 70, 51) == hdStatic(70, 51) &&
               at(withShadow, 70, 52) == dynamicColor(9, 1),
           "a set without a 128x32 shadow takes the stored 256x64 shadow on the 64-row plane");
}


// Color rotations, step by step: the editor's rotation preview against libserum's
// Serum_Rotate(). libserum steps a slot once per call when the slot's delay has passed, so
// the test waits longer than the delay before every call.
constexpr uint16_t kRotationDelayMs = 30;

std::vector<uint16_t> rotationTable(const std::vector<uint16_t>& colors)
{
    std::vector<uint16_t> table(4 * 64, 0);
    table[0] = static_cast<uint16_t>(colors.size());
    table[1] = kRotationDelayMs;
    std::copy(colors.begin(), colors.end(), table.begin() + 2);
    return table;
}

// Colors the plane shows, for a rotation that touches every kind of content: first those of
// pixels the editor says stand still (dynamic content and shadows), then those of the sprites
// found, then others spread over the picture.
std::vector<uint16_t> shownColors(const ProjectDocument& document, std::size_t frame,
                                  Resolution plane)
{
    std::vector<uint8_t> notRotating;
    const Rgb565Frame picture = composeResolvedFrame(document, frame, plane, true, &notRotating);
    std::vector<uint16_t> colors;
    const auto add = [&](uint16_t color, std::size_t limit) {
        if (colors.size() < limit &&
            std::find(colors.begin(), colors.end(), color) == colors.end()) {
            colors.push_back(color);
        }
    };
    for (std::size_t i = 0; i < picture.pixels.size(); ++i) {
        if (notRotating[i] != 0) {
            add(picture.pixels[i], 16);
        }
    }
    if (document.geometry().planeScale(plane) == 1) {
        for (const SpriteMatch& match : matchFrameSprites(document, frame)) {
            for (int y = 0; y < match.height; ++y) {
                for (int x = 0; x < match.width; ++x) {
                    add(picture.pixels[static_cast<std::size_t>(match.frameY + y) *
                                           picture.size.width +
                                       match.frameX + x],
                        40);
                }
            }
        }
    }
    const std::size_t stride = std::max<std::size_t>(1, picture.pixels.size() / 61);
    for (std::size_t i = 0; i < picture.pixels.size(); i += stride) {
        add(picture.pixels[i], colorizing::document::kMaxColorRotationColors);
    }
    return colors;
}

struct RotationStepCounts {
    std::size_t moved = 0;
    // Pixels that stand still although their color is one of the rotation's.
    std::size_t standing = 0;
};

// Plays the frame through libserum, steps its rotations and expects the editor's preview of
// the same number of steps to show the same pixels.
RotationStepCounts expectRotationStepsEqualRuntime(const ProjectDocument& document,
                                                   std::size_t frame,
                                                   Resolution plane,
                                                   PlaneRequest rows,
                                                   const std::string& text)
{
    RotationStepCounts counts;
    RuntimePreview& preview = RuntimePreview::instance();
    std::string error;
    expect(preview.openSequence(document, {frame}, rows, &error),
           text + ": the sequence opens: " + error);
    const RuntimeFrame shown = preview.renderSequenceFrame(frame);
    const RuntimePlane* still = shown.plane(rows);
    if (!still) {
        preview.close();
        return counts;
    }
    std::vector<uint8_t> notRotating;
    const Rgb565Frame editorStill = composeResolvedFrame(document, frame, plane, true,
                                                         &notRotating);
    expect(differingPixels(editorStill, *still) == 0, text + ": the still pictures are the same");
    expect(still->rotates() && shown.nextRotationMs > 0, text + ": libserum rotates the frame");
    const auto& table = document.colorRotations(frame, plane);
    for (std::size_t i = 0; i < editorStill.pixels.size(); ++i) {
        const bool inRotation =
            std::find(table.begin() + 2, table.begin() + 2 + table[0], editorStill.pixels[i]) !=
            table.begin() + 2 + table[0];
        counts.standing += notRotating[i] != 0 && inRotation ? 1 : 0;
    }
    for (int step = 1; step <= 3; ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kRotationDelayMs + 15));
        const RuntimeFrame rotated = preview.rotate();
        const RuntimePlane* runtime = rotated.plane(rows);
        const std::string stepText = text + ", rotation step " + std::to_string(step);
        expect(rotated.ok() && !rotated.unchanged && runtime, stepText + ": libserum stepped");
        Rgb565Frame editor = composeResolvedFrame(document, frame, plane, true, &notRotating);
        applyColorRotations(document, frame, plane, step * kRotationDelayMs, editor,
                            &notRotating);
        const std::size_t differing = differingPixels(editor, *runtime);
        expect(differing == 0, stepText + ": " + std::to_string(differing) +
                                   " pixels differ between the editor's rotation preview and "
                                   "libserum");
        for (std::size_t i = 0; i < editor.pixels.size(); ++i) {
            counts.moved += editor.pixels[i] != editorStill.pixels[i] ? 1 : 0;
            expect(notRotating[i] == 0 || runtime->image.pixels[i] == still->image.pixels[i],
                   stepText + ": dynamic content and shadows stand still in libserum");
        }
    }
    preview.close();
    return counts;
}

// Every frame of the full project, on the plane with the ROM frame's size, with a rotation
// made of colors the frame shows.
void testRotationSteps(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProjectWithSpriteListCases(layout);
    const bool extra = extraIsNative(snapshot.geometry);
    {
        const ProjectDocument plain = ProjectDocument::fromSnapshot(snapshot);
        const Resolution native = plain.geometry().nativePlane();
        for (std::size_t frame = 0; frame < plain.frameCount(); ++frame) {
            (extra ? snapshot.frames[frame].colorRotationsHd
                   : snapshot.frames[frame].colorRotationsSd) =
                rotationTable(shownColors(plain, frame, native));
        }
    }
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const Resolution native = document.geometry().nativePlane();
    RotationStepCounts total;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const std::string text = std::string(layout == PlaneLayout::Ordinary      ? "128x32"
                                             : layout == PlaneLayout::NativeExtra ? "256x64"
                                                                                  : "192x64") +
                                 " project, frame " + std::to_string(frame);
        const RotationStepCounts counts = expectRotationStepsEqualRuntime(
            document, frame, native, nativeRows(document.geometry()), text);
        total.moved += counts.moved;
        total.standing += counts.standing;
    }
    expect(total.moved > 0, "the rotations move pixels");
    expect(total.standing > 0,
           "dynamic content and shadows in a rotation's color are part of the comparison");
}

// The report this is for (afm_113b, frames 16 to 77): white digits from a dynamic zone, with
// their shadow, over a background whose stars rotate through white. The stars rotate; the
// digits and the shadow do not, although the rotation lists both their colors.
void testDynamicContentStandsStillInRotations()
{
    constexpr uint16_t kStar = 0x0808;
    constexpr uint16_t kWhite = 0xffff;
    ProjectSnapshot snapshot = makeProject(PlaneLayout::Ordinary, 1);
    const Size sd = snapshot.geometry.planeSize(Resolution::Sd);
    makeDark(snapshot, 0);
    const std::size_t glyph = static_cast<std::size_t>(10) * sd.width + 20;
    snapshot.frames[0].originalSd.pixels[glyph] = 3;
    FrameDynamicData& dynamic = addDynamicData(snapshot, 0);
    for (int y = 8; y <= 12; ++y) {
        std::fill_n(dynamic.maskSd.pixels.begin() + static_cast<std::ptrdiff_t>(y) * sd.width + 16,
                    8, uint8_t{1});
    }
    dynamic.colorsSd[1 * kColorCount + 3] = kWhite;
    dynamic.shadowDirectionsSd[1] = 0x02 | 0x20;  // above and below
    dynamic.shadowColorsSd[1] = kShadowColor;
    assignBackground(snapshot, 0, addBackground(snapshot));
    Rgb565Frame& background = snapshot.backgrounds[0].colorizedSd;
    std::fill(background.pixels.begin(), background.pixels.end(), uint16_t{0});
    const std::size_t star = static_cast<std::size_t>(3) * sd.width + 5;
    background.pixels[star] = kStar;
    // Stars right above and under the digit: the shadow covers them. libserum has rendered
    // the one above when the digit casts its shadow, and not yet the one below.
    background.pixels[glyph - sd.width] = kStar;
    background.pixels[glyph + sd.width] = kStar;
    snapshot.frames[0].colorRotationsSd = rotationTable({kStar, kWhite, kShadowColor});
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);

    std::vector<uint8_t> notRotating;
    const Rgb565Frame still = composeResolvedFrame(document, 0, Resolution::Sd, true,
                                                   &notRotating);
    expect(still.pixels[glyph] == kWhite && still.pixels[glyph - sd.width] == kShadowColor &&
               still.pixels[glyph + sd.width] == kShadowColor && still.pixels[star] == kStar,
           "the picture has the digit, its shadow above and below, and a star");
    expect(notRotating[glyph] == 1 && notRotating[glyph - sd.width] == 1 &&
               notRotating[glyph + sd.width] == 1 && notRotating[star] == 0,
           "the digit and its shadow are marked as standing still, the star is not");
    constexpr std::array<uint16_t, 3> kStarSteps{kWhite, kShadowColor, kStar};
    for (int step = 1; step <= 3; ++step) {
        Rgb565Frame editor = still;
        applyColorRotations(document, 0, Resolution::Sd, step * kRotationDelayMs, editor,
                            &notRotating);
        expect(editor.pixels[glyph] == kWhite, "the digit stays white in the editor's preview");
        expect(editor.pixels[glyph - sd.width] == kShadowColor &&
                   editor.pixels[glyph + sd.width] == kShadowColor,
               "the shadow keeps its color in the editor's preview");
        expect(editor.pixels[star] == kStarSteps[static_cast<std::size_t>(step - 1)],
               "the star rotates in the editor's preview");
    }
    for (PlaneRequest rows : {PlaneRequest::Rows32, PlaneRequest::Rows64, PlaneRequest::Both}) {
        RuntimePreview& preview = RuntimePreview::instance();
        std::string error;
        expect(preview.openSequence(document, {0}, rows, &error), "the sequence opens: " + error);
        const RuntimeFrame shown = preview.renderSequenceFrame(0);
        RuntimeFrame rotated = shown;
        for (int step = 1; step <= 3; ++step) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kRotationDelayMs + 15));
            rotated = preview.rotate();
            expect(rotated.ok() && !rotated.unchanged, "libserum stepped the rotation");
            const uint16_t starNow = kStarSteps[static_cast<std::size_t>(step - 1)];
            if (rows != PlaneRequest::Rows64) {
                const Rgb565Frame& plane = rotated.rows32->image;
                expect(plane.pixels[glyph] == kWhite &&
                           plane.pixels[glyph - sd.width] == kShadowColor &&
                           plane.pixels[glyph + sd.width] == kShadowColor,
                       "the digit and its shadow stand still on libserum's 32-row plane");
                expect(plane.pixels[star] == starNow, "the star rotates on the 32-row plane");
            }
            if (rows != PlaneRequest::Rows32) {
                const Rgb565Frame& plane = rotated.rows64->image;
                const Rgb565Frame& first = shown.rows64->image;
                const std::size_t width = static_cast<std::size_t>(plane.size.width);
                expect(first.pixels[20 * width + 40] == kWhite &&
                           first.pixels[6 * width + 10] == kStar,
                       "the 64-row plane has the digit and the star");
                // The shadow is one pixel thick there, so half of each star it covers shows
                // beside it and rotates like the other star.
                for (std::size_t i = 0; i < plane.pixels.size(); ++i) {
                    expect(plane.pixels[i] == (first.pixels[i] == kStar ? starNow : first.pixels[i]),
                           "on libserum's 64-row plane the stars rotate and nothing else");
                }
            }
        }
        preview.close();
    }
}

// The 256x64 plane of a 128x32 project, on the picture built of rows.
void testHdPlaneRotationSteps()
{
    ProjectSnapshot snapshot = makeRowProject();
    {
        const ProjectDocument plain = ProjectDocument::fromSnapshot(snapshot);
        snapshot.frames[0].colorRotationsHd =
            rotationTable(shownColors(plain, 0, Resolution::Hd));
    }
    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const RotationStepCounts counts = expectRotationStepsEqualRuntime(
        document, 0, Resolution::Hd, PlaneRequest::Rows64, "256x64 plane of a 128x32 project");
    expect(counts.moved > 0 && counts.standing > 0,
           "the 256x64 rotation moves pixels and leaves dynamic content alone");
}

}  // namespace

void runEditorMatchesRuntimeTests()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testNativePlane(layout);
    }
    testLibserum29Project();
    testHdPlaneOfOrdinaryProject();
    testHdDynamicZonesOfOrdinaryProject();
    testOriginalOnlyDynamicSets();
    testDynamicContentStandsStillInRotations();
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testRotationSteps(layout);
    }
    testHdPlaneRotationSteps();
}
