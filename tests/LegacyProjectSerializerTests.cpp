#include "legacy/LegacyProjectSerializer.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacySnapshotBuilder.h"
#include "render/CanvasLayout.h"

using colorizing::document::FrameBackgroundData;
using colorizing::document::FrameDynamicData;
using colorizing::document::IndexedFrame;
using colorizing::document::PaletteRecord;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::SpriteRecord;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::legacy::isLegacyFrameActive;
using colorizing::legacy::kSkipFrameDurationMs;
using colorizing::legacy::LegacySectionOffsets;
using colorizing::legacy::legacyFrameHash;
using colorizing::legacy::legacyOriginalColors;
using colorizing::legacy::writeLegacyCrom;
using colorizing::legacy::writeLegacyCrp;
using colorizing::render::kHdFrameSize;
using colorizing::render::kSdFrameSize;

namespace {

constexpr uint32_t kColorCount = 4;

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

std::vector<uint8_t> toBytes(const std::ostringstream& stream)
{
    const std::string data = stream.str();
    return std::vector<uint8_t>(data.begin(), data.end());
}

std::size_t sectionOffset(const LegacySectionOffsets& sections, const std::string& name)
{
    for (const auto& section : sections) {
        if (section.first == name) {
            return section.second;
        }
    }
    fail("missing section: " + name);
    return 0;
}

uint32_t readU32(const std::vector<uint8_t>& bytes, std::size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

SpriteRecord makeSprite()
{
    const colorizing::render::Size spriteSize{256, 64};
    SpriteRecord sprite;
    sprite.hasHd = true;
    sprite.originalSd = makeIndexedFrame(spriteSize, 255);
    sprite.originalSd.pixels[0] = 1;
    sprite.colorizedSd = makeRgb565Frame(spriteSize, 0x0101);
    sprite.maskHd = makeIndexedFrame(spriteSize, 255);
    sprite.colorizedHd = makeRgb565Frame(spriteSize, 0x0202);
    sprite.detectionDwords = {0x01020304, 0xffffffff, 0xffffffff, 0xffffffff};
    sprite.detectionDwordPositions = {7, 0xffff, 0xffff, 0xffff};
    sprite.detectionAreas.assign(4, {});
    sprite.detectionAreas[0] = {1, 2, 3, 4};
    sprite.name = "Ball";
    sprite.dynamicMaskSd = makeIndexedFrame(spriteSize, 255);
    sprite.dynamicMaskSd.pixels[3] = 2;
    sprite.dynamicMaskHd = makeIndexedFrame(spriteSize, 255);
    sprite.dynamicMaskHd.pixels[4] = 5;
    sprite.dynamicColorsSd.assign(9 * kColorCount, 0x0303);
    sprite.dynamicColorsHd.assign(9 * kColorCount, 0x0404);
    sprite.shapeMode = 1;
    return sprite;
}

// Frame 0: plain, uncolorized and too short to be played. Frame 1: fully featured.
ProjectSnapshot makeSnapshot()
{
    const std::vector<uint16_t> originalColors = legacyOriginalColors(kColorCount);
    ProjectSnapshot snapshot;
    snapshot.romName = "serial_rom";
    snapshot.colorCount = kColorCount;
    snapshot.cromRevision = 19;

    IndexedFrame mask = makeIndexedFrame(kSdFrameSize, 0);
    mask.pixels[0] = 1;
    snapshot.comparisonMasks.push_back(mask);

    ProjectFrameSnapshot plain;
    plain.originalSd = makeIndexedFrame(kSdFrameSize, 2);
    plain.colorizedSd = makeRgb565Frame(kSdFrameSize, originalColors[2]);
    plain.comparisonMaskId = 255;
    plain.triggerId = 0xffffffff;
    plain.durationMs = 10;
    snapshot.frames.push_back(plain);

    ProjectFrameSnapshot rich;
    rich.originalSd = makeIndexedFrame(kSdFrameSize, 3);
    rich.originalSd.pixels[0] = 0;
    rich.originalSd.pixels[1] = 2;
    rich.colorizedSd = makeRgb565Frame(kSdFrameSize, 0x1234);
    rich.colorizedHd = makeRgb565Frame(kHdFrameSize, 0x4321);
    rich.shapeComparisonMode = 1;
    rich.comparisonMaskId = 0;
    FrameDynamicData dynamicData;
    dynamicData.maskSd = makeIndexedFrame(kSdFrameSize, 255);
    dynamicData.maskSd.pixels[5] = 1;
    dynamicData.maskHd = makeIndexedFrame(kHdFrameSize, 255);
    dynamicData.maskHd.pixels[6] = 2;
    dynamicData.colorsSd.assign(32 * kColorCount, 0x0505);
    dynamicData.colorsHd.assign(32 * kColorCount, 0x0606);
    dynamicData.shadowDirectionsSd.assign(32, 0x01);
    dynamicData.shadowColorsSd.assign(32, 0x0707);
    dynamicData.shadowDirectionsHd.assign(32, 0x02);
    dynamicData.shadowColorsHd.assign(32, 0x0808);
    rich.dynamicData = dynamicData;
    FrameBackgroundData backgroundData;
    backgroundData.backgroundId = 0;
    backgroundData.maskSd = makeIndexedFrame(kSdFrameSize, 1);
    backgroundData.maskHd = makeIndexedFrame(kHdFrameSize, 1);
    rich.backgroundData = backgroundData;
    rich.spriteReferences.assign(32, {});
    rich.spriteReferences[0] = {0, 1, 2, 30, 20};
    rich.triggerId = 42;
    rich.durationMs = 100;
    rich.colorRotationsSd.assign(4 * 64, 0);
    rich.colorRotationsSd[0] = 2;
    rich.colorRotationsSd[1] = 50;
    rich.colorRotationsHd.assign(4 * 64, 0);
    snapshot.frames.push_back(rich);

    snapshot.backgrounds.push_back({makeRgb565Frame(kSdFrameSize, 0x0909),
                                    makeRgb565Frame(kHdFrameSize, 0x0a0a)});
    snapshot.sprites.push_back(makeSprite());

    snapshot.palettes.assign(256, PaletteRecord{std::vector<uint16_t>(64, 0), ""});
    snapshot.palettes[0].colors[5] = 0xbeef;
    snapshot.palettes[1].name = "Warm";

    auto& metadata = snapshot.editorMetadata;
    metadata.sectionCount = 1;
    metadata.sectionFirstFrames[0] = 1;
    colorizing::document::setFixedName(metadata.sectionNames, 0, 32, "Attract");
    colorizing::document::setFixedName(metadata.maskNames, 0, 32, "Score");
    metadata.spriteSourceFrames[0] = 1;
    metadata.editColors[3] = 0x0b0b;
    metadata.importedPalettes.assign(colorizing::document::kImportedPaletteBytes, 9);
    metadata.timeElapsedMs = 123456;
    metadata.hasPupPack = 1;
    metadata.pupPackName = "pack";
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
    expect(writeLegacyCrom(crom, snapshot, error, &project.cromSections),
           "cROM should serialize: " + error);
    expect(writeLegacyCrp(crp, snapshot, error), "cRP should serialize: " + error);
    project.crom = toBytes(crom);
    project.crp = toBytes(crp);
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
    const auto snapshot = colorizing::legacy::buildProjectSnapshot(crom.frameData, crp.originalData);
    expect(snapshot.ok, "serialized project should build a snapshot: " + snapshot.errorMessage);
    return snapshot.snapshot;
}

void testFrameHashMatchesReferenceCrc()
{
    // "123456789" is the standard CRC-32 check input.
    IndexedFrame frame;
    frame.size = {9, 1};
    frame.pixels = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    expect(legacyFrameHash(frame, nullptr, false) == 0xCBF43926, "plain frame hash");

    IndexedFrame mask;
    mask.size = {9, 1};
    mask.pixels = {0, 0, 0, 0, 0, 0, 0, 0, 1};
    IndexedFrame shorter;
    shorter.size = {8, 1};
    shorter.pixels = {'1', '2', '3', '4', '5', '6', '7', '8'};
    expect(legacyFrameHash(frame, &mask, false) == legacyFrameHash(shorter, nullptr, false),
           "masked pixels should not contribute to the hash");

    IndexedFrame shapeA;
    shapeA.size = {3, 1};
    shapeA.pixels = {0, 2, 3};
    IndexedFrame shapeB;
    shapeB.size = {3, 1};
    shapeB.pixels = {0, 1, 1};
    expect(legacyFrameHash(shapeA, nullptr, true) == legacyFrameHash(shapeB, nullptr, false),
           "shape mode should fold colors above 1");
}

void testOriginalColorsMatchWin32Ramp()
{
    const std::vector<uint16_t> colors = legacyOriginalColors(4);
    expect(colors.size() == 4, "original color count");
    expect(colors[0] == 0 && colors[3] == ((31 << 11) | (31 << 5)), "original color ramp ends");
    expect(colors[1] == ((10 << 11) | (10 << 5)), "original color ramp step");
}

void testSerializedProjectRoundTrips()
{
    const SerializedProject first = serialize(makeSnapshot());
    const ProjectSnapshot reloaded = parse(first);
    const SerializedProject second = serialize(reloaded);

    expect(first.crom == second.crom, "cROM should serialize identically after a reload");
    expect(first.crp == second.crp, "cRP should serialize identically after a reload");

    expect(reloaded.cromRevision == 19, "revision should survive");
    expect(reloaded.sprites[0].dynamicMaskSd.pixels[3] == 2, "sprite SD dynamic mask");
    expect(reloaded.sprites[0].dynamicMaskHd.pixels[4] == 5, "sprite HD dynamic mask");
    expect(reloaded.sprites[0].dynamicColorsSd.front() == 0x0303, "sprite SD dynamic colors");
    expect(reloaded.sprites[0].dynamicColorsHd.front() == 0x0404, "sprite HD dynamic colors");
    expect(reloaded.sprites[0].shapeMode == 1, "sprite shape mode");
    expect(reloaded.sprites[0].name == "Ball", "sprite name");
    expect(reloaded.palettes[0].colors[5] == 0xbeef, "palette color");
    expect(reloaded.palettes[1].name == "Warm", "palette name");
    expect(reloaded.frames[1].durationMs == 100, "frame duration");
    expect(reloaded.frames[1].spriteReferences[0].maxX == 30, "frame sprite bounding box");
    expect(reloaded.frames[0].spriteReferences[0].spriteId == 255 &&
               reloaded.frames[0].spriteReferences[0].maxX == 127,
           "frames without sprite data get empty full-frame slots");

    const auto& metadata = reloaded.editorMetadata;
    expect(metadata.sectionCount == 1 && metadata.sectionFirstFrames[0] == 1, "sections");
    expect(colorizing::document::fixedName(metadata.sectionNames, 0, 32) == "Attract",
           "section name");
    expect(colorizing::document::fixedName(metadata.maskNames, 0, 32) == "Score", "mask name");
    expect(metadata.spriteSourceFrames[0] == 1, "sprite source frame");
    expect(metadata.editColors[3] == 0x0b0b, "edit colors");
    expect(metadata.importedPalettes.size() == colorizing::document::kImportedPaletteBytes,
           "imported palettes");
    expect(metadata.timeElapsedMs == 123456, "elapsed time");
    expect(metadata.hasPupPack == 1 && metadata.pupPackName == "pack", "PuP pack");
}

void testDerivedFieldsAreRecomputed()
{
    const ProjectSnapshot snapshot = makeSnapshot();
    const SerializedProject project = serialize(snapshot);

    const std::size_t hashes = sectionOffset(project.cromSections, "hash codes");
    expect(readU32(project.crom, hashes) ==
               legacyFrameHash(snapshot.frames[0].originalSd, nullptr, false),
           "unmasked frame hash");
    expect(readU32(project.crom, hashes + 4) ==
               legacyFrameHash(snapshot.frames[1].originalSd, &snapshot.comparisonMasks[0], true),
           "masked shape-mode frame hash");

    const std::size_t active = sectionOffset(project.cromSections, "active frame flags");
    expect(project.crom[active] == 0, "short uncolorized frame should be inactive");
    expect(project.crom[active + 1] == 1, "colorized frame should be active");

    ProjectSnapshot colorized = snapshot;
    colorized.frames[0].colorizedSd.pixels[10] = 0x5555;
    const SerializedProject colorizedProject = serialize(colorized);
    expect(colorizedProject.crom[active] == 1, "colorizing a short frame should activate it");

    const ProjectSnapshot reloaded = parse(project);
    expect(reloaded.frames[1].colorizedSd.pixels[5] == 0, "SD pixel under dynamic mask is blanked");
    expect(reloaded.frames[1].colorizedSd.pixels[4] == 0x1234, "static SD pixel is kept");
    expect(reloaded.frames[1].colorizedHd->pixels[6] == 0, "HD pixel under dynamic mask is blanked");
}

void testRevisionTwentyHeaderRoundTrips()
{
    ProjectSnapshot snapshot = makeSnapshot();
    snapshot.cromRevision = 20;
    const SerializedProject project = serialize(snapshot);
    expect(readU32(project.crom, 64) == 80, "revision 20 header length");
    expect(sectionOffset(project.cromSections, "hash codes") == 64 + 9 * 4 + 2 + 4,
           "revision 20 header carries the 256x64 flag");
    expect(parse(project).cromRevision == 20, "revision 20 should survive a reload");

    snapshot.cromRevision = 15;
    expect(readU32(serialize(snapshot).crom, 64) == 76,
           "older revisions are written as revision 19");
}

void test256x64OriginalsRoundTrip()
{
    const std::vector<uint16_t> originalColors = legacyOriginalColors(kColorCount);
    ProjectSnapshot snapshot = makeSnapshot();
    snapshot.cromRevision = 20;
    snapshot.geometry =
        colorizing::render::ProjectGeometry(colorizing::render::PlaneLayout::NativeExtra);
    snapshot.comparisonMasks[0] = makeIndexedFrame(kHdFrameSize, 0);
    snapshot.comparisonMasks[0].pixels[300] = 1;
    for (ProjectFrameSnapshot& frame : snapshot.frames) {
        frame.originalSd = makeIndexedFrame(kHdFrameSize, 2);
    }
    // Frame 0 has no HD frame, frame 1 an HD frame that is the plain original, frame 2 one
    // with a single colorized pixel at an odd position. All SD frames differ from the original.
    snapshot.frames.resize(3, snapshot.frames[0]);
    for (ProjectFrameSnapshot& frame : snapshot.frames) {
        frame.durationMs = 10;
        frame.dynamicData.reset();
        frame.colorizedSd = makeRgb565Frame(kSdFrameSize, 0x1234);
        frame.colorizedHd = makeRgb565Frame(kHdFrameSize, originalColors[2]);
    }
    snapshot.frames[0].colorizedHd.reset();
    snapshot.frames[0].originalSd.pixels[2] = 3;
    snapshot.frames[2].colorizedHd->pixels[256 + 3] = 0x4321;

    const SerializedProject project = serialize(snapshot);
    expect(readU32(project.crom, 64 + 9 * 4 + 2) == 1, "256x64 flag should be written");
    const std::size_t active = sectionOffset(project.cromSections, "active frame flags");
    expect(project.crom[active] == 0,
           "a 256x64 frame without HD is not colorized, whatever its SD frame holds");
    expect(project.crom[active + 1] == 0,
           "an HD frame equal to the plain 256x64 original is not colorized");
    expect(project.crom[active + 2] == 1,
           "an HD frame that differs at one native pixel is colorized");
    {
        ProjectFrameSnapshot dynamic = snapshot.frames[1];
        FrameDynamicData dynamicData;
        dynamicData.maskSd = makeIndexedFrame(kSdFrameSize, 255);
        dynamicData.maskHd = makeIndexedFrame(kHdFrameSize, 255);
        dynamicData.maskSd.pixels[0] = 0;
        dynamic.dynamicData = dynamicData;
        expect(!isLegacyFrameActive(dynamic, originalColors, colorizing::render::Resolution::Hd),
               "SD dynamic content of a 256x64 frame does not count");
        dynamic.dynamicData->maskHd.pixels[257] = 0;
        expect(isLegacyFrameActive(dynamic, originalColors, colorizing::render::Resolution::Hd),
               "HD dynamic content of a 256x64 frame counts");
        dynamic.colorizedHd.reset();
        expect(!isLegacyFrameActive(dynamic, originalColors, colorizing::render::Resolution::Hd),
               "dynamic content without an HD frame is not rendered");
        dynamic.durationMs = kSkipFrameDurationMs;
        expect(isLegacyFrameActive(dynamic, originalColors, colorizing::render::Resolution::Hd),
               "a frame long enough is active in every project");
    }
    const std::size_t sdFrames = sectionOffset(project.cromSections, "SD colorized frames");
    expect(project.crom[sdFrames] == 0x34 && project.crom[sdFrames + 1] == 0x12 &&
               sectionOffset(project.cromSections, "HD colorized frames") - sdFrames ==
                   3 * 128 * 32 * 2,
           "the unused SD frames are written with their 128x32 size and content");
    const std::size_t hashes = sectionOffset(project.cromSections, "hash codes");
    expect(readU32(project.crom, hashes + 4) ==
               legacyFrameHash(snapshot.frames[1].originalSd, &snapshot.comparisonMasks[0], true),
           "hash should cover the full 256x64 original and mask");

    const ProjectSnapshot reloaded = parse(project);
    expect(reloaded.geometry.original256x64Flag(), "256x64 flag should survive");
    expect(reloaded.frames[0].originalSd.size.width == 256 &&
               reloaded.frames[0].originalSd.pixels[2] == 3,
           "256x64 original frame should survive");
    expect(reloaded.comparisonMasks[0].size.height == 64 &&
               reloaded.comparisonMasks[0].pixels[300] == 1,
           "256x64 comparison mask should survive");
    expect(reloaded.frames[0].colorizedSd.size.width == 128, "SD colorized frame stays 128x32");
    expect(serialize(reloaded).crom == project.crom, "256x64 project should reserialize identically");

    snapshot.cromRevision = 19;
    std::ostringstream stream;
    std::string error;
    expect(!writeLegacyCrom(stream, snapshot, error),
           "256x64 originals cannot be written as revision 19");
}

void testSpriteCountChangeSerializes()
{
    ProjectSnapshot snapshot = makeSnapshot();
    snapshot.sprites.push_back(makeSprite());
    snapshot.sprites[1].name = "Second";
    snapshot.frames.push_back(snapshot.frames[1]);
    snapshot.backgrounds.clear();
    snapshot.frames[1].backgroundData->backgroundId = 0xffff;
    snapshot.frames[2].backgroundData->backgroundId = 0xffff;

    const ProjectSnapshot reloaded = parse(serialize(snapshot));
    expect(reloaded.frames.size() == 3, "added frame should serialize");
    expect(reloaded.sprites.size() == 2 && reloaded.sprites[1].name == "Second",
           "added sprite should serialize");
    expect(reloaded.backgrounds.empty(), "removed background should serialize");
}

}  // namespace

// What the frame list marks as "inactive": the frames a save flags inactive and libserum has
// nothing to render for.
void testFramesSkippedAtRuntime()
{
    using colorizing::document::ProjectDocument;
    using colorizing::legacy::isFrameSkippedAtRuntime;
    ProjectSnapshot snapshot = makeSnapshot();
    const std::vector<uint16_t> originalColors = legacyOriginalColors(kColorCount);
    const auto skipped = [&](std::size_t frame) {
        const ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
        // The marker and the flag a save writes come from one rule.
        const SerializedProject project = serialize(snapshot);
        const bool inactive =
            project.crom[sectionOffset(project.cromSections, "active frame flags") + frame] == 0;
        const bool marked = isFrameSkippedAtRuntime(document, frame);
        expect(!marked || inactive, "a frame marked as skipped is saved as inactive");
        expect(inactive == !isLegacyFrameActive(snapshot.frames[frame], originalColors,
                                                colorizing::render::Resolution::Sd),
               "the saved flag is isLegacyFrameActive()");
        return marked;
    };
    expect(skipped(0), "shorter than 15 ms and not colorized: skipped");
    expect(!skipped(1), "a colorized frame is not skipped");
    snapshot.frames[0].durationMs = 15;
    expect(!skipped(0), "15 ms is long enough");
    snapshot.frames[0].durationMs = 14;
    expect(skipped(0), "14 ms is not");
    snapshot.frames[0].colorizedSd.pixels[77] ^= 0x0101;
    expect(!skipped(0), "one colorized pixel makes the frame active");
    snapshot.frames[0].colorizedSd.pixels[77] ^= 0x0101;

    // Inactive, and still rendered by libserum (FrameHasRenderableContent()): with a
    // background, or with a dynamic zone on the HD plane of a frame that has HD.
    snapshot.frames[0].backgroundData = snapshot.frames[1].backgroundData;
    expect(!skipped(0), "an inactive frame with a background is rendered");
    snapshot.frames[0].backgroundData->backgroundId = 0xffff;
    expect(skipped(0), "an unassigned background does not count");
    snapshot.frames[0].backgroundData.reset();
    snapshot.frames[0].dynamicData = snapshot.frames[1].dynamicData;
    snapshot.frames[0].dynamicData->maskSd = makeIndexedFrame(kSdFrameSize, 255);
    expect(skipped(0), "an HD dynamic zone of a frame without HD does not count");
    snapshot.frames[0].colorizedHd = makeRgb565Frame(kHdFrameSize, 0);
    expect(!skipped(0), "an inactive frame with an HD dynamic zone and HD is rendered");
    snapshot.frames[0].dynamicData->maskHd = makeIndexedFrame(kHdFrameSize, 255);
    expect(skipped(0), "dynamic data without a zone does not count");
    snapshot.frames[0].dynamicData->maskSd.pixels[9] = 0;
    expect(!skipped(0), "a dynamic zone on the original plane makes the frame active");
}

// Names of the Win32 editor are in the Windows code page; the editor's own are UTF-8.
void testLegacyNameEncoding()
{
    using colorizing::document::fixedName;
    using colorizing::document::setFixedName;
    std::vector<uint8_t> table(2 * 8, 0);
    // "Grün" as the Win32 editor wrote it, and "€5" (0x80 in Windows-1252).
    const uint8_t win32[] = {'G', 'r', 0xfc, 'n'};
    std::copy(std::begin(win32), std::end(win32), table.begin());
    table[8] = 0x80;
    table[9] = '5';
    expect(fixedName(table, 0, 8) == "Gr\xc3\xbcn", "a Windows-1252 name is read as UTF-8");
    expect(fixedName(table, 1, 8) == "\xe2\x82\xac" "5", "including the euro sign");
    const std::vector<uint8_t> before = table;
    setFixedName(table, 0, 8, "Gr\xc3\xbcn");
    expect(table == before, "the same name leaves the Win32 bytes untouched");
    setFixedName(table, 0, 8, "Gr\xc3\xbc\xc3\xbc\xc3\xbc");  // 9 bytes, 7 fit
    expect(fixedName(table, 0, 8) == "Gr\xc3\xbc\xc3\xbc",
           "a name is cut at a character boundary, and stays UTF-8");
    expect(colorizing::document::utf8FromLegacyName("Gr\xc3\xbcn") == "Gr\xc3\xbcn" &&
               colorizing::document::utf8FromLegacyName("\xc3") == "\xc3\x83",
           "UTF-8 stays as it is, a cut-off sequence is Windows-1252");
}

void runLegacyProjectSerializerTests()
{
    testLegacyNameEncoding();
    testFrameHashMatchesReferenceCrc();
    testOriginalColorsMatchWin32Ramp();
    testSerializedProjectRoundTrips();
    testDerivedFieldsAreRecomputed();
    testFramesSkippedAtRuntime();
    testRevisionTwentyHeaderRoundTrips();
    test256x64OriginalsRoundTrip();
    testSpriteCountChangeSerializes();
}
