#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "render/CanvasLayout.h"

using colorizing::document::ProjectCreateOptions;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::BackgroundRecord;
using colorizing::document::FrameDynamicData;
using colorizing::document::FrameBackgroundData;
using colorizing::document::FrameSpriteReference;
using colorizing::document::SpriteRecord;
using colorizing::document::UnsupportedLegacyPayload;
using colorizing::document::isValidProjectSnapshot;
using colorizing::document::kMaxDynamicSetsPerFrame;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::render::Resolution;

namespace {

void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

template <typename ExceptionType, typename Func>
void expectThrows(Func func, const std::string& message)
{
    try {
        func();
    } catch (const ExceptionType&) {
        return;
    } catch (const std::exception& ex) {
        fail(message + " threw different exception: " + ex.what());
    }
    fail(message + " did not throw");
}

void testCreateEmptyProject()
{
    ProjectDocument document = ProjectDocument::createEmpty(ProjectCreateOptions{
        "afm_113b",
        2,
        false,
    });

    expect(document.romName() == "afm_113b", "ROM name should be stored");
    expect(document.frameCount() == 2, "frame count should match create options");
    expect(!document.isDirty(), "new empty document should be clean");
    expect(!document.hasHdFrame(0), "HD should be absent unless requested");
    expect(document.originalFrame(0).isValid(), "original SD frame should be valid");
    expect(document.colorizedFrame(0, Resolution::Sd).isValid(), "colorized SD frame should be valid");
    expect(document.colorizedFrame(0, Resolution::Sd).size.width == 128, "SD width");
    expect(document.colorizedFrame(0, Resolution::Sd).size.height == 32, "SD height");
}

void testCreateProjectWithHdFrames()
{
    ProjectDocument document = ProjectDocument::createEmpty(ProjectCreateOptions{
        "taf_l7",
        1,
        true,
    });

    expect(document.hasHdFrame(0), "HD should be present when requested");
    expect(document.colorizedFrame(0, Resolution::Hd).isValid(), "colorized HD frame should be valid");
    expect(document.colorizedFrame(0, Resolution::Hd).size.width == 256, "HD width");
    expect(document.colorizedFrame(0, Resolution::Hd).size.height == 64, "HD height");
    expect(!document.isDirty(), "factory-created HD frames should be clean");
}

void testCreateAndRemoveHdFrameDirtyState()
{
    ProjectDocument document = ProjectDocument::createEmpty(ProjectCreateOptions{
        "test",
        1,
        false,
    });
    document.setColorizedPixel(0, Resolution::Sd, 3, 2, 0x1234);
    document.markClean();

    document.createHdFrame(0);
    expect(document.hasHdFrame(0), "createHdFrame should add HD frame");
    expect(document.colorizedPixel(0, Resolution::Hd, 6, 4) == 0x1234,
           "createHdFrame should upscale SD pixels to HD");
    expect(document.colorizedPixel(0, Resolution::Hd, 7, 5) == 0x1234,
           "createHdFrame should fill the 2x2 HD block from the SD source pixel");
    expect(document.isDirty(), "creating HD frame should mark dirty");

    document.markClean();
    document.createHdFrame(0);
    expect(!document.isDirty(), "creating an already-present HD frame should be a no-op");

    document.removeHdFrame(0);
    expect(!document.hasHdFrame(0), "removeHdFrame should remove HD frame");
    expect(document.isDirty(), "removing HD frame should mark dirty");

    document.markClean();
    document.removeHdFrame(0);
    expect(!document.isDirty(), "removing an absent HD frame should be a no-op");
}

void testSetColorizedPixelsByResolution()
{
    ProjectDocument document = ProjectDocument::createEmpty(ProjectCreateOptions{
        "test",
        1,
        true,
    });

    document.setColorizedPixel(0, Resolution::Sd, 127, 31, 0x1234);
    expect(document.colorizedFrame(0, Resolution::Sd).pixels.back() == 0x1234,
           "SD pixel should be written at SD coordinates");
    expect(document.colorizedPixel(0, Resolution::Sd, 127, 31) == 0x1234,
           "SD pixel accessor should read edited color");
    expect(document.isDirty(), "SD pixel edit should mark dirty");

    document.markClean();
    document.setColorizedPixel(0, Resolution::Sd, 127, 31, 0x1234);
    expect(!document.isDirty(), "writing identical SD pixel should not mark dirty");

    document.setColorizedPixel(0, Resolution::Hd, 255, 63, 0xabcd);
    expect(document.colorizedFrame(0, Resolution::Hd).pixels.back() == 0xabcd,
           "HD pixel should be written at HD coordinates");
    expect(document.colorizedPixel(0, Resolution::Hd, 255, 63) == 0xabcd,
           "HD pixel accessor should read edited color");
    expect(document.isDirty(), "pixel edit should mark dirty");
}

void testMissingHdAndBoundsErrors()
{
    ProjectDocument document = ProjectDocument::createEmpty(ProjectCreateOptions{
        "test",
        1,
        false,
    });

    expectThrows<std::logic_error>(
        [&]() { (void)document.colorizedFrame(0, Resolution::Hd); },
        "missing HD read");
    expectThrows<std::logic_error>(
        [&]() { document.setColorizedPixel(0, Resolution::Hd, 0, 0, 1); },
        "missing HD write");
    expectThrows<std::out_of_range>(
        [&]() { document.setColorizedPixel(0, Resolution::Sd, 128, 0, 1); },
        "SD x bounds");
    expect(!document.isDirty(), "failed SD bounds write should not mark dirty");
    expectThrows<std::out_of_range>(
        [&]() { (void)document.colorizedPixel(0, Resolution::Sd, -1, 0); },
        "SD read x bounds");
    expectThrows<std::out_of_range>(
        [&]() { document.setColorizedPixel(1, Resolution::Sd, 0, 0, 1); },
        "frame index bounds");
}

void testUnsupportedPayloadPreservationHook()
{
    ProjectDocument document = ProjectDocument::createEmpty(ProjectCreateOptions{
        "test",
        1,
        false,
    });

    document.addUnsupportedPayload(UnsupportedLegacyPayload{"pup-pack", {1, 2, 3}});

    expect(document.unsupportedPayloads().size() == 1, "unsupported payload count");
    expect(document.unsupportedPayloads()[0].name == "pup-pack", "unsupported payload name");
    expect(document.unsupportedPayloads()[0].bytes.size() == 3, "unsupported payload bytes");
    expect(document.isDirty(), "adding unsupported payload should mark dirty");
}

void testSnapshotValidationAndRoundTrip()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "roundtrip";
    FrameDynamicData dynamicData{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0x3030),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0x4040),
    };
    dynamicData.maskSd.pixels[2] = 4;
    dynamicData.maskHd.pixels[3] = 5;
    FrameBackgroundData backgroundData{
        0,
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeIndexedFrame(colorizing::render::kHdFrameSize, 0),
    };
    backgroundData.maskSd.pixels[4] = 6;
    backgroundData.maskHd.pixels[5] = 7;
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 1),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0x1010),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0x2020),
        7,
        5,
        dynamicData,
        backgroundData,
    });
    snapshot.comparisonMasks.push_back(makeIndexedFrame(colorizing::render::kSdFrameSize, 3));
    snapshot.backgrounds.push_back(BackgroundRecord{
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0x5050),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0x6060),
    });
    SpriteRecord sprite{
        true,
        makeIndexedFrame({256, 64}, 9),
        makeRgb565Frame({256, 64}, 0x7070),
        makeIndexedFrame({256, 64}, 10),
        makeRgb565Frame({256, 64}, 0x8080),
        {0x12345678, 0xffffffff, 0xffffffff, 0xffffffff},
        {12, 0xffff, 0xffff, 0xffff},
        {{colorizing::document::SpriteDetectionArea{1, 2, 3, 4},
          colorizing::document::SpriteDetectionArea{},
          colorizing::document::SpriteDetectionArea{},
          colorizing::document::SpriteDetectionArea{}}},
        "Left saucer",
    };
    snapshot.sprites.push_back(sprite);
    snapshot.frames[0].spriteReferences.assign(32, FrameSpriteReference{});
    snapshot.frames[0].spriteReferences[0] = FrameSpriteReference{0, 5, 6, 7, 8};
    snapshot.unsupportedPayloads.push_back(UnsupportedLegacyPayload{"opaque", {9}});

    std::string errorMessage;
    expect(isValidProjectSnapshot(snapshot, &errorMessage), "valid snapshot should pass validation");

    const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    const ProjectSnapshot roundTrip = document.toSnapshot();

    expect(roundTrip.romName == "roundtrip", "snapshot ROM name round-trip");
    expect(roundTrip.frames.size() == 1, "snapshot frame count round-trip");
    expect(roundTrip.frames[0].originalSd.pixels.front() == 1, "snapshot original data round-trip");
    expect(roundTrip.frames[0].colorizedSd.pixels.front() == 0x1010,
           "snapshot SD colorized data round-trip");
    expect(roundTrip.frames[0].colorizedHd->pixels.front() == 0x2020,
           "snapshot HD colorized data round-trip");
    expect(roundTrip.frames[0].shapeComparisonMode == 7,
           "snapshot shape comparison mode round-trip");
    expect(roundTrip.frames[0].comparisonMaskId == 5,
           "snapshot comparison mask ID round-trip");
    expect(roundTrip.frames[0].dynamicData.has_value(),
           "snapshot dynamic data round-trip");
    expect(roundTrip.frames[0].dynamicData->maskSd.pixels[2] == 4,
           "snapshot SD dynamic mask data round-trip");
    expect(roundTrip.frames[0].dynamicData->maskHd.pixels[3] == 5,
           "snapshot HD dynamic mask data round-trip");
    expect(roundTrip.frames[0].dynamicData->colorsSd.front() == 0x3030,
           "snapshot SD dynamic color data round-trip");
    expect(roundTrip.frames[0].dynamicData->colorsHd.front() == 0x4040,
           "snapshot HD dynamic color data round-trip");
    expect(roundTrip.frames[0].backgroundData.has_value(),
           "snapshot background data round-trip");
    expect(roundTrip.frames[0].backgroundData->backgroundId == 0,
           "snapshot background ID round-trip");
    expect(roundTrip.frames[0].backgroundData->maskSd.pixels[4] == 6,
           "snapshot SD background mask round-trip");
    expect(roundTrip.frames[0].backgroundData->maskHd.pixels[5] == 7,
           "snapshot HD background mask round-trip");
    expect(roundTrip.backgrounds.size() == 1, "snapshot background count round-trip");
    expect(roundTrip.backgrounds[0].colorizedSd.pixels.front() == 0x5050,
           "snapshot SD background image round-trip");
    expect(roundTrip.backgrounds[0].colorizedHd->pixels.front() == 0x6060,
           "snapshot HD background image round-trip");
    expect(roundTrip.sprites.size() == 1, "snapshot sprite count round-trip");
    expect(roundTrip.sprites[0].hasHd, "snapshot sprite HD flag round-trip");
    expect(roundTrip.sprites[0].originalSd.pixels.front() == 9,
           "snapshot sprite original data round-trip");
    expect(roundTrip.sprites[0].colorizedSd.pixels.front() == 0x7070,
           "snapshot SD sprite color data round-trip");
    expect(roundTrip.sprites[0].maskHd.pixels.front() == 10,
           "snapshot HD sprite mask data round-trip");
    expect(roundTrip.sprites[0].colorizedHd.pixels.front() == 0x8080,
           "snapshot HD sprite color data round-trip");
    expect(roundTrip.sprites[0].detectionDwords[0] == 0x12345678,
           "snapshot sprite detection data round-trip");
    expect(roundTrip.sprites[0].name == "Left saucer",
           "snapshot sprite name round-trip");
    expect(roundTrip.frames[0].spriteReferences[0].maxY == 8,
           "snapshot frame sprite reference round-trip");
    expect(document.shapeComparisonMode(0) == 7,
           "document shape comparison mode accessor");
    expect(document.comparisonMaskId(0) == 5,
           "document comparison mask ID accessor");
    expect(document.comparisonMaskCount() == 1,
           "document comparison mask count accessor");
    expect(document.comparisonMask(0).pixels.front() == 3,
           "document comparison mask accessor");
    expect(document.comparisonMaskPixel(0, 0, 0) == 3,
           "document comparison mask pixel accessor");
    expect(document.backgroundCount() == 1,
           "document background count accessor");
    expect(document.background(0).colorizedSd.pixels.front() == 0x5050,
           "document background accessor");
    expect(document.backgroundFrame(0, Resolution::Sd).pixels.front() == 0x5050,
           "document SD background frame accessor");
    expect(document.backgroundFrame(0, Resolution::Hd).pixels.front() == 0x6060,
           "document HD background frame accessor");
    expect(document.backgroundId(0) == 0,
           "document background ID accessor");
    expect(document.backgroundMaskPixel(0, Resolution::Sd, 4, 0) == 6,
           "document SD background mask pixel accessor");
    expect(document.spriteCount() == 1,
           "document sprite count accessor");
    expect(document.sprite(0).colorizedSd.pixels.front() == 0x7070,
           "document sprite accessor");
    expect(document.sprite(0).name == "Left saucer",
           "document sprite name accessor");
    expect(document.frameSpriteReferences(0)[0].spriteId == 0,
           "document frame sprite reference accessor");
    expect(roundTrip.comparisonMasks.size() == 1, "snapshot comparison mask count round-trip");
    expect(roundTrip.comparisonMasks[0].pixels.front() == 3,
           "snapshot comparison mask data round-trip");
    expect(roundTrip.unsupportedPayloads.size() == 1, "snapshot unsupported payload round-trip");
}

