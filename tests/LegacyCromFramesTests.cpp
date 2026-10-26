#include "legacy/LegacyCromFrames.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using colorizing::legacy::LegacyCromFrameParseResult;
using colorizing::legacy::hdFrameIfPresent;
using colorizing::legacy::parseModernLegacyCromFrames;

namespace {

constexpr uint32_t kSdWidth = 128;
constexpr uint32_t kSdHeight = 32;
constexpr uint32_t kHdWidth = 256;
constexpr uint32_t kHdHeight = 64;
constexpr std::size_t kSdPixels = kSdWidth * kSdHeight;
constexpr std::size_t kHdPixels = kHdWidth * kHdHeight;
constexpr std::size_t kDynamicSetsPerFrame = 32;
constexpr std::size_t kColorCount = 16;
constexpr std::size_t kSpritesPerFrame = 32;
constexpr std::size_t kSpriteCount = 1;
constexpr std::size_t kMaxSpriteWidth = 256;
constexpr std::size_t kMaxSpriteHeight = 64;
constexpr std::size_t kSpritePixels = kMaxSpriteWidth * kMaxSpriteHeight;
constexpr std::size_t kMaxColorRotations = 4;
constexpr std::size_t kMaxColorRotationLength = 64;
constexpr std::size_t kMaxSpriteDetectAreas = 4;

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
    bytes.push_back(1);
    bytes.push_back(0);
    appendRepeatedU8(bytes, 255, kSpritesPerFrame - 1);
    appendRepeatedU8(bytes, 255, kSpritesPerFrame);
    appendRepeatedU8(bytes, 9, kSpritePixels);
    appendRepeatedU16(bytes, 0x7001, kSpritePixels);
    appendRepeatedU8(bytes, 10, kSpritePixels);
    appendRepeatedU16(bytes, 0x8001, kSpritePixels);
    appendRepeatedU8(bytes, 1, 2);
    appendU16(bytes, 3);
    appendU16(bytes, 40);
    appendU16(bytes, 0x1111);
    appendU16(bytes, 0x2222);
    appendU16(bytes, 0x3333);
    appendRepeatedU16(bytes, 0, 2 * kMaxColorRotations * kMaxColorRotationLength - 5);
    appendU16(bytes, 2);
    appendU16(bytes, 80);
    appendU16(bytes, 0x4444);
    appendU16(bytes, 0x5555);
    appendRepeatedU16(bytes, 0, 2 * kMaxColorRotations * kMaxColorRotationLength - 4);
    appendU32(bytes, 0x12345678);
    appendRepeatedU32(bytes, 0xffffffff, kMaxSpriteDetectAreas - 1);
    appendU16(bytes, 12);
    appendRepeatedU16(bytes, 0xffff, kMaxSpriteDetectAreas - 1);
    appendU16(bytes, 1);
    appendU16(bytes, 2);
    appendU16(bytes, 3);
    appendU16(bytes, 4);
    appendRepeatedU16(bytes, 0xffff, (kMaxSpriteDetectAreas - 1) * 4);
    appendU32(bytes, 0x11112222);
    appendU32(bytes, 0x33334444);
    appendU16(bytes, 5);
    appendU16(bytes, 6);
    appendU16(bytes, 7);
    appendU16(bytes, 8);
    appendRepeatedU16(bytes, 0xffff, (2 * kSpritesPerFrame - 1) * 4);

    bytes.push_back(1);
    appendRepeatedU16(bytes, 0x5001, kSdPixels);
    appendRepeatedU16(bytes, 0x6001, kHdPixels);
    appendU16(bytes, 0);
    appendU16(bytes, 0xffff);
    appendRepeatedU8(bytes, 0, kSdPixels);
    appendRepeatedU8(bytes, 2, kSdPixels);
    appendRepeatedU8(bytes, 0, kHdPixels);
    appendRepeatedU8(bytes, 3, kHdPixels);
    bytes.push_back(0x08);
    appendRepeatedU8(bytes, 0, 2 * kDynamicSetsPerFrame - 1);
    appendU16(bytes, 0x0f0f);
    appendRepeatedU16(bytes, 0, 2 * kDynamicSetsPerFrame - 1);
    bytes.push_back(0x20);
    appendRepeatedU8(bytes, 0, 2 * kDynamicSetsPerFrame - 1);
    appendU16(bytes, 0xf0f0);
    appendRepeatedU16(bytes, 0, 2 * kDynamicSetsPerFrame - 1);
}

