// Edit batches: steps on many frames and records, undone as one step.

#include "edit/EditBatch.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"

namespace {

using namespace colorizing;
using document::ProjectDocument;
using render::Resolution;
namespace fixtures = runtime_fixtures;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: edit batch: " << message << '\n';
        std::exit(1);
    }
}

bool samePixels(const document::Rgb565Frame& a, const document::Rgb565Frame& b)
{
    if (a.pixels.size() != b.pixels.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.pixels.size(); ++index) {
        if (a.pixels[index] != b.pixels[index]) {
            return false;
        }
    }
    return true;
}

bool hasRotations(const ProjectDocument& document, std::size_t frame)
{
    for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
        if (document.colorRotationLength(frame, Resolution::Sd, slot) > 0) {
            return true;
        }
    }
    return false;
}

// Frame 1: HD, comparison mask, background 2; frame 2: dynamic zones; frame 4: background 0;
// frame 5: color rotations (RuntimeFixtures::makeFullProject).
void testSteps()
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    const ProjectDocument before = ProjectDocument::fromSnapshot(document.toSnapshot());
    const document::IndexedFrame& original = document.originalFrame(1);
    const std::vector<uint16_t> plain = document::plainOriginalColors(document.colorCount());

    edit::UndoHistories histories;
    // An earlier edit of frame 1, whose history the batch replaces.
    histories.push(edit::HistoryKey{edit::EditKind::FramePixels, 1, Resolution::Sd},
                   std::make_unique<edit::PixelEditCommand>(
                       edit::EditKind::FramePixels, 1, Resolution::Sd,
                       std::vector<edit::PixelChange>{}));

    edit::EditBatch batch("Strip");
    // Keep the first row of the ROM frame.
    std::vector<uint8_t> keep(original.pixelCount(), 0);
    for (int x = 0; x < original.size.width; ++x) {
        keep[static_cast<std::size_t>(x)] = 1;
    }
    batch.add(edit::resetPixels(document, 1, keep, std::nullopt));
    const document::Rgb565Frame& sd = document.colorizedFrame(1, Resolution::Sd);
    const document::Rgb565Frame& hd = document.colorizedFrame(1, Resolution::Hd);
    const int width = original.size.width;
    expect(sd.pixels[5] == before.colorizedFrame(1, Resolution::Sd).pixels[5],
           "a kept pixel keeps its color");
    expect(sd.pixels[static_cast<std::size_t>(3 * width + 7)] ==
               plain[original.pixels[static_cast<std::size_t>(3 * width + 7)]],
           "the others get the original's plain color");
    expect(hd.pixels[static_cast<std::size_t>(2 * width + 9)] ==
               before.colorizedFrame(1, Resolution::Hd).pixels[static_cast<std::size_t>(2 * width + 9)],
           "an HD pixel over a kept ROM pixel keeps its color");
    expect(hd.pixels[static_cast<std::size_t>(2 * (2 * width) * 3 + 14)] ==
               plain[original.pixels[static_cast<std::size_t>(3 * width + 7)]],
           "an HD pixel follows the ROM pixel under it");
    expect(!edit::resetPixels(document, 1, keep, std::nullopt), "twice changes nothing");
    batch.add(edit::resetPixels(document, 6, keep, uint16_t{0}));
    expect(document.colorizedFrame(6, Resolution::Sd).pixels[static_cast<std::size_t>(3 * width + 7)] == 0 &&
               document.colorizedFrame(6, Resolution::Hd).pixels[static_cast<std::size_t>(2 * (2 * width) * 3 + 14)] == 0 &&
               document.colorizedFrame(6, Resolution::Sd).pixels[5] ==
                   before.colorizedFrame(6, Resolution::Sd).pixels[5],
           "a fill color instead of the plain look");

    const render::Size dynamicSize = document.geometry().planeSize(document.dynamicPlane());
    batch.add(edit::setDynamicZones(
        document, 2,
        std::vector<uint8_t>(static_cast<std::size_t>(dynamicSize.width) * dynamicSize.height,
                             255)));
    expect(!document.hasDynamicPixels(2), "the dynamic zones are gone");

    batch.add(edit::setFrameBackground(document, 1, document::kNoBackgroundId));
    expect(document.backgroundId(1) == document::kNoBackgroundId, "the frame has no background");
    batch.add(edit::deleteBackground(document, 0));
    expect(document.backgroundCount() == 2 && document.backgroundId(4) == document::kNoBackgroundId,
           "a background is deleted");
    batch.add(edit::clearColorRotations(document, 5));
    expect(!hasRotations(document, 5), "the rotations are gone");
    expect(!edit::clearColorRotations(document, 5), "no rotations, nothing to clear");

    bool thrown = false;
    try {
        (void)edit::resetPixels(document, 1, std::vector<uint8_t>(3, 0), std::nullopt);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    expect(thrown, "a keep mask of the wrong size is refused");

    std::unique_ptr<edit::EditBatchCommand> command = batch.finish();
    expect(command && command->name() == "Strip" && command->stepCount() == 6 &&
               command->changesStructure(),
           "the batch is one command of six steps");
    histories.push(edit::structureHistoryKey(), std::move(command));
    expect(!histories.canUndo(edit::HistoryKey{edit::EditKind::FramePixels, 1, Resolution::Sd}),
           "the earlier history of an edited frame is dropped");

    expect(histories.undo(edit::structureHistoryKey(), document) != nullptr, "one undo");
    expect(samePixels(document.colorizedFrame(1, Resolution::Sd),
                      before.colorizedFrame(1, Resolution::Sd)) &&
               samePixels(document.colorizedFrame(1, Resolution::Hd),
                          before.colorizedFrame(1, Resolution::Hd)) &&
               document.hasDynamicPixels(2) && document.backgroundCount() == 3 &&
               document.backgroundId(1) == 2 && document.backgroundId(4) == 0 &&
               hasRotations(document, 5),
           "takes back every step");
    expect(histories.redo(edit::structureHistoryKey(), document) != nullptr &&
               document.backgroundCount() == 2 && !document.hasDynamicPixels(2) &&
               !hasRotations(document, 5),
           "one redo makes them again");
}