void testComparisonMaskPixelEditing()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "mask_edit";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
        0,
        0,
    });
    snapshot.comparisonMasks.push_back(makeIndexedFrame(colorizing::render::kSdFrameSize, 0));

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.setComparisonMaskPixel(0, 4, 2, 1);
    expect(document.comparisonMaskPixel(0, 4, 2) == 1,
           "comparison mask pixel edit should be readable");
    expect(document.isDirty(), "comparison mask pixel edit should mark dirty");

    document.markClean();
    document.setComparisonMaskPixel(0, 4, 2, 1);
    expect(!document.isDirty(), "writing identical comparison mask pixel should not mark dirty");

    expectThrows<std::out_of_range>(
        [&]() { document.setComparisonMaskPixel(1, 0, 0, 1); },
        "comparison mask index bounds");
    expectThrows<std::out_of_range>(
        [&]() { (void)document.comparisonMaskPixel(0, 128, 0); },
        "comparison mask pixel bounds");
}

void testFrameComparisonMetadataEditing()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "mask_metadata";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
        0,
        255,
    });
    snapshot.comparisonMasks.push_back(makeIndexedFrame(colorizing::render::kSdFrameSize, 0));

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.setShapeComparisonMode(0, 1);
    expect(document.shapeComparisonMode(0) == 1,
           "shape comparison mode edit should be readable");
    expect(document.isDirty(), "shape comparison mode edit should mark dirty");

    document.markClean();
    document.setShapeComparisonMode(0, 1);
    expect(!document.isDirty(), "identical shape comparison mode edit should not mark dirty");
    expectThrows<std::invalid_argument>(
        [&]() { document.setShapeComparisonMode(0, 2); },
        "shape comparison mode value bounds");

    document.setComparisonMaskId(0, 0);
    expect(document.comparisonMaskId(0) == 0,
           "comparison mask ID edit should be readable");
    expect(document.isDirty(), "comparison mask ID edit should mark dirty");

    document.markClean();
    document.setComparisonMaskId(0, 0);
    expect(!document.isDirty(), "identical comparison mask ID edit should not mark dirty");

    expectThrows<std::out_of_range>(
        [&]() { document.setComparisonMaskId(1, 0); },
        "comparison mask ID frame bounds");
}