std::vector<uint8_t> makeModernCromWithFrames(uint32_t comparisonMaskCount = 1)
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "frames_rom");
    appendU32(bytes, 19 * 4);
    appendU32(bytes, kSdWidth);
    appendU32(bytes, kSdHeight);
    appendU32(bytes, kHdWidth);
    appendU32(bytes, kHdHeight);
    appendU32(bytes, 2);
    appendU32(bytes, kColorCount);
    appendU32(bytes, comparisonMaskCount);
    appendU32(bytes, kSpriteCount);
    appendU16(bytes, 1);

    appendU32(bytes, 0x11111111);
    appendU32(bytes, 0x22222222);
    bytes.push_back(0);
    bytes.push_back(1);
    bytes.push_back(255);
    bytes.push_back(3);
    for (std::size_t i = 0; i < comparisonMaskCount * kSdPixels; ++i) {
        bytes.push_back(static_cast<uint8_t>(i % 2));
    }

    bytes.push_back(0);
    bytes.push_back(1);

    appendRepeatedU16(bytes, 0x1001, kSdPixels);
    appendRepeatedU16(bytes, 0x1002, kSdPixels);
    appendRepeatedU16(bytes, 0x2001, kHdPixels);
    appendRepeatedU16(bytes, 0x2002, kHdPixels);
    appendRepeatedU8(bytes, 255, kSdPixels);
    appendRepeatedU8(bytes, 7, kSdPixels);
    appendRepeatedU8(bytes, 255, kHdPixels);
    appendRepeatedU8(bytes, 8, kHdPixels);
    appendRepeatedU16(bytes, 0x3001, 2 * kDynamicSetsPerFrame * kColorCount);
    appendRepeatedU16(bytes, 0x4001, 2 * kDynamicSetsPerFrame * kColorCount);
    appendModernPostDynamicBlocks(bytes);

    return bytes;
}

std::vector<uint8_t> makeLegacyPalettedHeader()
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "legacy_rom");
    appendU32(bytes, 11 * 4);
    appendU32(bytes, kSdWidth);
    appendU32(bytes, kSdHeight);
    appendU32(bytes, 2);
    appendU32(bytes, 4);
    appendU32(bytes, 64);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    appendU32(bytes, 0);
    return bytes;
}

