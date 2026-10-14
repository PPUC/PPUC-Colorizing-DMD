#include "edit/SpriteClipboard.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/PlaneSelection.h"
#include "edit/StateCommands.h"

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

void testSpriteDynamicEditing()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary);
    const document::SpriteRecord& sprite = document.sprite(0);
    // An opaque and a transparent pixel of sprite 0.
    int opaqueX = -1, opaqueY = -1, clearX = -1, clearY = -1;
    for (int y = 0; y < 8 && opaqueX < 0; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (sprite.originalSd.pixels[at(x, y)] != 255) {
                opaqueX = x;
                opaqueY = y;
                break;
            }
        }
    }
    for (int y = 0; y < 8 && clearX < 0; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (sprite.originalSd.pixels[at(x, y)] == 255) {
                clearX = x;
                clearY = y;
                break;
            }
        }
    }
    expect(opaqueX >= 0 && clearX >= 0, "the fixture sprite has both kinds of pixels");

    edit::SpriteState before = edit::spriteState(document, 0);
    const std::size_t changed = document.setSpriteDynamicMaskPixels(
        0, {{opaqueX, opaqueY}, {clearX, clearY}}, 3);
    expect(changed == 1 && document.sprite(0).dynamicMaskSd.pixels[at(opaqueX, opaqueY)] == 3 &&
               document.sprite(0).dynamicMaskSd.pixels[at(clearX, clearY)] == 255,
           "only covered pixels become dynamic");
    expect([&]() {
        try {
            (void)document.setSpriteDynamicMaskPixels(0, {{opaqueX, opaqueY}}, 9);
        } catch (const std::out_of_range&) {
            return true;
        }
        return false;
    }(), "a sprite has 9 dynamic sets");
    document.setSpriteDynamicColor(0, 3, 2, 0xabcd);
    expect(document.spriteDynamicColor(0, 3, 2) == 0xabcd, "a set's color");
    document.setSpriteShapeMode(0, true);
    std::unique_ptr<EditCommand> command = edit::spriteStateCommand(document, 0, before);
    expect(command && command->replacedData().empty(),
           "a dynamic or shape change keeps the sprite's pixel histories");
    command->revert(document);
    expect(document.sprite(0).dynamicMaskSd.pixels[at(opaqueX, opaqueY)] == 255 &&
               document.spriteDynamicColor(0, 3, 2) != 0xabcd && document.sprite(0).shapeMode == 0,
           "undo puts all of it back");

    // In an HD-only project the sprite's dynamic content is on the native plane.
    ProjectDocument native = makeDocument(PlaneLayout::NativeExtra);
    native.setSpriteDynamicColor(0, 1, 0, 0x1111);
    expect(native.sprite(0).dynamicColorsHd[1 * native.colorCount()] == 0x1111,
           "native plane colors");
}

}  // namespace

void runSpriteClipboardTests()
{
    testCopySprite();
    testPasteSpriteIntoSprite();
    testPasteFrameSelectionIntoSprite();
    testHdOnlyProject();
    testSpriteDynamicEditing();
}
