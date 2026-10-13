// A synthetic project with 192x64 original frames and a stored 96x32 extra plane
// (render::PlaneLayout::NativeOriginal): validation, serialization, the document, and that
// the 96x32 data is discarded on load and written as "no extra data".

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacyProjectSerializer.h"
#include "legacy/LegacySnapshotBuilder.h"
#include "render/ProjectGeometry.h"

using colorizing::document::BackgroundRecord;
using colorizing::document::FrameBackgroundData;
using colorizing::document::FrameDynamicData;
using colorizing::document::IndexedFrame;
using colorizing::document::PaletteRecord;
using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::Rgb565Frame;
using colorizing::document::SpriteRecord;
using colorizing::document::isValidProjectSnapshot;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::legacy::LegacySectionOffsets;
using colorizing::legacy::legacyFrameHash;
using colorizing::legacy::legacyOriginalColors;
using colorizing::render::PlaneLayout;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;

namespace {

constexpr uint32_t kColorCount = 4;
constexpr Size kNativeSize{192, 64};
constexpr Size kExtraSize{96, 32};
constexpr Size kSpriteSize{256, 64};

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: 192x64 project: " << message << '\n';
        std::exit(1);
    }
}

std::size_t nativeIndex(int x, int y)
{
    return static_cast<std::size_t>(y) * 192 + static_cast<std::size_t>(x);
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

SpriteRecord makeSprite()
{
    SpriteRecord sprite;
    sprite.hasHd = true;
    sprite.originalSd = makeIndexedFrame(kSpriteSize, 255);
    sprite.originalSd.pixels[0] = 1;
    sprite.colorizedSd = makeRgb565Frame(kSpriteSize, 0x0101);
    sprite.maskHd = makeIndexedFrame(kSpriteSize, 255);
    sprite.maskHd.pixels[1] = 0;
    sprite.colorizedHd = makeRgb565Frame(kSpriteSize, 0x0202);
    sprite.detectionDwords.assign(4, 0xffffffff);
    sprite.detectionDwordPositions.assign(4, 0xffff);
    sprite.detectionAreas.assign(4, {});
    sprite.name = "Bolt";
    sprite.dynamicMaskSd = makeIndexedFrame(kSpriteSize, 255);
    sprite.dynamicMaskHd = makeIndexedFrame(kSpriteSize, 255);
    sprite.dynamicMaskHd.pixels[4] = 5;
    sprite.dynamicColorsSd.assign(9 * kColorCount, 0x0303);
    sprite.dynamicColorsHd.assign(9 * kColorCount, 0x0404);
    return sprite;
}

// Frame 0: plain and short, with 96x32 pixels left behind a cleared extra flag.
// Frame 1: colorized at the last native pixel, flagged extra frame, dynamic content on both
//          planes, a background, a sprite slot, rotations on both planes.
// Frame 2: plain but long enough to be played.
// Frame 3: plain colors, one dynamic pixel beyond column 127.
ProjectSnapshot makeSnapshot()
{
    const std::vector<uint16_t> originalColors = legacyOriginalColors(kColorCount);
    ProjectSnapshot snapshot;
    snapshot.romName = "native192";
    snapshot.colorCount = kColorCount;
    snapshot.cromRevision = 20;
    snapshot.geometry = ProjectGeometry(PlaneLayout::NativeOriginal);

    snapshot.comparisonMasks.assign(2, makeIndexedFrame(kNativeSize, 0));
    snapshot.comparisonMasks[1].pixels[nativeIndex(190, 63)] = 1;

    ProjectFrameSnapshot plain;
    plain.originalSd = makeIndexedFrame(kNativeSize, 2);
    plain.colorizedSd = makeRgb565Frame(kNativeSize, originalColors[2]);
    plain.comparisonMaskId = 255;
    plain.triggerId = 0xffffffff;
    plain.durationMs = 10;
    FrameDynamicData noDynamic;
    noDynamic.maskSd = makeIndexedFrame(kNativeSize, 255);
    noDynamic.maskHd = makeIndexedFrame(kExtraSize, 255);
    noDynamic.colorsSd.assign(32 * kColorCount, 0);
    noDynamic.colorsHd.assign(32 * kColorCount, 0x0606);
    noDynamic.shadowDirectionsSd.assign(32, 0);
    noDynamic.shadowColorsSd.assign(32, 0);
    noDynamic.shadowDirectionsHd.assign(32, 0);
    noDynamic.shadowColorsHd.assign(32, 0);
    plain.dynamicData = noDynamic;
    FrameBackgroundData noBackground;
    noBackground.maskSd = makeIndexedFrame(kNativeSize, 0);
    noBackground.maskHd = makeIndexedFrame(kExtraSize, 0);
    plain.backgroundData = noBackground;
    plain.spriteReferences.assign(32, {255, 0, 0, 191, 63});
    plain.colorRotationsSd.assign(4 * 64, 0);
    plain.colorRotationsHd.assign(4 * 64, 0);
    snapshot.frames.assign(4, plain);

    snapshot.frames[0].inactiveHd = makeRgb565Frame(kExtraSize, 0x7777);

    ProjectFrameSnapshot& rich = snapshot.frames[1];
    rich.originalSd.pixels[nativeIndex(191, 63)] = 3;
    rich.originalSd.pixels[nativeIndex(190, 63)] = 1;
    rich.colorizedSd.pixels[nativeIndex(191, 63)] = 0x1234;
    rich.colorizedHd = makeRgb565Frame(kExtraSize, 0x4321);
    rich.colorizedHd->pixels[6] = 0;
    rich.comparisonMaskId = 1;
    rich.dynamicData->maskSd.pixels[nativeIndex(150, 40)] = 1;
    rich.dynamicData->colorsSd[1 * kColorCount + 2] = 0x0505;
    // The colorized pixel under a dynamic mask is blanked on save, on the extra plane too.
    rich.dynamicData->maskHd.pixels[6] = 2;
    rich.dynamicData->shadowDirectionsHd[2] = 0x02;
    rich.dynamicData->shadowColorsHd[2] = 0x0808;
    rich.backgroundData->backgroundId = 0;
    rich.backgroundData->maskSd.pixels[nativeIndex(191, 0)] = 1;
    rich.backgroundData->maskHd.pixels[95] = 1;
    rich.spriteReferences[0] = {0, 130, 0, 191, 63};
    rich.triggerId = 42;
    rich.durationMs = 100;
    rich.colorRotationsSd[0] = 2;
    rich.colorRotationsSd[1] = 50;
    rich.colorRotationsHd[0] = 3;
    rich.colorRotationsHd[1] = 70;
    rich.colorRotationsHd[2] = 0x0c0c;

    snapshot.frames[2].durationMs = 20;
    snapshot.frames[3].dynamicData->maskSd.pixels[nativeIndex(128, 33)] = 0;

    BackgroundRecord flagged;
    flagged.colorizedSd = makeRgb565Frame(kNativeSize, 0x0909);
    flagged.colorizedHd = makeRgb565Frame(kExtraSize, 0x0a0a);
    BackgroundRecord leftover;
    leftover.colorizedSd = makeRgb565Frame(kNativeSize, 0x0b0b);
    leftover.inactiveHd = makeRgb565Frame(kExtraSize, 0x0d0d);
    snapshot.backgrounds = {flagged, leftover};
    snapshot.sprites.push_back(makeSprite());
    snapshot.palettes.assign(256, PaletteRecord{std::vector<uint16_t>(64, 0), ""});
    return snapshot;
}

struct SerializedProject {
    std::vector<uint8_t> crom;
    std::vector<uint8_t> crp;
    LegacySectionOffsets cromSections;
};

SerializedProject serialize(const ProjectSnapshot& snapshot)
{
    SerializedProject project;
    std::ostringstream crom;
    std::ostringstream crp;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(crom, snapshot, error, &project.cromSections),
           "cROM should serialize: " + error);
    expect(colorizing::legacy::writeLegacyCrp(crp, snapshot, error),
           "cRP should serialize: " + error);
    const std::string cromData = crom.str();
    const std::string crpData = crp.str();
    project.crom.assign(cromData.begin(), cromData.end());
    project.crp.assign(crpData.begin(), crpData.end());
    return project;
}

