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
using colorizing::document::UnsupportedLegacyPayload;
using colorizing::document::isValidProjectSnapshot;
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

    document.createHdFrame(0);
    expect(document.hasHdFrame(0), "createHdFrame should add HD frame");
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
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 1),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0x1010),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0x2020),
        7,
        5,
    });
    snapshot.comparisonMasks.push_back(makeIndexedFrame(colorizing::render::kSdFrameSize, 3));
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
    expect(document.shapeComparisonMode(0) == 7,
           "document shape comparison mode accessor");
    expect(document.comparisonMaskId(0) == 5,
           "document comparison mask ID accessor");
    expect(document.comparisonMaskCount() == 1,
           "document comparison mask count accessor");
    expect(document.comparisonMask(0).pixels.front() == 3,
           "document comparison mask accessor");
    expect(roundTrip.comparisonMasks.size() == 1, "snapshot comparison mask count round-trip");
    expect(roundTrip.comparisonMasks[0].pixels.front() == 3,
           "snapshot comparison mask data round-trip");
    expect(roundTrip.unsupportedPayloads.size() == 1, "snapshot unsupported payload round-trip");
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
    testInvalidSnapshotReportsReason();
}