void testCancel()
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    edit::EditBatch batch("Nothing");
    expect(batch.isEmpty() && !edit::EditBatch("Empty").finish(), "an empty batch is no command");
    batch.add(edit::clearColorRotations(document, 5));
    batch.add(edit::deleteBackground(document, 2));
    batch.cancel(document);
    expect(hasRotations(document, 5) && document.backgroundCount() == 3 &&
               document.backgroundId(1) == 2 && batch.isEmpty(),
           "cancel takes the steps back");
}

// A picture of the plane's size: the color of each pixel from its position.
std::vector<uint16_t> patternPicture(render::Size size)
{
    std::vector<uint16_t> pixels(static_cast<std::size_t>(size.width) *
                                 static_cast<std::size_t>(size.height));
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = static_cast<uint16_t>(0x1000 + index % 4000);
    }
    return pixels;
}

// Writes only the left half.
std::vector<uint8_t> leftHalf(render::Size size)
{
    std::vector<uint8_t> write(static_cast<std::size_t>(size.width) *
                               static_cast<std::size_t>(size.height));
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width / 2; ++x) {
            write[static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                  static_cast<std::size_t>(x)] = 1;
        }
    }
    return write;
}

void testPictures()
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    const render::Size hd = document.geometry().planeSize(Resolution::Hd);
    const std::vector<uint16_t> picture = patternPicture(hd);
    const std::vector<uint8_t> write = leftHalf(hd);

    // A frame with background data and no HD (frame 4 has background 0).
    const std::size_t frame = 4;
    expect(!document.hasHdFrame(frame) && document.backgroundData(frame).has_value(),
           "the fixture's frame 4 has a background and no HD");
    const document::Rgb565Frame sdBefore = document.colorizedFrame(frame, Resolution::Sd);
    bool refused = false;
    try {
        static_cast<void>(edit::setFramePicture(document, frame, Resolution::Hd,
                                                std::vector<uint16_t>(10), {}));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && !document.hasHdFrame(frame), "a picture of another size is refused");

    std::unique_ptr<edit::EditCommand> command =
        edit::setFramePicture(document, frame, Resolution::Hd, picture, write);
    expect(command && document.hasHdFrame(frame), "the frame gets HD for the picture");
    const document::Rgb565Frame& hdFrame = document.colorizedFrame(frame, Resolution::Hd);
    const auto at = [](const document::Rgb565Frame& plane, int x, int y) {
        return plane.pixels[static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(plane.size.width) +
                            static_cast<std::size_t>(x)];
    };
    expect(at(hdFrame, 3, 5) == picture[5 * 256 + 3] &&
               at(hdFrame, 200, 5) == at(sdBefore, 100, 2),
           "the picture where the mask writes, Create HD's doubling elsewhere");
    expect(document.backgroundData(frame)->maskHd.isValid() &&
               document.backgroundData(frame)->maskHd.pixels[5 * 256 + 3] ==
                   document.backgroundData(frame)->maskSd.pixels[2 * 128 + 1],
           "with its background mask doubled");
    expect(samePixels(document.colorizedFrame(frame, Resolution::Sd), sdBefore),
           "the SD picture stays");
    command->revert(document);
    expect(!document.hasHdFrame(frame), "undone, the frame has no HD again");
    command->apply(document);
    expect(document.hasHdFrame(frame) &&
               at(document.colorizedFrame(frame, Resolution::Hd), 3, 5) == picture[5 * 256 + 3],
           "and redone");
    expect(!edit::setFramePicture(document, frame, Resolution::Hd, picture, write),
           "the same picture again changes nothing");

    // A background without HD gets an HD image: the picture, black where it does not write.
    const std::size_t background = 0;
    document.removeBackgroundHdFrame(background);
    command = edit::setBackgroundPicture(document, background, Resolution::Hd, picture, write);
    expect(command && document.background(background).colorizedHd &&
               at(document.backgroundFrame(background, Resolution::Hd), 3, 5) ==
                   picture[5 * 256 + 3] &&
               at(document.backgroundFrame(background, Resolution::Hd), 200, 5) == 0,
           "a background's new HD image");
    command->revert(document);
    expect(!document.background(background).colorizedHd, "undone, it has none again");
    command = edit::setBackgroundPicture(document, background, Resolution::Sd,
                                         patternPicture(document.geometry().planeSize(
                                             Resolution::Sd)),
                                         {});
    expect(command && at(document.backgroundFrame(background, Resolution::Sd), 7, 1) ==
                          static_cast<uint16_t>(0x1000 + 1 * 128 + 7),
           "the SD picture, written everywhere");
    command->revert(document);

    // A sprite without HD gets it, its shape doubled, its colors from the picture.
    const render::Size spriteSize{static_cast<int>(document::kMaxSpriteWidth),
                                  static_cast<int>(document::kMaxSpriteHeight)};
    std::size_t sprite = document.spriteCount();
    for (std::size_t index = 0; index < document.spriteCount(); ++index) {
        if (!document.hasHdSprite(index)) {
            sprite = index;
            break;
        }
    }
    expect(sprite < document.spriteCount(), "the fixture has a sprite without HD");
    const document::SpriteRecord spriteBefore = document.sprite(sprite);
    const edit::PictureEdit spritePicture{edit::PictureKind::Sprite, sprite, Resolution::Hd,
                                          patternPicture(spriteSize), {}};
    expect(edit::checkPictureEdit(document, spritePicture).width == spriteSize.width,
           "a sprite's picture has the sprite's size");
    command = edit::setPicture(document, spritePicture);
    expect(command && document.hasHdSprite(sprite) &&
               document.spriteColorizedPixel(sprite, Resolution::Hd, 9, 3) ==
                   spritePicture.pixels[3 * 256 + 9] &&
               document.sprite(sprite).maskHd.pixels[3 * 256 + 9] ==
                   spriteBefore.originalSd.pixels[1 * 256 + 4],
           "the sprite's HD colors, with Create HD's shape");
    command->revert(document);
    expect(!document.hasHdSprite(sprite), "undone, the sprite has no HD again");

    edit::PictureEdit missing = spritePicture;
    missing.index = document.spriteCount();
    refused = false;
    try {
        static_cast<void>(edit::checkPictureEdit(document, missing));
    } catch (const std::out_of_range&) {
        refused = true;
    }
    expect(refused, "a sprite that does not exist is refused");
}

}  // namespace

void runEditBatchTests()
{
    testSteps();
    testCancel();
    testPictures();
}