ProjectSnapshot parse(const SerializedProject& project)
{
    const auto crom = colorizing::legacy::parseModernLegacyCromFrames(project.crom);
    expect(crom.ok, "serialized cROM should parse: " + crom.errorMessage);
    expect(crom.nextOffset == project.crom.size(), "parser should consume the whole cROM");
    const auto crp =
        colorizing::legacy::parseLegacyCrpOriginalFrames(project.crp, crom.frameData.metadata);
    expect(crp.ok, "serialized cRP should parse: " + crp.errorMessage);
    expect(crp.nextOffset == project.crp.size(), "parser should consume the whole cRP");
    const auto built = colorizing::legacy::buildProjectSnapshot(crom.frameData, crp.originalData);
    expect(built.ok, "serialized project should build a snapshot: " + built.errorMessage);
    return built.snapshot;
}

std::size_t sectionIndex(const LegacySectionOffsets& sections, const std::string& name)
{
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].first == name) {
            return i;
        }
    }
    expect(false, "missing section: " + name);
    return 0;
}

std::size_t sectionOffset(const SerializedProject& project, const std::string& name)
{
    return project.cromSections[sectionIndex(project.cromSections, name)].second;
}

std::vector<uint8_t> sectionBytes(const SerializedProject& project, const std::string& name)
{
    const std::size_t index = sectionIndex(project.cromSections, name);
    return std::vector<uint8_t>(
        project.crom.begin() + static_cast<std::ptrdiff_t>(project.cromSections[index].second),
        project.crom.begin() +
            static_cast<std::ptrdiff_t>(project.cromSections[index + 1].second));
}

