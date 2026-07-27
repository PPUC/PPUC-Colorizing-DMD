#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacySnapshotBuilder.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using colorizing::legacy::LegacyCromFrameParseResult;
using colorizing::legacy::LegacyCrpOriginalParseResult;
using colorizing::legacy::LegacySnapshotBuildResult;
using colorizing::legacy::buildProjectSnapshot;
using colorizing::legacy::parseLegacyCrpOriginalFrames;
using colorizing::legacy::parseModernLegacyCromFrames;
using colorizing::render::Resolution;

namespace {

constexpr uint32_t kSdWidth = 128;
constexpr uint32_t kSdHeight = 32;
constexpr uint32_t kHdWidth = 256;
constexpr uint32_t kHdHeight = 64;
constexpr std::size_t kSdPixels = kSdWidth * kSdHeight;
constexpr std::size_t kHdPixels = kHdWidth * kHdHeight;
constexpr std::size_t kDynamicSetsPerFrame = 32;
constexpr std::size_t kColorCount = 16;
constexpr std::size_t kMaxSpritesPerFrame = 32;
constexpr std::size_t kMaxColorRotations = 4;
constexpr std::size_t kMaxColorRotationLength = 64;

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

void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value & 0xff));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value & 0xff));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void appendRepeatedU32(std::vector<uint8_t>& bytes, uint32_t value, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        appendU32(bytes, value);
    }
}

void appendName(std::vector<uint8_t>& bytes, const std::string& name)
{
    for (std::size_t i = 0; i < 64; ++i) {
        bytes.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : uint8_t{0});
    }
}

void appendFixedName(std::vector<uint8_t>& bytes, const std::string& name, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        bytes.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : uint8_t{0});
    }
}

void appendRepeatedU16(std::vector<uint8_t>& bytes, uint16_t value, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        appendU16(bytes, value);
    }
}

void appendRepeatedU8(std::vector<uint8_t>& bytes, uint8_t value, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        bytes.push_back(value);
    }
}

void appendModernPostDynamicBlocks(std::vector<uint8_t>& bytes)
{
    appendRepeatedU8(bytes, 255, 2 * kMaxSpritesPerFrame);
    appendRepeatedU8(bytes, 1, 2);
    appendRepeatedU16(bytes, 0, 2 * kMaxColorRotations * kMaxColorRotationLength);
    appendRepeatedU16(bytes, 0, 2 * kMaxColorRotations * kMaxColorRotationLength);
    appendRepeatedU32(bytes, 0xffffffff, 2);
    appendRepeatedU16(bytes, 0xffff, 2 * kMaxSpritesPerFrame * 4);
    appendU16(bytes, 0xffff);
    appendU16(bytes, 0xffff);
    appendRepeatedU8(bytes, 0, 2 * kSdPixels);
    appendRepeatedU8(bytes, 0, 2 * kHdPixels);
    appendRepeatedU8(bytes, 0, 2 * kDynamicSetsPerFrame);
    appendRepeatedU16(bytes, 0, 2 * kDynamicSetsPerFrame);
    appendRepeatedU8(bytes, 0, 2 * kDynamicSetsPerFrame);
    appendRepeatedU16(bytes, 0, 2 * kDynamicSetsPerFrame);
}

std::vector<uint8_t> makeModernCromWithFrames()
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "join_rom");
    appendU32(bytes, 19 * 4);
    appendU32(bytes, kSdWidth);
    appendU32(bytes, kSdHeight);
    appendU32(bytes, kHdWidth);
    appendU32(bytes, kHdHeight);
    appendU32(bytes, 2);
    appendU32(bytes, kColorCount);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU16(bytes, 0);

    appendU32(bytes, 0x11111111);
    appendU32(bytes, 0x22222222);
    bytes.push_back(0);
    bytes.push_back(1);
    bytes.push_back(255);
    bytes.push_back(255);

    bytes.push_back(0);
    bytes.push_back(1);

    appendRepeatedU16(bytes, 0x1001, kSdPixels);
    appendRepeatedU16(bytes, 0x1002, kSdPixels);
    appendRepeatedU16(bytes, 0x2001, kHdPixels);
    appendRepeatedU16(bytes, 0x2002, kHdPixels);
    appendRepeatedU8(bytes, 255, 2 * kSdPixels);
    appendRepeatedU8(bytes, 255, 2 * kHdPixels);
    appendRepeatedU16(bytes, 0x3001, 2 * kDynamicSetsPerFrame * kColorCount);
    appendRepeatedU16(bytes, 0x4001, 2 * kDynamicSetsPerFrame * kColorCount);
    appendModernPostDynamicBlocks(bytes);
    return bytes;
}