void testDynamicMaskPixelEditing()
{
    FrameDynamicData dynamicData{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0),
    };
    ProjectSnapshot snapshot;
    snapshot.romName = "dynamic_edit";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        dynamicData,
    });

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.setDynamicMaskPixel(0, Resolution::Sd, 4, 2, 3);
    expect(document.dynamicMaskPixel(0, Resolution::Sd, 4, 2) == 3,
           "SD dynamic mask pixel edit should be readable");
    expect(document.isDirty(), "dynamic mask pixel edit should mark dirty");

    document.markClean();
    document.setDynamicMaskPixel(0, Resolution::Sd, 4, 2, 3);
    expect(!document.isDirty(), "identical dynamic mask pixel edit should not mark dirty");

    document.setDynamicMaskPixel(0, Resolution::Hd, 8, 4, 5);
    expect(document.dynamicMaskPixel(0, Resolution::Hd, 8, 4) == 5,
           "HD dynamic mask pixel edit should be readable");

    document.markClean();
    document.setDynamicColor(0, Resolution::Sd, 2, 3, 0x7777);
    expect(document.dynamicColor(0, Resolution::Sd, 2, 3) == 0x7777,
           "SD dynamic color edit should be readable");
    expect(document.isDirty(), "dynamic color edit should mark dirty");

    document.markClean();
    document.setDynamicColor(0, Resolution::Sd, 2, 3, 0x7777);
    expect(!document.isDirty(), "identical dynamic color edit should not mark dirty");

    document.setDynamicColor(0, Resolution::Hd, 2, 3, 0x8888);
    expect(document.dynamicColor(0, Resolution::Hd, 2, 3) == 0x8888,
           "HD dynamic color edit should be readable");

    expectThrows<std::out_of_range>(
        [&]() { document.setDynamicMaskPixel(0, Resolution::Sd, 128, 0, 1); },
        "dynamic mask pixel bounds");
    expectThrows<std::out_of_range>(
        [&]() { document.setDynamicColor(0, Resolution::Sd, kMaxDynamicSetsPerFrame, 0, 1); },
        "dynamic color set bounds");

    ProjectDocument noDynamicData = ProjectDocument::createEmpty(ProjectCreateOptions{
        "empty_dynamic",
        1,
        true,
    });
    expectThrows<std::logic_error>(
        [&]() { noDynamicData.setDynamicMaskPixel(0, Resolution::Sd, 0, 0, 1); },
        "missing dynamic mask write");
    expectThrows<std::logic_error>(
        [&]() { noDynamicData.setDynamicColor(0, Resolution::Sd, 0, 0, 1); },
        "missing dynamic color write");
}