uint32_t readU32(const std::vector<uint8_t>& bytes, std::size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint16_t readU16(const std::vector<uint8_t>& bytes, std::size_t offset)
{
    return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

// Every block of the file that belongs to the extra plane.
const std::vector<std::string>& extraSections()
{
    static const std::vector<std::string> names = {
        "extra frame flags",
        "HD colorized frames",
        "HD dynamic masks",
        "HD dynamic colors",
        "extra sprite flags",
        "HD sprite masks",
        "HD sprite colors",
        "HD color rotations",
        "extra background flags",
        "HD backgrounds",
        "HD background masks",
        "HD dynamic shadow directions",
        "HD dynamic shadow colors",
        "HD sprite dynamic colors",
        "HD sprite dynamic masks",
    };
    return names;
}

// The value every byte of an extra-plane block has for "no extra data".
uint8_t neutralExtraByte(const std::string& name)
{
    if (name == "HD dynamic masks" || name == "HD sprite masks" ||
        name == "HD sprite dynamic masks") {
        return 255;
    }
    return name == "HD background masks" ? 1 : 0;
}

void expectNeutralExtraSections(const SerializedProject& project, const std::string& when)
{
    std::size_t extraCount = 0;
    for (const auto& section : project.cromSections) {
        extraCount += colorizing::legacy::isLegacyExtraPlaneSection(section.first) ? 1 : 0;
    }
    expect(extraCount == extraSections().size(), "every extra-plane block is known as one");
    for (const std::string& name : extraSections()) {
        const std::vector<uint8_t> bytes = sectionBytes(project, name);
        expect(!bytes.empty() &&
                   bytes == std::vector<uint8_t>(bytes.size(), neutralExtraByte(name)),
               "\"" + name + "\" should be written as no extra data " + when);
    }
}

void expectSameOriginalSections(const SerializedProject& expected,
                                const SerializedProject& actual,
                                const std::string& when)
{
    expect(expected.crom.size() == actual.crom.size(), "the cROM keeps its size " + when);
    for (std::size_t i = 0; i + 1 < expected.cromSections.size(); ++i) {
        const std::string& name = expected.cromSections[i].first;
        if (!colorizing::legacy::isLegacyExtraPlaneSection(name)) {
            expect(sectionBytes(expected, name) == sectionBytes(actual, name),
                   "\"" + name + "\" should be unchanged " + when);
        }
    }
    expect(expected.crp == actual.crp, "the cRP should be unchanged " + when);
}

void testSnapshotValidation()
{
    std::string error;
    expect(isValidProjectSnapshot(makeSnapshot(), &error), "the fixture is valid: " + error);

    const auto rejected = [](void (*change)(ProjectSnapshot&)) {
        ProjectSnapshot snapshot = makeSnapshot();
        change(snapshot);
        return !isValidProjectSnapshot(snapshot);
    };
    expect(rejected([](ProjectSnapshot& s) {
               s.frames[0].originalSd = makeIndexedFrame({128, 32});
           }),
           "a 128x32 original frame is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.frames[0].colorizedSd = makeRgb565Frame({128, 32});
           }),
           "a 128x32 colorized frame is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.frames[1].colorizedHd = makeRgb565Frame({256, 64});
           }),
           "a 256x64 extra frame is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.frames[1].colorizedHd = makeRgb565Frame({192, 64});
           }),
           "an extra frame of the native size is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.frames[0].dynamicData->maskHd = makeIndexedFrame({192, 64}, 255);
           }),
           "an extra dynamic mask of the native size is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.frames[0].backgroundData->maskSd = makeIndexedFrame({128, 32});
           }),
           "a 128x32 background mask is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.comparisonMasks[0] = makeIndexedFrame({128, 32});
           }),
           "a 128x32 comparison mask is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.backgrounds[0].colorizedSd = makeRgb565Frame({256, 64});
           }),
           "a 256x64 background is rejected");
    expect(rejected([](ProjectSnapshot& s) { s.geometry = ProjectGeometry(); }),
           "192x64 data in an ordinary project is rejected");
    expect(rejected([](ProjectSnapshot& s) {
               s.geometry = ProjectGeometry(PlaneLayout::NativeExtra);
           }),
           "192x64 data in a 256x64 project is rejected");

    ProjectSnapshot older = makeSnapshot();
    older.cromRevision = 19;
    expect(isValidProjectSnapshot(older), "192x64 does not need header revision 20");
}