std::vector<uint8_t> makeCrpOriginals(const std::string& romName)
{
    std::vector<uint8_t> bytes;
    appendName(bytes, romName);
    for (std::size_t i = 0; i < kSdPixels; ++i) {
        bytes.push_back(1);
    }
    for (std::size_t i = 0; i < kSdPixels; ++i) {
        bytes.push_back(2);
    }
    return bytes;
}

std::vector<uint8_t> makeCrpWithSpriteNames(const std::string& romName)
{
    std::vector<uint8_t> bytes = makeCrpOriginals(romName);
    appendRepeatedU32(bytes, 0, 64);
    appendRepeatedU8(bytes, 0, 64 * 16);
    bytes.push_back(0);
    bytes.push_back(0);
    appendRepeatedU8(bytes, 0, 64 * 64);
    appendU32(bytes, 0);
    bytes.push_back(0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendRepeatedU8(bytes, 0, 64 * 32);
    appendU32(bytes, 0);
    appendRepeatedU32(bytes, 0, 512);
    appendRepeatedU8(bytes, 0, 512 * 32);
    appendFixedName(bytes, "Left saucer", 32);
    appendFixedName(bytes, "Right ramp", 32);
    for (std::size_t i = 2; i < 255; ++i) {
        appendFixedName(bytes, "", 32);
    }
    return bytes;
}

std::vector<uint8_t> makeCrpWithFrameDurations(const std::string& romName)
{
    std::vector<uint8_t> bytes = makeCrpWithSpriteNames(romName);
    appendRepeatedU32(bytes, 0, 255);
    appendU32(bytes, 75);
    appendU32(bytes, 125);
    return bytes;
}

void testCrpOriginalFramesParse()
{
    const LegacyCromFrameParseResult cromResult =
        parseModernLegacyCromFrames(makeModernCromWithFrames());
    expect(cromResult.ok, "fixture cROM should parse");

    const LegacyCrpOriginalParseResult crpResult =
        parseLegacyCrpOriginalFrames(makeCrpOriginals("join_rom"),
                                     cromResult.frameData.metadata);

    expect(crpResult.ok, "matching cRP originals should parse");
    expect(crpResult.originalData.romName == "join_rom", "cRP ROM name");
    expect(crpResult.originalData.originalSdFrames.size() == 2, "cRP original frame count");
    expect(crpResult.originalData.originalSdFrames[0].pixels.front() == 1,
           "first original frame value");
    expect(crpResult.originalData.originalSdFrames[1].pixels.back() == 2,
           "second original frame value");
    expect(crpResult.nextOffset == 64 + 2 * kSdPixels, "cRP originals next offset");
}

void testCrpSpriteNamesParse()
{
    colorizing::legacy::LegacyCromMetadata metadata;
    metadata.romName = "join_rom";
    metadata.frameWidth = kSdWidth;
    metadata.frameHeight = kSdHeight;
    metadata.frameCount = 2;
    metadata.spriteCount = 2;

    const LegacyCrpOriginalParseResult crpResult =
        parseLegacyCrpOriginalFrames(makeCrpWithSpriteNames("join_rom"), metadata);

    expect(crpResult.ok, "cRP sprite names should parse");
    expect(crpResult.originalData.spriteNames.size() == 2, "cRP sprite name count");
    expect(crpResult.originalData.spriteNames[0] == "Left saucer", "first sprite name");
    expect(crpResult.originalData.spriteNames[1] == "Right ramp", "second sprite name");
}

void testCrpFrameDurationsParse()
{
    colorizing::legacy::LegacyCromMetadata metadata;
    metadata.romName = "join_rom";
    metadata.frameWidth = kSdWidth;
    metadata.frameHeight = kSdHeight;
    metadata.frameCount = 2;
    metadata.spriteCount = 2;

    const LegacyCrpOriginalParseResult crpResult =
        parseLegacyCrpOriginalFrames(makeCrpWithFrameDurations("join_rom"), metadata);

    expect(crpResult.ok, "cRP frame durations should parse");
    expect(crpResult.originalData.frameDurationsMs.size() == 2,
           "cRP frame duration count");
    expect(crpResult.originalData.frameDurationsMs[0] == 75,
           "first cRP frame duration");
    expect(crpResult.originalData.frameDurationsMs[1] == 125,
           "second cRP frame duration");
}

void testCrpNameMismatchFails()
{
    const LegacyCromFrameParseResult cromResult =
        parseModernLegacyCromFrames(makeModernCromWithFrames());
    expect(cromResult.ok, "fixture cROM should parse");

    const LegacyCrpOriginalParseResult crpResult =
        parseLegacyCrpOriginalFrames(makeCrpOriginals("wrong_rom"),
                                     cromResult.frameData.metadata);

    expect(!crpResult.ok, "cRP name mismatch should fail");
    expect(crpResult.errorMessage == "cRP ROM name does not match cROM ROM name",
           "cRP name mismatch error");
}

void testTruncatedCrpOriginalsFail()
{
    const LegacyCromFrameParseResult cromResult =
        parseModernLegacyCromFrames(makeModernCromWithFrames());
    expect(cromResult.ok, "fixture cROM should parse");

    std::vector<uint8_t> crpBytes = makeCrpOriginals("join_rom");
    crpBytes.resize(crpBytes.size() - 1);

    const LegacyCrpOriginalParseResult crpResult =
        parseLegacyCrpOriginalFrames(crpBytes, cromResult.frameData.metadata);

    expect(!crpResult.ok, "truncated cRP originals should fail");
    expect(crpResult.errorMessage == "unexpected end of cRP while reading original frames",
           "truncated cRP originals error");
}

void testBuildProjectSnapshotFromCromAndCrp()
{
    const LegacyCromFrameParseResult cromResult =
        parseModernLegacyCromFrames(makeModernCromWithFrames());
    expect(cromResult.ok, "fixture cROM should parse");

    const LegacyCrpOriginalParseResult crpResult =
        parseLegacyCrpOriginalFrames(makeCrpWithFrameDurations("join_rom"),
                                     cromResult.frameData.metadata);
    expect(crpResult.ok, "fixture cRP should parse");

    const LegacySnapshotBuildResult buildResult =
        buildProjectSnapshot(cromResult.frameData, crpResult.originalData);

    expect(buildResult.ok, "snapshot build should succeed");
    expect(buildResult.snapshot.romName == "join_rom", "snapshot ROM name");
    expect(buildResult.snapshot.frames.size() == 2, "snapshot frame count");
    expect(buildResult.snapshot.frames[0].originalSd.pixels.front() == 1,
           "snapshot first original");
    expect(buildResult.snapshot.frames[1].colorizedSd.pixels.front() == 0x1002,
           "snapshot second SD colorized");
    expect(!buildResult.snapshot.frames[0].colorizedHd,
           "snapshot first HD absent by cROM flag");
    expect(buildResult.snapshot.frames[1].colorizedHd.has_value(),
           "snapshot second HD present by cROM flag");
    expect(buildResult.snapshot.frames[1].colorizedHd->pixels.front() == 0x2002,
           "snapshot second HD colorized");

    const colorizing::document::ProjectDocument document =
        colorizing::document::ProjectDocument::fromSnapshot(buildResult.snapshot);
    expect(document.frameCount() == 2, "document frame count from joined snapshot");
    expect(!document.hasHdFrame(0), "document first HD absent");
    expect(document.hasHdFrame(1), "document second HD present");
    expect(document.colorizedFrame(1, Resolution::Hd).pixels.front() == 0x2002,
           "document HD value from joined snapshot");
    expect(document.frameDurationMs(0) == 75, "document first frame duration");
    expect(document.frameDurationMs(1) == 125, "document second frame duration");
    expect(!document.isDirty(), "document loaded from joined snapshot should be clean");
}

}  // namespace

void runLegacyCrpOriginalsTests()
{
    testCrpOriginalFramesParse();
    testCrpSpriteNamesParse();
    testCrpFrameDurationsParse();
    testCrpNameMismatchFails();
    testTruncatedCrpOriginalsFail();
    testBuildProjectSnapshotFromCromAndCrp();
}