void testBackgroundMetadataAndMaskEditing()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "background_edit";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        std::nullopt,
        FrameBackgroundData{
            0xffff,
            makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
            makeIndexedFrame(colorizing::render::kHdFrameSize, 0),
        },
    });
    snapshot.backgrounds.push_back(BackgroundRecord{
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0x5050),
        std::nullopt,
    });

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.setBackgroundId(0, 0);
    expect(document.backgroundId(0) == 0, "background ID edit should be readable");
    expect(document.isDirty(), "background ID edit should mark dirty");

    document.markClean();
    document.setBackgroundId(0, 0);
    expect(!document.isDirty(), "identical background ID edit should not mark dirty");

    document.setBackgroundMaskPixel(0, Resolution::Sd, 4, 2, 1);
    expect(document.backgroundMaskPixel(0, Resolution::Sd, 4, 2) == 1,
           "SD background mask pixel edit should be readable");
    expect(document.isDirty(), "background mask edit should mark dirty");

    document.markClean();
    document.setBackgroundMaskPixel(0, Resolution::Hd, 8, 4, 2);
    expect(document.backgroundMaskPixel(0, Resolution::Hd, 8, 4) == 2,
           "HD background mask pixel edit should be readable");

    expectThrows<std::out_of_range>(
        [&]() { (void)document.background(1); },
        "background index bounds");
    expectThrows<std::out_of_range>(
        [&]() { document.setBackgroundMaskPixel(0, Resolution::Sd, 128, 0, 1); },
        "background mask pixel bounds");

    ProjectDocument noBackgroundData = ProjectDocument::createEmpty(ProjectCreateOptions{
        "empty_background",
        1,
        true,
    });
    expectThrows<std::logic_error>(
        [&]() { noBackgroundData.setBackgroundId(0, 0); },
        "missing background ID write");
    expectThrows<std::logic_error>(
        [&]() { noBackgroundData.setBackgroundMaskPixel(0, Resolution::Sd, 0, 0, 1); },
        "missing background mask write");
}