void testHeaderAndBlockSizes()
{
    const ProjectSnapshot snapshot = makeSnapshot();
    const SerializedProject project = serialize(snapshot);
    expect(readU32(project.crom, 68) == 192 && readU32(project.crom, 72) == 64 &&
               readU32(project.crom, 76) == 96 && readU32(project.crom, 80) == 32,
           "header carries 192x64 and 96x32");
    expect(readU32(project.crom, 64 + 9 * 4 + 2) == 0, "the 256x64 flag is not set");

    const auto size = [&](const std::string& name) { return sectionBytes(project, name).size(); };
    const std::size_t frames = 4;
    expect(size("comparison masks") == 2 * 192 * 64, "comparison masks are 192x64");
    expect(size("SD colorized frames") == frames * 192 * 64 * 2, "original plane frames");
    expect(size("HD colorized frames") == frames * 96 * 32 * 2, "extra plane frames");
    expect(size("SD dynamic masks") == frames * 192 * 64 &&
               size("HD dynamic masks") == frames * 96 * 32,
           "dynamic masks of both planes");
    expect(size("SD backgrounds") == 2 * 192 * 64 * 2 && size("HD backgrounds") == 2 * 96 * 32 * 2,
           "backgrounds of both planes");
    expect(size("SD background masks") == frames * 192 * 64 &&
               size("HD background masks") == frames * 96 * 32,
           "background masks of both planes");
    expect(project.crp.size() > 64 + frames * 192 * 64, "the cRP holds 192x64 originals");

    const std::size_t boxes = sectionOffset(project, "frame sprite bounding boxes");
    expect(readU16(project.crom, boxes + 4) == 191 && readU16(project.crom, boxes + 6) == 63,
           "an empty slot spans the 192x64 frame");
    const std::size_t richBox = boxes + 32 * 8;
    expect(readU16(project.crom, richBox) == 130 && readU16(project.crom, richBox + 4) == 191,
           "slot bounds beyond column 127 are written as they are");
}