void testModernFrameArraysParse()
{
    const LegacyCromFrameParseResult result =
        parseModernLegacyCromFrames(makeModernCromWithFrames());

    expect(result.ok, "modern frame arrays should parse");
    expect(result.frameData.metadata.romName == "frames_rom", "frame data ROM name");
    expect(result.frameData.extraFrameFlags.size() == 2, "extra frame flag count");
    expect(result.frameData.shapeComparisonModes.size() == 2, "shape comparison mode count");
    expect(result.frameData.shapeComparisonModes[0] == 0, "first shape comparison mode");
    expect(result.frameData.shapeComparisonModes[1] == 1, "second shape comparison mode");
    expect(result.frameData.comparisonMaskIds.size() == 2, "comparison mask ID count");
    expect(result.frameData.comparisonMaskIds[0] == 255, "first comparison mask ID");
    expect(result.frameData.comparisonMaskIds[1] == 3, "second comparison mask ID");
    expect(result.frameData.comparisonMasks.size() == 1, "comparison mask count");
    expect(result.frameData.comparisonMasks[0].pixels.size() == kSdPixels,
           "comparison mask pixel count");
    expect(result.frameData.comparisonMasks[0].pixels[1] == 1,
           "comparison mask pixel data");
    expect(result.frameData.extraFrameFlags[0] == 0, "first frame HD absent flag");
    expect(result.frameData.extraFrameFlags[1] == 1, "second frame HD present flag");
    expect(result.frameData.colorizedSdFrames.size() == 2, "SD frame count");
    expect(result.frameData.colorizedHdFrames.size() == 2, "HD frame storage count");
    expect(result.frameData.colorizedSdFrames[0].pixels.front() == 0x1001,
           "first SD frame value");
    expect(result.frameData.colorizedSdFrames[1].pixels.back() == 0x1002,
           "second SD frame value");
    expect(result.frameData.colorizedHdFrames[0].pixels.front() == 0x2001,
           "first HD storage value");
    expect(result.frameData.colorizedHdFrames[1].pixels.back() == 0x2002,
           "second HD storage value");
    expect(result.frameData.dynamicFrames.size() == 2, "dynamic frame count");
    expect(result.frameData.dynamicFrames[1].maskSd.pixels.front() == 7,
           "second SD dynamic mask value");
    expect(result.frameData.dynamicFrames[1].maskHd.pixels.front() == 8,
           "second HD dynamic mask value");
    expect(result.frameData.dynamicFrames[0].colorsSd.front() == 0x3001,
           "first SD dynamic color set value");
    expect(result.frameData.dynamicFrames[0].colorsHd.front() == 0x4001,
           "first HD dynamic color set value");
    expect(result.frameData.dynamicFrames[0].shadowDirectionsSd.front() == 0x08,
           "first SD dynamic shadow direction value");
    expect(result.frameData.dynamicFrames[0].shadowColorsSd.front() == 0x0f0f,
           "first SD dynamic shadow color value");
    expect(result.frameData.dynamicFrames[0].shadowDirectionsHd.front() == 0x20,
           "first HD dynamic shadow direction value");
    expect(result.frameData.dynamicFrames[0].shadowColorsHd.front() == 0xf0f0,
           "first HD dynamic shadow color value");
    expect(result.frameData.sprites.size() == kSpriteCount, "sprite count");
    expect(result.frameData.sprites[0].hasHd, "sprite HD flag");
    expect(result.frameData.sprites[0].originalSd.pixels.front() == 9,
           "sprite original value");
    expect(result.frameData.sprites[0].colorizedSd.pixels.front() == 0x7001,
           "SD sprite color value");
    expect(result.frameData.sprites[0].maskHd.pixels.front() == 10,
           "HD sprite mask value");
    expect(result.frameData.sprites[0].colorizedHd.pixels.front() == 0x8001,
           "HD sprite color value");
    expect(result.frameData.colorRotationsSd[0][0] == 3,
           "first SD color rotation length");
    expect(result.frameData.colorRotationsSd[0][1] == 40,
           "first SD color rotation delay");
    expect(result.frameData.colorRotationsSd[0][2] == 0x1111,
           "first SD color rotation color");
    expect(result.frameData.colorRotationsHd[0][0] == 2,
           "first HD color rotation length");
    expect(result.frameData.colorRotationsHd[0][1] == 80,
           "first HD color rotation delay");
    expect(result.frameData.colorRotationsHd[0][3] == 0x5555,
           "first HD color rotation color");
    expect(result.frameData.sprites[0].detectionDwords[0] == 0x12345678,
           "sprite detection dword value");
    expect(result.frameData.sprites[0].detectionDwordPositions[0] == 12,
           "sprite detection dword position value");
    expect(result.frameData.sprites[0].detectionAreas[0].x == 1,
           "sprite detection area x value");
    expect(result.frameData.sprites[0].detectionAreas[0].height == 4,
           "sprite detection area height value");
    expect(result.frameData.frameSpriteReferences.size() == 2,
           "frame sprite reference frame count");
    expect(result.frameData.triggerIds.size() == 2, "trigger ID frame count");
    expect(result.frameData.triggerIds[0] == 0x11112222, "first trigger ID value");
    expect(result.frameData.triggerIds[1] == 0x33334444, "second trigger ID value");
    expect(result.frameData.frameSpriteReferences[0][0].spriteId == 0,
           "frame sprite assignment value");
    expect(result.frameData.frameSpriteReferences[0][0].minX == 5,
           "frame sprite bounding box min x");
    expect(result.frameData.frameSpriteReferences[0][0].maxY == 8,
           "frame sprite bounding box max y");
    expect(result.frameData.backgrounds.size() == 1, "background count");
    expect(result.frameData.backgrounds[0].colorizedSd.pixels.front() == 0x5001,
           "SD background value");
    expect(result.frameData.backgrounds[0].colorizedHd.has_value(),
           "HD background should be present by flag");
    expect(result.frameData.backgrounds[0].colorizedHd->pixels.front() == 0x6001,
           "HD background value");
    expect(result.frameData.frameBackgrounds.size() == 2, "frame background count");
    expect(result.frameData.frameBackgrounds[0].backgroundId == 0,
           "first frame background ID");
    expect(result.frameData.frameBackgrounds[1].backgroundId == 0xffff,
           "second frame background ID");
    expect(result.frameData.frameBackgrounds[1].maskSd.pixels.front() == 2,
           "second SD background mask value");
    expect(result.frameData.frameBackgrounds[1].maskHd.pixels.front() == 3,
           "second HD background mask value");

    expect(!hdFrameIfPresent(result.frameData, 0), "first HD frame should be absent by flag");
    const std::optional<colorizing::document::Rgb565Frame> hdFrame =
        hdFrameIfPresent(result.frameData, 1);
    expect(hdFrame.has_value(), "second HD frame should be present by flag");
    expect(hdFrame->pixels.front() == 0x2002, "second HD frame helper value");
}