void testFrameSpriteReferenceEditing()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "sprite_assignment";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
        0,
        255,
        std::nullopt,
        std::nullopt,
        std::vector<FrameSpriteReference>(32),
    });
    snapshot.sprites.push_back(SpriteRecord{
        false,
        makeIndexedFrame({256, 64}, 255),
        makeRgb565Frame({256, 64}, 0),
        makeIndexedFrame({256, 64}, 255),
        makeRgb565Frame({256, 64}, 0),
        {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
        {0xffff, 0xffff, 0xffff, 0xffff},
        {{colorizing::document::SpriteDetectionArea{},
          colorizing::document::SpriteDetectionArea{},
          colorizing::document::SpriteDetectionArea{},
          colorizing::document::SpriteDetectionArea{}}},
    });
    const std::size_t spriteDwordPosition = 11 * 256 + 10;
    snapshot.sprites[0].originalSd.pixels[spriteDwordPosition] = 1;
    snapshot.sprites[0].originalSd.pixels[spriteDwordPosition + 1] = 2;
    snapshot.sprites[0].originalSd.pixels[spriteDwordPosition + 2] = 3;
    snapshot.sprites[0].originalSd.pixels[spriteDwordPosition + 3] = 0;

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.setFrameSpriteReference(0, 1, FrameSpriteReference{0, 2, 3, 4, 5});
    expect(document.frameSpriteReferences(0)[1].spriteId == 0,
           "frame sprite assignment should be readable");
    expect(document.frameSpriteReferences(0)[1].maxY == 5,
           "frame sprite bounding box should be readable");
    expect(document.isDirty(), "frame sprite assignment should mark dirty");

    document.markClean();
    document.setFrameSpriteReference(0, 1, FrameSpriteReference{0, 2, 3, 4, 5});
    expect(!document.isDirty(), "identical frame sprite assignment should not mark dirty");

    document.clearFrameSpriteReference(0, 1);
    expect(document.frameSpriteReferences(0)[1].spriteId == 255,
           "frame sprite clear should store empty sprite ID");
    expect(document.frameSpriteReferences(0)[1].minX == 2,
           "frame sprite clear should preserve bounding box min x");
    expect(document.frameSpriteReferences(0)[1].maxY == 5,
           "frame sprite clear should preserve bounding box max y");
    expect(document.isDirty(), "frame sprite clear should mark dirty");

    expectThrows<std::out_of_range>(
        [&]() { document.setFrameSpriteReference(0, 32, FrameSpriteReference{0, 0, 0, 1, 1}); },
        "sprite slot bounds");
    expectThrows<std::out_of_range>(
        [&]() { document.setFrameSpriteReference(0, 0, FrameSpriteReference{4, 0, 0, 1, 1}); },
        "sprite reference index bounds");

    document.markClean();
    document.setSpriteDetectionArea(0, 2, colorizing::document::SpriteDetectionArea{10, 11, 12, 13});
    expect(document.sprite(0).detectionAreas[2].x == 10,
           "sprite detection area edit should be readable");
    expect(document.sprite(0).detectionAreas[2].height == 13,
           "sprite detection area height edit should be readable");
    expect(document.sprite(0).detectionDwords[2] == 0x00030201,
           "sprite detection area edit should recalculate dword");
    expect(document.sprite(0).detectionDwordPositions[2] == spriteDwordPosition,
           "sprite detection area edit should recalculate dword position");
    expect(document.isDirty(), "sprite detection area edit should mark dirty");

    document.markClean();
    document.setSpriteDetectionArea(0, 2, colorizing::document::SpriteDetectionArea{10, 11, 12, 13});
    expect(!document.isDirty(), "identical sprite detection area edit should not mark dirty");

    document.setSpriteDetectionArea(0,
                                    2,
                                    colorizing::document::SpriteDetectionArea{
                                        0xffff,
                                        0xffff,
                                        0xffff,
                                        0xffff,
                                    });
    expect(document.sprite(0).detectionAreas[2].x == 0xffff,
           "sprite detection area clear should store disabled marker");
    expect(document.sprite(0).detectionDwords[2] == 0xffffffff,
           "sprite detection area clear should disable dword");
    expect(document.sprite(0).detectionDwordPositions[2] == 0xffff,
           "sprite detection area clear should disable dword position");

    expectThrows<std::out_of_range>(
        [&]() {
            document.setSpriteDetectionArea(1, 0, colorizing::document::SpriteDetectionArea{0, 0, 1, 1});
        },
        "sprite detection area sprite bounds");
    expectThrows<std::out_of_range>(
        [&]() {
            document.setSpriteDetectionArea(0, 4, colorizing::document::SpriteDetectionArea{0, 0, 1, 1});
        },
        "sprite detection area index bounds");
}

