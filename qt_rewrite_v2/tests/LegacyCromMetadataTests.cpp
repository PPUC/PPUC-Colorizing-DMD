#include "legacy/LegacyCromMetadata.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using colorizing::legacy::LegacyCromParseResult;
using colorizing::legacy::parseLegacyCromMetadata;

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

std::vector<uint8_t> makeModernHeader(uint32_t frameCount = 3,
                                      uint32_t colorCount = 16,
                                      uint16_t backgroundCount = 2)
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "modern_rom");
    appendU32(bytes, 19 * 4);
    appendU32(bytes, 128);
    appendU32(bytes, 32);
    appendU32(bytes, 256);
    appendU32(bytes, 64);
    appendU32(bytes, frameCount);
    appendU32(bytes, colorCount);
    appendU32(bytes, 5);
    appendU32(bytes, 7);
    appendU16(bytes, backgroundCount);
    return bytes;
}

std::vector<uint8_t> makeLegacyHeader(uint32_t frameWidth, uint32_t frameHeight)
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "legacy_rom");
    appendU32(bytes, 11 * 4);
    appendU32(bytes, frameWidth);
    appendU32(bytes, frameHeight);
    appendU32(bytes, 4);
    appendU32(bytes, 4);
    appendU32(bytes, 64);
    appendU32(bytes, 3);
    appendU32(bytes, 0);
    appendU32(bytes, 9);
    return bytes;
}

void testModernRgb565Header()
{
    const LegacyCromParseResult result = parseLegacyCromMetadata(makeModernHeader());

    expect(result.ok, "modern header should parse");
    expect(result.metadata.romName == "modern_rom", "modern ROM name");
    expect(result.metadata.headerLengthBytes == 76, "modern header length");
    expect(result.metadata.modernRgb565Format, "modern format flag");
    expect(result.metadata.frameWidth == 128 && result.metadata.frameHeight == 32,
           "modern SD dimensions");
    expect(result.metadata.hdFrameWidth == 256 && result.metadata.hdFrameHeight == 64,
           "modern HD dimensions");
    expect(result.metadata.frameCount == 3, "modern frame count");
    expect(result.metadata.colorCount == 16, "modern color count");
    expect(result.metadata.comparisonMaskCount == 5, "modern comparison mask count");
    expect(result.metadata.spriteCount == 7, "modern sprite count");
    expect(result.metadata.backgroundCount == 2, "modern background count");
    expect(result.nextOffset == 64 + 4 + 8 * 4 + 2, "modern next offset");
}

void testLegacyHeaderInfersHdByUpscalingSd()
{
    const LegacyCromParseResult result = parseLegacyCromMetadata(makeLegacyHeader(128, 32));

    expect(result.ok, "legacy SD header should parse");
    expect(result.metadata.romName == "legacy_rom", "legacy ROM name");
    expect(!result.metadata.modernRgb565Format, "legacy format flag");
    expect(result.metadata.frameWidth == 128 && result.metadata.frameHeight == 32,
           "legacy SD dimensions");
    expect(result.metadata.hdFrameWidth == 256 && result.metadata.hdFrameHeight == 64,
           "legacy inferred HD dimensions");
    expect(result.metadata.frameCount == 4, "legacy frame count");
    expect(result.metadata.colorCount == 4, "legacy color count");
    expect(result.metadata.comparisonMaskCount == 3, "legacy comparison mask count");
    expect(result.metadata.spriteCount == 9, "legacy sprite count");
    expect(result.metadata.backgroundCount == 0, "legacy background count before header v13");
}

void testLegacyHeaderInfersSdFromSixtyFourPixelSource()
{
    const LegacyCromParseResult result = parseLegacyCromMetadata(makeLegacyHeader(256, 64));

    expect(result.ok, "legacy 64px header should parse");
    expect(result.metadata.hdFrameWidth == 128 && result.metadata.hdFrameHeight == 32,
           "legacy 64px source follows legacy downscale rule");
}

void testTruncatedHeaderFails()
{
    std::vector<uint8_t> bytes = makeModernHeader();
    bytes.resize(bytes.size() - 1);

    const LegacyCromParseResult result = parseLegacyCromMetadata(bytes);

    expect(!result.ok, "truncated header should fail");
    expect(result.errorMessage == "unexpected end of cROM while reading background count",
           "truncated header error");
}

void testInvalidZeroFrameCountFails()
{
    const LegacyCromParseResult result = parseLegacyCromMetadata(makeModernHeader(0));

    expect(!result.ok, "zero frame count should fail");
    expect(result.errorMessage == "cROM frame count must be non-zero",
           "zero frame count error");
}

void testEmptyNameFails()
{
    std::vector<uint8_t> bytes = makeModernHeader();
    for (std::size_t i = 0; i < 64; ++i) {
        bytes[i] = 0;
    }

    const LegacyCromParseResult result = parseLegacyCromMetadata(bytes);

    expect(!result.ok, "empty ROM name should fail");
    expect(result.errorMessage == "cROM ROM name is empty", "empty ROM name error");
}

}  // namespace

void runLegacyCromMetadataTests()
{
    testModernRgb565Header();
    testLegacyHeaderInfersHdByUpscalingSd();
    testLegacyHeaderInfersSdFromSixtyFourPixelSource();
    testTruncatedHeaderFails();
    testInvalidZeroFrameCountFails();
    testEmptyNameFails();
}