void testModernFrameArraysWithoutComparisonMasks()
{
    const LegacyCromFrameParseResult result =
        parseModernLegacyCromFrames(makeModernCromWithFrames(0));

    expect(result.ok, "modern frame arrays without comparison masks should parse");
    expect(result.frameData.comparisonMasks.empty(), "zero comparison masks should be empty");
    expect(result.frameData.colorizedSdFrames[0].pixels.front() == 0x1001,
           "SD data after zero comparison masks");
}

void testLegacyPalettedFramesRejectedForNow()
{
    const LegacyCromFrameParseResult result =
        parseModernLegacyCromFrames(makeLegacyPalettedHeader());

    expect(!result.ok, "legacy paletted frame parse should be rejected for now");
    expect(result.errorMessage == "legacy paletted cROM frame parsing is not implemented yet",
           "legacy paletted rejection message");
}

void testTruncatedSdFramesFail()
{
    std::vector<uint8_t> bytes = makeModernCromWithFrames();
    const LegacyCromFrameParseResult fullResult = parseModernLegacyCromFrames(bytes);
    expect(fullResult.ok, "full fixture should parse before SD truncation");
    bytes.resize(fullResult.frameData.colorizedSdFramesOffset +
                 kSdPixels * sizeof(uint16_t) - 1);

    const LegacyCromFrameParseResult result = parseModernLegacyCromFrames(bytes);

    expect(!result.ok, "truncated SD frames should fail");
    expect(result.errorMessage == "unexpected end of cROM while reading SD colorized frames",
           "truncated SD frame error");
}

void testTruncatedHdFramesFail()
{
    std::vector<uint8_t> bytes = makeModernCromWithFrames();
    const LegacyCromFrameParseResult fullResult = parseModernLegacyCromFrames(bytes);
    expect(fullResult.ok, "full fixture should parse before HD truncation");
    bytes.resize(fullResult.frameData.colorizedHdFramesOffset +
                 kHdPixels * sizeof(uint16_t) - 1);

    const LegacyCromFrameParseResult result = parseModernLegacyCromFrames(bytes);

    expect(!result.ok, "truncated HD frames should fail");
    expect(result.errorMessage == "unexpected end of cROM while reading HD colorized frames",
           "truncated HD frame error");
}

}  // namespace

void runLegacyCromFramesTests()
{
    testModernFrameArraysParse();
    testModernFrameArraysWithoutComparisonMasks();
    testLegacyPalettedFramesRejectedForNow();
    testTruncatedSdFramesFail();
    testTruncatedHdFramesFail();
}