void testBackgroundImagePixelEditing()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "background_image_edit";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0),
        0,
        255,
        std::nullopt,
        FrameBackgroundData{
            0,
            makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
            makeIndexedFrame(colorizing::render::kHdFrameSize, 0),
        },
    });
    snapshot.backgrounds.push_back(BackgroundRecord{
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0x5050),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0x6060),
    });

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.setBackgroundPixel(0, Resolution::Sd, 4, 2, 0x1111);
    expect(document.backgroundPixel(0, Resolution::Sd, 4, 2) == 0x1111,
           "SD background pixel edit should be readable");
    expect(document.isDirty(), "background pixel edit should mark dirty");

    document.markClean();
    document.setBackgroundPixel(0, Resolution::Sd, 4, 2, 0x1111);
    expect(!document.isDirty(), "identical background pixel edit should not mark dirty");

    document.setBackgroundPixel(0, Resolution::Hd, 8, 4, 0x2222);
    expect(document.backgroundPixel(0, Resolution::Hd, 8, 4) == 0x2222,
           "HD background pixel edit should be readable");

    expectThrows<std::out_of_range>(
        [&]() { document.setBackgroundPixel(1, Resolution::Sd, 0, 0, 1); },
        "background image index bounds");
    expectThrows<std::out_of_range>(
        [&]() { document.setBackgroundPixel(0, Resolution::Sd, 128, 0, 1); },
        "background image pixel bounds");

    ProjectSnapshot sdOnlySnapshot = snapshot;
    sdOnlySnapshot.backgrounds[0].colorizedHd.reset();
    ProjectDocument sdOnlyDocument = ProjectDocument::fromSnapshot(sdOnlySnapshot);
    expectThrows<std::logic_error>(
        [&]() { sdOnlyDocument.setBackgroundPixel(0, Resolution::Hd, 0, 0, 1); },
           "missing HD background write");
}

