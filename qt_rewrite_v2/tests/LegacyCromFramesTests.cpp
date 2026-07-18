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
    appendU32(bytes, 0);
    appendU16(bytes, 0);

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
