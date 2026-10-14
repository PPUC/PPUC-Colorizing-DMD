#include "edit/SpriteClipboard.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/PlaneSelection.h"

using colorizing::document::ProjectDocument;
using colorizing::document::ProjectSnapshot;
using colorizing::edit::CopyBuffer;
using colorizing::edit::EditCommand;
using colorizing::edit::EditKind;
using colorizing::render::PlaneLayout;
using colorizing::render::Resolution;
namespace edit = colorizing::edit;
namespace document = colorizing::document;
namespace render = colorizing::render;
namespace fixtures = colorizing::runtime_fixtures;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: sprite clipboard: " << message << '\n';
        std::exit(1);
    }
}

std::size_t at(int x, int y)
{
    return static_cast<std::size_t>(y) * document::kMaxSpriteWidth + static_cast<std::size_t>(x);
}

ProjectDocument makeDocument(PlaneLayout layout)
{
    ProjectSnapshot snapshot = fixtures::makeProject(layout, 3);
    fixtures::addSprite(snapshot);
    snapshot.sprites.push_back(fixtures::emptySprite());
    if (fixtures::extraIsNative(snapshot.geometry)) {
        snapshot.sprites.back().hasHd = true;
    }
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

void testCopySprite()
{
    const ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    const CopyBuffer buffer = edit::copySprite(document, 0, Resolution::Sd);
    const document::SpriteRecord& sprite = document.sprite(0);
    expect(buffer.fromSprite && buffer.plane == Resolution::Sd && buffer.selectedCount() > 0,
           "the sprite's pixels are copied");
    // Every opaque pixel of the sprite is in the box with its color and shade.
    std::size_t opaque = 0;
    for (int y = 0; y < static_cast<int>(document::kMaxSpriteHeight); ++y) {
        for (int x = 0; x < static_cast<int>(document::kMaxSpriteWidth); ++x) {
            if (sprite.originalSd.pixels[at(x, y)] == 255) {
                continue;
            }
            ++opaque;
            const int bx = x - buffer.box.x;
            const int by = y - buffer.box.y;
            const std::size_t i = static_cast<std::size_t>(by * buffer.box.width + bx);
            expect(bx >= 0 && by >= 0 && bx < buffer.box.width && by < buffer.box.height &&
                       buffer.mask[i] && buffer.colors[i] == sprite.colorizedSd.pixels[at(x, y)] &&
                       buffer.originals[i] == sprite.originalSd.pixels[at(x, y)],
                   "pixel in the box");
        }
    }
    expect(opaque == buffer.selectedCount(), "only the opaque pixels");
    expect([&]() {
        try {
            (void)edit::copySprite(document, 1, Resolution::Sd);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    }(), "an empty sprite has nothing to copy");
}

void testPasteSpriteIntoSprite()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    const CopyBuffer buffer = edit::copySprite(document, 0, Resolution::Sd);
    const uint32_t sourceBefore = document.spriteSourceFrame(1);
    std::unique_ptr<EditCommand> paste =
        edit::pasteIntoSprite(document, 1, buffer, Resolution::Sd, false, false);
    expect(paste && paste->target().kind == EditKind::SpriteAttributes &&
               paste->target().index == 1,
           "one step in the sprite's history");
    const document::SpriteRecord& sprite = document.sprite(1);
    std::size_t i = 0;
    for (int y = 0; y < buffer.box.height; ++y) {
        for (int x = 0; x < buffer.box.width; ++x, ++i) {
            expect(buffer.mask[i] ? sprite.originalSd.pixels[at(x, y)] == buffer.originals[i] &&
                                        sprite.colorizedSd.pixels[at(x, y)] == buffer.colors[i]
                                  : sprite.originalSd.pixels[at(x, y)] == 255,
                   "pasted at the top left corner");
        }
    }
    expect(document.spriteSourceFrame(1) == sourceBefore, "a sprite buffer names no frame");
    expect(document.sprite(1).detectionDwords == fixtures::emptySprite().detectionDwords,
           "no detection area, no detection data");
    paste->revert(document);
    expect(document.sprite(1).originalSd.pixels == fixtures::emptySprite().originalSd.pixels,
           "undo empties the sprite again");

    // Mirrored horizontally: the last column of the box lands in the first.
    std::unique_ptr<EditCommand> mirrored =
        edit::pasteIntoSprite(document, 1, buffer, Resolution::Sd, true, false);
    const int last = buffer.box.width - 1;
    for (int y = 0; y < buffer.box.height; ++y) {
        const std::size_t source = static_cast<std::size_t>(y * buffer.box.width + last);
        if (buffer.mask[source]) {
            expect(document.sprite(1).originalSd.pixels[at(0, y)] == buffer.originals[source],
                   "mirrored");
        }
    }
}

void testPasteFrameSelectionIntoSprite()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    const uint32_t sourceBefore = document.spriteSourceFrame(1);
    edit::PlaneSelection selection(Resolution::Sd, render::kSdFrameSize);
    selection.paint({{10, 5}, {11, 5}, {10, 6}}, false);
    const CopyBuffer buffer = edit::copySelection(document, 2, selection);
    std::unique_ptr<EditCommand> paste =
        edit::pasteIntoSprite(document, 1, buffer, Resolution::Sd, false, false);
    expect(paste && document.spriteSourceFrame(1) == 2,
           "a frame buffer makes its frame the sprite's source frame");
    expect(document.sprite(1).colorizedSd.pixels[at(0, 0)] ==
                   document.colorizedPixel(2, Resolution::Sd, 10, 5) &&
               document.sprite(1).originalSd.pixels[at(1, 1)] == 255,
           "selected pixels in, the others transparent");
    expect(edit::spritePasteRefusal(document, 1, buffer, Resolution::Hd).has_value(),
           "not onto the HD plane of a sprite without HD");
    paste->revert(document);
    expect(sourceBefore != 2 && document.spriteSourceFrame(1) == sourceBefore,
           "undo puts the source frame back");
}

void testHdOnlyProject()
{
    ProjectDocument document = makeDocument(PlaneLayout::NativeExtra);
    expect(edit::spritePasteRefusal(document, 1, edit::copySprite(document, 0, Resolution::Hd),
                                    Resolution::Sd)
               .has_value(),
           "only the native plane");
    const CopyBuffer buffer = edit::copySprite(document, 0, Resolution::Hd);
    std::unique_ptr<EditCommand> paste =
        edit::pasteIntoSprite(document, 1, buffer, Resolution::Hd, false, false);
    expect(paste && document.sprite(1).hasHd &&
               document.sprite(1).originalSd.pixels == document.sprite(1).maskHd.pixels,
           "the original and the mask are the pasted shape");
}

}  // namespace

void runSpriteClipboardTests()
{
    testCopySprite();
    testPasteSpriteIntoSprite();
    testPasteFrameSelectionIntoSprite();
    testHdOnlyProject();
}