void testDerivedFields()
{
    const std::vector<uint16_t> originalColors = legacyOriginalColors(kColorCount);
    ProjectSnapshot snapshot = makeSnapshot();
    const SerializedProject project = serialize(snapshot);

    const std::size_t hashes = sectionOffset(project, "hash codes");
    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        const ProjectFrameSnapshot& frame = snapshot.frames[i];
        const IndexedFrame* mask =
            frame.comparisonMaskId == 255 ? nullptr
                                          : &snapshot.comparisonMasks[frame.comparisonMaskId];
        expect(readU32(project.crom, hashes + i * 4) ==
                   legacyFrameHash(frame.originalSd, mask, false),
               "hash of frame " + std::to_string(i) + " covers the 192x64 original and mask");
    }
    const uint32_t richHash = readU32(project.crom, hashes + 4);
    {
        ProjectSnapshot changed = snapshot;
        changed.frames[1].originalSd.pixels[nativeIndex(191, 63)] = 2;
        expect(readU32(serialize(changed).crom, hashes + 4) != richHash,
               "the last native pixel is part of the hash");
        changed = snapshot;
        changed.frames[1].originalSd.pixels[nativeIndex(190, 63)] = 2;
        expect(readU32(serialize(changed).crom, hashes + 4) == richHash,
               "a pixel under the 192x64 comparison mask is not");
    }

    const std::vector<uint8_t> active = sectionBytes(project, "active frame flags");
    expect(active == std::vector<uint8_t>({0, 1, 1, 1}),
           "plain short frame inactive; colorized, long and dynamic frames active");
    expect(!colorizing::legacy::isLegacyFrameActive(snapshot.frames[0],
                                                    originalColors,
                                                    Resolution::Sd),
           "leftover 96x32 pixels do not make a frame colorized");
    {
        ProjectFrameSnapshot frame = snapshot.frames[0];
        frame.colorizedHd = makeRgb565Frame(kExtraSize, 0x4321);
        frame.dynamicData->maskHd.pixels[3] = 1;
        expect(!colorizing::legacy::isLegacyFrameActive(frame, originalColors, Resolution::Sd),
               "the 96x32 frame and its dynamic mask do not count");
        frame.colorizedSd.pixels[nativeIndex(191, 63)] = 0x0001;
        expect(colorizing::legacy::isLegacyFrameActive(frame, originalColors, Resolution::Sd),
               "one native pixel that differs from the plain original counts");
    }

    const std::size_t sdFrames = sectionOffset(project, "SD colorized frames");
    const std::size_t richFrame = sdFrames + 192 * 64 * 2;
    expect(readU16(project.crom, richFrame + nativeIndex(191, 63) * 2) == 0x1234,
           "the colorized last native pixel is written");
    expect(readU16(project.crom, richFrame + nativeIndex(150, 40) * 2) == 0,
           "a native pixel under the dynamic mask is blanked");
    expect(readU16(project.crom, richFrame + nativeIndex(149, 40) * 2) == originalColors[2],
           "its neighbour is not");
}

// The serializer writes a snapshot as it is; the fixture with its 96x32 data stands for a file
// the Win32 editor saved.
void testSerializerWritesExtraPlaneOfSnapshot()
{
    const SerializedProject project = serialize(makeSnapshot());
    expect(sectionBytes(project, "extra frame flags") == std::vector<uint8_t>({0, 1, 0, 0}) &&
               sectionBytes(project, "extra background flags") == std::vector<uint8_t>({1, 0}) &&
               sectionBytes(project, "extra sprite flags") == std::vector<uint8_t>({1}),
           "the fixture file has extra flags set");
    const std::size_t hdFrames = sectionOffset(project, "HD colorized frames");
    expect(readU16(project.crom, hdFrames) == 0x7777 &&
               readU16(project.crom, hdFrames + 96 * 32 * 2) == 0x4321,
           "the fixture file has 96x32 pixels");
}