void testHdRelatedDataUpscaling()
{
    FrameDynamicData dynamicData{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 255),
        makeIndexedFrame(colorizing::render::kHdFrameSize, 255),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0x1111),
        std::vector<uint16_t>(kMaxDynamicSetsPerFrame * 16, 0x2222),
    };
    dynamicData.maskSd.pixels[2 * 128 + 3] = 7;
    dynamicData.colorsSd[2 * 16 + 3] = 0x3333;

    FrameBackgroundData backgroundData{
        0,
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeIndexedFrame(colorizing::render::kHdFrameSize, 0),
    };
    backgroundData.maskSd.pixels[4 * 128 + 5] = 1;

    ProjectSnapshot snapshot;
    snapshot.romName = "hd_related_upscale";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 0),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
        0,
        255,
        dynamicData,
        backgroundData,
    });
    BackgroundRecord background{
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
    };
    background.colorizedSd.pixels[6 * 128 + 7] = 0x4444;
    snapshot.backgrounds.push_back(background);

    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();

    document.upscaleDynamicMaskToHd(0);
    expect(document.dynamicMaskPixel(0, Resolution::Hd, 6, 4) == 7,
           "HD dynamic mask should be upscaled from SD");
    expect(document.dynamicMaskPixel(0, Resolution::Hd, 7, 5) == 7,
           "HD dynamic mask should fill the 2x2 block from SD");
    expect(document.dynamicColor(0, Resolution::Hd, 2, 3) == 0x3333,
           "HD dynamic colors should copy SD dynamic colors");
    expect(document.isDirty(), "dynamic HD upscale should mark dirty");

    document.markClean();
    document.upscaleBackgroundMaskToHd(0);
    expect(document.backgroundMaskPixel(0, Resolution::Hd, 10, 8) == 1,
           "HD background mask should be upscaled from SD");
    expect(document.backgroundMaskPixel(0, Resolution::Hd, 11, 9) == 1,
           "HD background mask should fill the 2x2 block from SD");
    expect(document.isDirty(), "background mask HD upscale should mark dirty");

    document.markClean();
    document.upscaleAssignedBackgroundToHd(0);
    expect(document.backgroundFrame(0, Resolution::Hd).pixels[12 * 256 + 14] == 0x4444,
           "HD background image should be upscaled from SD");
    expect(document.backgroundFrame(0, Resolution::Hd).pixels[13 * 256 + 15] == 0x4444,
           "HD background image should fill the 2x2 block from SD");
    expect(document.isDirty(), "background image HD upscale should mark dirty");
}

void testInvalidSnapshotReportsReason()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "bad";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::Size{64, 32}, 1),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0),
        std::nullopt,
    });

    std::string errorMessage;
    expect(!isValidProjectSnapshot(snapshot, &errorMessage), "invalid snapshot should fail validation");
    expect(errorMessage == "frame 0 has invalid SD original data",
           "invalid snapshot should report the first bad field");
}

}  // namespace

void runProjectDocumentTests()
{
    testCreateEmptyProject();
    testCreateProjectWithHdFrames();
    testCreateAndRemoveHdFrameDirtyState();
    testSetColorizedPixelsByResolution();
    testMissingHdAndBoundsErrors();
    testUnsupportedPayloadPreservationHook();
    testSnapshotValidationAndRoundTrip();
    testComparisonMaskPixelEditing();
    testFrameComparisonMetadataEditing();
    testDynamicMaskPixelEditing();
    testBackgroundMetadataAndMaskEditing();
    testFrameSpriteReferenceEditing();
    testBackgroundImagePixelEditing();
    testHdRelatedDataUpscaling();
    testInvalidSnapshotReportsReason();
}
