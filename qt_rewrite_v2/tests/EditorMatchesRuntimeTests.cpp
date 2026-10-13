// The editor's own renderer (app/FrameImageUtils) against libserum: on the plane that has the
// ROM frame's size both have to show the same pixels, for every kind of content and in every
// plane layout. On the 256x64 plane of a 128x32 project they have to where libserum's upscale
// of the 128x32 content rounds nothing.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FrameImageUtils.h"
#include "runtime/RuntimePreview.h"
#include "RuntimeFixtures.h"

using colorizing::app::SpriteMatch;
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

}  // namespace

void runEditorMatchesRuntimeTests()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testNativePlane(layout);
    }
    testHdPlaneOfOrdinaryProject();
    testHdDynamicZonesOfOrdinaryProject();
}