void testExtraPlaneIsDiscardedOnLoad()
{
    const SerializedProject project = serialize(makeSnapshot());

    const ProjectSnapshot loaded = parse(project);
    expect(loaded.geometry == ProjectGeometry(PlaneLayout::NativeOriginal),
           "the geometry is derived from the header");
    expect(loaded.frames[1].originalSd.size == kNativeSize &&
               loaded.frames[1].colorizedSd.size == kNativeSize &&
               loaded.comparisonMasks[1].size == kNativeSize,
           "reloaded sizes");
    for (const ProjectFrameSnapshot& frame : loaded.frames) {
        expect(!frame.colorizedHd && !frame.inactiveHd, "no 96x32 frame is loaded");
    }
    expect(!loaded.backgrounds[0].colorizedHd && !loaded.backgrounds[1].inactiveHd &&
               !loaded.sprites[0].hasHd,
           "no 96x32 background or sprite is loaded");
    expect(loaded.frames[1].dynamicData->maskHd.size == kExtraSize &&
               loaded.frames[1].dynamicData->maskHd.pixels[6] == 255 &&
               loaded.frames[1].dynamicData->colorsHd[0] == 0 &&
               loaded.frames[1].dynamicData->shadowDirectionsHd[2] == 0 &&
               loaded.frames[1].dynamicData->shadowColorsHd[2] == 0 &&
               loaded.frames[1].backgroundData->maskHd.size == kExtraSize &&
               loaded.frames[1].backgroundData->maskHd.pixels[0] == 1 &&
               loaded.frames[1].colorRotationsHd[0] == 0 &&
               loaded.sprites[0].maskHd.pixels[1] == 255 &&
               loaded.sprites[0].colorizedHd.pixels[0] == 0 &&
               loaded.sprites[0].dynamicMaskHd.pixels[4] == 255 &&
               loaded.sprites[0].dynamicColorsHd[0] == 0,
           "the loaded extra arrays hold the values of an item without extra resolution");
    std::string error;
    expect(isValidProjectSnapshot(loaded, &error), "the loaded snapshot is valid: " + error);

    const SerializedProject saved = serialize(loaded);
    expect(readU32(saved.crom, 68) == 192 && readU32(saved.crom, 72) == 64 &&
               readU32(saved.crom, 76) == 96 && readU32(saved.crom, 80) == 32,
           "the saved header still carries 192x64 and 96x32");
    expectNeutralExtraSections(saved, "after a load");
    expectSameOriginalSections(project, saved, "by discarding the 96x32 data");

    const SerializedProject again = serialize(parse(saved));
    expect(again.crom == saved.crom && again.crp == saved.crp,
           "a second load and save is byte-identical to the first");
}

