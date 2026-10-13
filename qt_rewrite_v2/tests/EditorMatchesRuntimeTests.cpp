// The editor's own renderer (app/FrameImageUtils) against libserum: on the plane that has the
// ROM frame's size both have to show the same pixels, for every kind of content and in every
// plane layout.

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

}  // namespace

void runEditorMatchesRuntimeTests()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testNativePlane(layout);
    }
    testHdPlaneOfOrdinaryProject();
}