void testDocumentDiscardsExtraPlane()
{
    const ProjectSnapshot snapshot = makeSnapshot();
    const SerializedProject project = serialize(snapshot);
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);

    expect(document.editsOriginalPlaneOnly() && !document.rendersHdPlaneOnly(),
           "the document edits the original plane only");
    expect(document.originalFrameSize() == kNativeSize &&
               document.colorizedFrame(1, Resolution::Sd).size == kNativeSize,
           "the edited plane is 192x64");
    expect(!document.hasHdFrame(0) && !document.hasHdFrame(1) && !document.hasHdSprite(0) &&
               !document.background(0).colorizedHd && !document.background(1).inactiveHd &&
               !document.frameHdGaps(1).any(),
           "nothing of the 96x32 plane shows as HD");
    expect(document.dynamicData(1)->maskHd.pixels.empty() &&
               document.dynamicData(1)->colorsHd.empty() &&
               document.backgroundData(1)->maskHd.pixels.empty() &&
               document.sprite(0).maskHd.pixels.empty() &&
               document.sprite(0).colorizedHd.pixels.empty() &&
               document.sprite(0).dynamicMaskHd.pixels.empty() &&
               document.sprite(0).dynamicColorsHd.empty(),
           "the document holds no 96x32 data");
    {
        const ProjectSnapshot unedited = document.toSnapshot();
        std::string error;
        expect(isValidProjectSnapshot(unedited, &error),
               "the unedited snapshot is valid: " + error);
        const SerializedProject saved = serialize(unedited);
        expectNeutralExtraSections(saved, "by an unedited document");
        expectSameOriginalSections(project, saved, "by an unedited document");
        const SerializedProject loadedAndSaved = serialize(parse(project));
        expect(saved.crom == loadedAndSaved.crom && saved.crp == loadedAndSaved.crp,
               "the document and the loader write the same file");
        const SerializedProject again =
            serialize(ProjectDocument::fromSnapshot(parse(saved)).toSnapshot());
        expect(again.crom == saved.crom && again.crp == saved.crp,
               "a second save is byte-identical to the first");
    }

    const Resolution extra = Resolution::Hd;
    expect(throwsLogicError([&]() { (void)document.colorizedFrame(1, extra); }) &&
               throwsLogicError([&]() { document.setColorizedPixel(1, extra, 0, 0, 1); }) &&
               throwsLogicError([&]() { (void)document.dynamicMaskPixel(1, extra, 0, 0); }) &&
               throwsLogicError([&]() { document.setDynamicMaskPixel(1, extra, 0, 0, 1); }) &&
               throwsLogicError([&]() { document.setDynamicColor(1, extra, 0, 0, 1); }) &&
               throwsLogicError([&]() { document.setBackgroundMaskPixel(1, extra, 0, 0, 1); }) &&
               throwsLogicError([&]() { document.setColorRotationLength(1, extra, 0, 4); }) &&
               throwsLogicError([&]() { (void)document.colorRotations(1, extra); }) &&
               throwsLogicError([&]() { (void)document.backgroundFrame(0, extra); }) &&
               throwsLogicError([&]() {
                   document.setBackgroundFrame(0, extra, makeRgb565Frame(kExtraSize));
               }) &&
               throwsLogicError([&]() { (void)document.spriteColorizedPixel(0, extra, 0, 0); }),
           "the 96x32 plane cannot be reached");
    expect(throwsLogicError([&]() { document.createHdFrame(0); }) &&
               throwsLogicError([&]() { document.createHdSprite(0); }) &&
               throwsLogicError([&]() { document.upscaleBackgroundMaskToHd(1); }) &&
               throwsLogicError([&]() { document.upscaleAssignedBackgroundToHd(1); }),
           "no 96x32 data can be created");
    document.removeHdFrame(1);
    document.removeHdSprite(0);
    document.removeBackgroundHdFrame(0);
    expect(!document.isDirty(), "refused and empty operations leave the document clean");

    // Edit everything the native plane has, at coordinates beyond 128x32.
    document.setColorizedPixel(0, Resolution::Sd, 191, 63, 0x2222);
    document.setComparisonMaskPixel(0, 191, 63, 1);
    document.setDynamicMaskPixel(1, Resolution::Sd, 191, 63, 3);
    document.setDynamicColor(1, Resolution::Sd, 3, 1, 0x3333);
    document.setDynamicShadowDirection(1, Resolution::Sd, 3, 0x04);
    document.setBackgroundMaskPixel(1, Resolution::Sd, 190, 62, 1);
    document.setBackgroundPixel(0, Resolution::Sd, 191, 63, 0x4444);
    document.setColorRotationLength(1, Resolution::Sd, 1, 5);
    document.setFrameSpriteReference(2, 3, {0, 129, 33, 191, 63});
    Rgb565Frame newBackground = makeRgb565Frame(kNativeSize, 0x5555);
    document.setBackgroundFrame(1, Resolution::Sd, newBackground);
    bool refused = false;
    try {
        document.setBackgroundFrame(1, Resolution::Sd, makeRgb565Frame({128, 32}));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, "a 128x32 background image is refused");
    document.replaceSpriteFromFrame(0,
                                    document.originalFrame(1),
                                    document.colorizedFrame(1, Resolution::Sd),
                                    188,
                                    60,
                                    4,
                                    4);
    expect(document.sprite(0).originalSd.pixels[3 * 256 + 3] == 3 &&
               document.sprite(0).colorizedSd.pixels[3 * 256 + 3] == 0x1234 &&
               !document.sprite(0).hasHd,
           "a sprite is captured from native pixels beyond column 127, without HD");

    const ProjectSnapshot edited = document.toSnapshot();
    std::string error;
    expect(isValidProjectSnapshot(edited, &error), "the edited snapshot is valid: " + error);
    expect(edited.frames[0].colorizedSd.pixels[nativeIndex(191, 63)] == 0x2222 &&
               edited.comparisonMasks[0].pixels[nativeIndex(191, 63)] == 1 &&
               edited.frames[1].dynamicData->maskSd.pixels[nativeIndex(191, 63)] == 3 &&
               edited.frames[1].dynamicData->colorsSd[3 * kColorCount + 1] == 0x3333 &&
               edited.frames[1].dynamicData->shadowDirectionsSd[3] == 0x04 &&
               edited.frames[1].backgroundData->maskSd.pixels[nativeIndex(190, 62)] == 1 &&
               edited.backgrounds[0].colorizedSd.pixels[nativeIndex(191, 63)] == 0x4444 &&
               edited.backgrounds[1].colorizedSd.pixels[0] == 0x5555 &&
               edited.frames[1].colorRotationsSd[64] == 5 &&
               edited.frames[2].spriteReferences[3].minX == 129,
           "native edits are in the snapshot");
    expect(!edited.frames[1].colorizedHd && !edited.frames[0].inactiveHd &&
               !edited.backgrounds[0].colorizedHd && !edited.backgrounds[1].inactiveHd &&
               !edited.sprites[0].hasHd,
           "no extra flag or leftover comes back");
    const SerializedProject saved = serialize(edited);
    expectNeutralExtraSections(saved, "after edits of the native plane");
    for (const char* name : {"SD colorized frames", "comparison masks", "SD sprite colors"}) {
        expect(sectionBytes(saved, name) != sectionBytes(project, name),
               std::string("the native block \"") + name + "\" carries the edits");
    }
    expect(sectionBytes(saved, "active frame flags") == std::vector<uint8_t>({1, 1, 1, 1}),
           "painting the last native pixel of frame 0 activates it");

    // What was edited on the native plane comes back from the file.
    const ProjectSnapshot reloaded = parse(saved);
    expect(reloaded.frames[0].colorizedSd.pixels[nativeIndex(191, 63)] == 0x2222 &&
               reloaded.comparisonMasks[0].pixels[nativeIndex(191, 63)] == 1 &&
               reloaded.frames[1].dynamicData->maskSd.pixels[nativeIndex(191, 63)] == 3 &&
               reloaded.frames[1].backgroundData->maskSd.pixels[nativeIndex(190, 62)] == 1 &&
               reloaded.backgrounds[1].colorizedSd.pixels[0] == 0x5555 &&
               reloaded.sprites[0].colorizedSd.pixels[3 * 256 + 3] == 0x1234,
           "original-plane data round-trips through the file");
    const SerializedProject again =
        serialize(ProjectDocument::fromSnapshot(reloaded).toSnapshot());
    expect(again.crom == saved.crom && again.crp == saved.crp,
           "saving the edited project a second time is byte-identical");
}

void testUnsupportedHeaderGeometryIsRefused()
{
    const SerializedProject project = serialize(makeSnapshot());
    auto crom = colorizing::legacy::parseModernLegacyCromFrames(project.crom);
    const auto crp =
        colorizing::legacy::parseLegacyCrpOriginalFrames(project.crp, crom.frameData.metadata);
    expect(crom.ok && crp.ok, "the fixture parses");

    crom.frameData.metadata.hdFrameWidth = 384;
    crom.frameData.metadata.hdFrameHeight = 128;
    auto built = colorizing::legacy::buildProjectSnapshot(crom.frameData, crp.originalData);
    expect(!built.ok &&
               built.errorMessage.find("unsupported frame geometry 192x64 with an extra plane "
                                       "of 384x128") != std::string::npos,
           "192x64 with a doubled extra plane is refused with its sizes: " + built.errorMessage);

    crom.frameData.metadata.hdFrameWidth = 96;
    crom.frameData.metadata.hdFrameHeight = 32;
    crom.frameData.metadata.originalIs256x64 = true;
    built = colorizing::legacy::buildProjectSnapshot(crom.frameData, crp.originalData);
    expect(!built.ok, "192x64 with the 256x64 flag is refused");

    crom.frameData.metadata.originalIs256x64 = false;
    built = colorizing::legacy::buildProjectSnapshot(crom.frameData, crp.originalData);
    expect(built.ok, "the untouched header is accepted: " + built.errorMessage);
}

}  // namespace

void runNativeOriginalProjectTests()
{
    testSnapshotValidation();
    testHeaderAndBlockSizes();
    testDerivedFields();
    testSerializerWritesExtraPlaneOfSnapshot();
    testExtraPlaneIsDiscardedOnLoad();
    testDocumentDiscardsExtraPlane();
    testUnsupportedHeaderGeometryIsRefused();
}
