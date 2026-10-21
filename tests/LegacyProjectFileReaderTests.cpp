#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"
#include "legacy/LegacyProjectSerializer.h"
#include "document/EditorMetadata.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "legacy/CrpxFile.h"
#include "legacy/LegacyCrzArchive.h"
#include "platform/Utf8Path.h"
#include "render/CanvasLayout.h"

using colorizing::document::makeRgb565Frame;
using colorizing::legacy::LegacyLoadResult;
using colorizing::legacy::LegacyProjectFileReader;
using colorizing::legacy::LegacyProjectFileWriter;
using colorizing::legacy::LegacyProjectPaths;
using colorizing::legacy::LegacySaveResult;
using colorizing::platform::pathFromUtf8;
using colorizing::platform::utf8FromPath;

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

void appendFixedName(std::vector<uint8_t>& bytes, const std::string& name, std::size_t length)
{
    for (std::size_t i = 0; i < length; ++i) {
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
    appendRepeatedU8(bytes, 255, kMaxSpritesPerFrame - 1);
    appendRepeatedU8(bytes, 255, kMaxSpritesPerFrame);
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
    appendRepeatedU16(bytes, 0xffff, (2 * kMaxSpritesPerFrame - 1) * 4);

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

std::vector<uint8_t> makeModernCromWithFrames()
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "file_rom");
    appendU32(bytes, 19 * 4);
    appendU32(bytes, kSdWidth);
    appendU32(bytes, kSdHeight);
    appendU32(bytes, kHdWidth);
    appendU32(bytes, kHdHeight);
    appendU32(bytes, 2);
    appendU32(bytes, kColorCount);
    appendU32(bytes, 1);
    appendU32(bytes, kSpriteCount);
    appendU16(bytes, 1);

    appendU32(bytes, 0x11111111);
    appendU32(bytes, 0x22222222);
    bytes.push_back(0);
    bytes.push_back(1);
    bytes.push_back(255);
    bytes.push_back(255);
    for (std::size_t i = 0; i < kSdPixels; ++i) {
        bytes.push_back(static_cast<uint8_t>(i % 2));
    }

    bytes.push_back(1);
    bytes.push_back(0);

    appendRepeatedU16(bytes, 0x1111, kSdPixels);
    appendRepeatedU16(bytes, 0x2222, kSdPixels);
    appendRepeatedU16(bytes, 0xaaaa, kHdPixels);
    appendRepeatedU16(bytes, 0xbbbb, kHdPixels);
    appendRepeatedU8(bytes, 255, kSdPixels);
    appendRepeatedU8(bytes, 3, kSdPixels);
    appendRepeatedU8(bytes, 255, kHdPixels);
    appendRepeatedU8(bytes, 4, kHdPixels);
    appendRepeatedU16(bytes, 0x3001, 2 * kDynamicSetsPerFrame * kColorCount);
    appendRepeatedU16(bytes, 0x4001, 2 * kDynamicSetsPerFrame * kColorCount);
    appendModernPostDynamicBlocks(bytes);
    bytes.push_back(0x99);
    bytes.push_back(0x88);
    return bytes;
}

std::vector<uint8_t> makeCrpOriginals(const std::string& romName)
{
    std::vector<uint8_t> bytes;
    appendName(bytes, romName);
    for (std::size_t i = 0; i < kSdPixels; ++i) {
        bytes.push_back(3);
    }
    for (std::size_t i = 0; i < kSdPixels; ++i) {
        bytes.push_back(4);
    }
    return bytes;
}

std::vector<uint8_t> makeCrpWithSpriteNames(const std::string& romName)
{
    std::vector<uint8_t> bytes = makeCrpOriginals(romName);
    appendRepeatedU32(bytes, 0, 64);
    appendRepeatedU16(bytes, 0, 64 * 16);
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
    appendFixedName(bytes, "Initial sprite", 32);
    for (std::size_t i = 1; i < 255; ++i) {
        appendFixedName(bytes, "", 32);
    }
    return bytes;
}

std::vector<uint8_t> makeCrpWithFrameDurations(const std::string& romName)
{
    std::vector<uint8_t> bytes = makeCrpWithSpriteNames(romName);
    appendRepeatedU32(bytes, 0, 255);
    appendU32(bytes, 10);
    appendU32(bytes, 20);
    return bytes;
}

std::vector<uint8_t> makeCrpWithPalettes(const std::string& romName)
{
    std::vector<uint8_t> bytes = makeCrpWithFrameDurations(romName);
    appendRepeatedU16(bytes, 0, 255 * 4);
    appendRepeatedU32(bytes, 0, 255 * 2);
    for (std::size_t paletteIndex = 0; paletteIndex < 256; ++paletteIndex) {
        for (std::size_t colorIndex = 0; colorIndex < 64; ++colorIndex) {
            uint16_t color = 0;
            if (paletteIndex == 0 && colorIndex == 0) {
                color = 0x1111;
            } else if (paletteIndex == 1 && colorIndex == 2) {
                color = 0x2222;
            }
            appendU16(bytes, color);
        }
    }
    appendRepeatedU16(bytes, 0, 16);
    appendU32(bytes, 0);
    appendRepeatedU8(bytes, 0, 256 * 64);
    appendRepeatedU32(bytes, 0, 256 * 16);
    appendFixedName(bytes, "Default palette", 64);
    appendFixedName(bytes, "Warm palette", 64);
    for (std::size_t i = 2; i < 256; ++i) {
        appendFixedName(bytes, "", 64);
    }
    return bytes;
}

std::vector<uint8_t> makeLegacyPalettedCrom()
{
    std::vector<uint8_t> bytes;
    appendName(bytes, "file_rom");
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

// The names hold a Greek capital omega (U+03A9), which code page 1252 (Western Windows, and
// the CI runners) does not have: a path that goes through the ANSI code page anywhere between
// the test and the file fails these tests on Windows. Paths reach the code under test as UTF-8.
std::filesystem::path uniqueTempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           pathFromUtf8("colorizing_qt_v2_reader_\xce\xa9_" + run + "_" +
                        std::to_string(counter++) + suffix);
}

std::filesystem::path savingPathFor(const std::filesystem::path& path)
{
    return pathFromUtf8(utf8FromPath(path) + ".saving");
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        fail("could not create temp file: " + utf8FromPath(path));
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        fail("could not write temp file: " + utf8FromPath(path));
    }
}

void testFileReaderLoadsModernProject()
{
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    writeBytes(cromPath, makeModernCromWithFrames());
    writeBytes(crpPath, makeCrpWithPalettes("file_rom"));

    const LegacyProjectFileReader reader;
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        utf8FromPath(cromPath),
        utf8FromPath(crpPath),
    });

    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);

    expect(result.ok, "file reader should load modern project");
    expect(result.snapshot.romName == "file_rom", "file reader ROM name");
    expect(result.snapshot.frames.size() == 2, "file reader frame count");
    expect(result.snapshot.frames[0].originalSd.pixels.front() == 3,
           "file reader first original");
    expect(result.snapshot.frames[1].colorizedSd.pixels.front() == 0x2222,
           "file reader second SD colorized");
    expect(result.snapshot.frames[0].colorizedHd.has_value(),
           "file reader first HD present");
    expect(!result.snapshot.frames[1].colorizedHd,
           "file reader second HD absent");
    expect(result.snapshot.frames[0].colorizedHd->pixels.front() == 0xaaaa,
           "file reader first HD value");
    expect(result.snapshot.comparisonMasks.size() == 1,
           "file reader comparison mask count");
    expect(result.snapshot.comparisonMasks[0].pixels[1] == 1,
           "file reader comparison mask data");
    expect(result.snapshot.frames[0].dynamicData.has_value(),
           "file reader first dynamic data present");
    expect(result.snapshot.frames[1].dynamicData.has_value(),
           "file reader second dynamic data present");
    expect(result.snapshot.frames[1].dynamicData->maskSd.pixels.front() == 3,
           "file reader second SD dynamic mask data");
    expect(result.snapshot.frames[1].dynamicData->maskHd.pixels.front() == 4,
           "file reader second HD dynamic mask data");
    expect(result.snapshot.frames[0].dynamicData->colorsSd.front() == 0x3001,
           "file reader SD dynamic color data");
    expect(result.snapshot.frames[0].dynamicData->colorsHd.front() == 0x4001,
           "file reader HD dynamic color data");
    expect(result.snapshot.frames[0].dynamicData->shadowDirectionsSd.front() == 0x08,
           "file reader SD dynamic shadow direction data");
    expect(result.snapshot.frames[0].dynamicData->shadowColorsSd.front() == 0x0f0f,
           "file reader SD dynamic shadow color data");
    expect(result.snapshot.frames[0].dynamicData->shadowDirectionsHd.front() == 0x20,
           "file reader HD dynamic shadow direction data");
    expect(result.snapshot.frames[0].dynamicData->shadowColorsHd.front() == 0xf0f0,
           "file reader HD dynamic shadow color data");
    expect(result.snapshot.sprites.size() == kSpriteCount,
           "file reader sprite count");
    expect(result.snapshot.sprites[0].hasHd,
           "file reader sprite HD flag");
    expect(result.snapshot.sprites[0].originalSd.pixels.front() == 9,
           "file reader sprite original data");
    expect(result.snapshot.sprites[0].colorizedSd.pixels.front() == 0x7001,
           "file reader SD sprite color data");
    expect(result.snapshot.sprites[0].maskHd.pixels.front() == 10,
           "file reader HD sprite mask data");
    expect(result.snapshot.sprites[0].colorizedHd.pixels.front() == 0x8001,
           "file reader HD sprite color data");
    expect(result.snapshot.sprites[0].detectionDwords[0] == 0x12345678,
           "file reader sprite detection dword data");
    expect(result.snapshot.sprites[0].detectionAreas[0].height == 4,
           "file reader sprite detection area data");
    expect(result.snapshot.frames[0].spriteReferences[0].spriteId == 0,
           "file reader frame sprite assignment data");
    expect(result.snapshot.frames[0].spriteReferences[0].maxY == 8,
           "file reader frame sprite bounding box data");
    expect(result.snapshot.frames[0].triggerId == 0x11112222,
           "file reader first trigger ID");
    expect(result.snapshot.frames[1].triggerId == 0x33334444,
           "file reader second trigger ID");
    expect(result.snapshot.frames[0].colorRotationsSd[0] == 3,
           "file reader SD color rotation length");
    expect(result.snapshot.frames[0].colorRotationsSd[2] == 0x1111,
           "file reader SD color rotation color");
    expect(result.snapshot.frames[0].colorRotationsHd[1] == 80,
           "file reader HD color rotation delay");
    expect(result.snapshot.frames[0].colorRotationsHd[3] == 0x5555,
           "file reader HD color rotation color");
    expect(result.snapshot.backgrounds.size() == 1,
           "file reader background count");
    expect(result.snapshot.backgrounds[0].colorizedSd.pixels.front() == 0x5001,
           "file reader SD background data");
    expect(result.snapshot.backgrounds[0].colorizedHd.has_value(),
           "file reader HD background present");
    expect(result.snapshot.backgrounds[0].colorizedHd->pixels.front() == 0x6001,
           "file reader HD background data");
    expect(result.snapshot.frames[0].backgroundData.has_value(),
           "file reader first background data present");
    expect(result.snapshot.frames[0].backgroundData->backgroundId == 0,
           "file reader first background ID");
    expect(result.snapshot.frames[1].backgroundData->backgroundId == 0xffff,
           "file reader second background ID");
    expect(result.snapshot.frames[1].backgroundData->maskSd.pixels.front() == 2,
           "file reader SD background mask data");
    expect(result.snapshot.frames[1].backgroundData->maskHd.pixels.front() == 3,
           "file reader HD background mask data");
    expect(result.snapshot.palettes.size() == 256,
           "file reader palette count");
    expect(result.snapshot.palettes[0].colors[0] == 0x1111,
           "file reader first palette color");
    expect(result.snapshot.palettes[1].colors[2] == 0x2222,
           "file reader second palette color");
    expect(result.snapshot.palettes[0].name == "Default palette",
           "file reader first palette name");
    expect(result.snapshot.palettes[1].name == "Warm palette",
           "file reader second palette name");
}

void testFileReaderReportsMissingCrom()
{
    const LegacyProjectFileReader reader;
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        "/private/tmp/colorizing_qt_v2_missing.cROM",
        "/private/tmp/colorizing_qt_v2_missing.cRP",
    });

    expect(!result.ok, "missing cROM should fail");
    expect(result.error.message.find("could not open .cROM file:") == 0,
           "missing cROM error message");
}

void testFileReaderRejectsLegacyPalettedCrom()
{
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    writeBytes(cromPath, makeLegacyPalettedCrom());
    writeBytes(crpPath, makeCrpOriginals("file_rom"));

    const LegacyProjectFileReader reader;
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        utf8FromPath(cromPath),
        utf8FromPath(crpPath),
    });

    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);

    expect(!result.ok, "legacy paletted cROM should fail for now");
    expect(result.error.message == "legacy paletted cROM frame parsing is not implemented yet",
           "legacy paletted file reader error");
}

void testFileWriterSavesEditedModernCrom()
{
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    writeBytes(cromPath, makeModernCromWithFrames());
    writeBytes(crpPath, makeCrpWithPalettes("file_rom"));

    const LegacyProjectPaths paths{
        utf8FromPath(cromPath),
        utf8FromPath(crpPath),
    };

    const LegacyProjectFileReader reader;
    LegacyLoadResult loadResult = reader.load(paths);
    expect(loadResult.ok, "writer test initial load");

    loadResult.snapshot.frames[0].colorizedSd.pixels[0] = 0x1357;
    loadResult.snapshot.frames[0].shapeComparisonMode = 9;
    loadResult.snapshot.frames[0].comparisonMaskId = 0;
    loadResult.snapshot.comparisonMasks[0].pixels[7] = 42;
    loadResult.snapshot.frames[0].dynamicData->maskSd.pixels[8] = 5;
    loadResult.snapshot.frames[0].dynamicData->maskHd.pixels[9] = 6;
    loadResult.snapshot.frames[0].dynamicData->colorsSd[10] = 0x7777;
    loadResult.snapshot.frames[0].dynamicData->colorsHd[11] = 0x8888;
    loadResult.snapshot.frames[0].dynamicData->shadowDirectionsSd[2] = 0x04;
    loadResult.snapshot.frames[0].dynamicData->shadowColorsSd[2] = 0x9990;
    loadResult.snapshot.frames[0].dynamicData->shadowDirectionsHd[3] = 0x40;
    loadResult.snapshot.frames[0].dynamicData->shadowColorsHd[3] = 0x9991;
    loadResult.snapshot.frames[0].colorRotationsSd[0] = 2;
    loadResult.snapshot.frames[0].colorRotationsSd[1] = 120;
    loadResult.snapshot.frames[0].colorRotationsSd[2] = 0xabcd;
    loadResult.snapshot.frames[0].colorRotationsSd[3] = 0xbcde;
    loadResult.snapshot.frames[0].colorRotationsHd[0] = 1;
    loadResult.snapshot.frames[0].colorRotationsHd[1] = 250;
    loadResult.snapshot.frames[0].colorRotationsHd[2] = 0xcdef;
    loadResult.snapshot.sprites[0].hasHd = false;
    loadResult.snapshot.sprites[0].name = "Renamed sprite";
    loadResult.snapshot.sprites[0].originalSd.pixels[16] = 11;
    loadResult.snapshot.sprites[0].colorizedSd.pixels[17] = 0x7770;
    loadResult.snapshot.sprites[0].maskHd.pixels[18] = 12;
    loadResult.snapshot.sprites[0].colorizedHd.pixels[19] = 0x8880;
    loadResult.snapshot.sprites[0].detectionDwords[1] = 0x87654321;
    loadResult.snapshot.sprites[0].detectionDwordPositions[1] = 34;
    loadResult.snapshot.sprites[0].detectionAreas[1] = {9, 10, 11, 12};
    loadResult.snapshot.frames[0].spriteReferences[0].spriteId = 255;
    loadResult.snapshot.frames[0].spriteReferences[1] = {0, 13, 14, 15, 16};
    loadResult.snapshot.frames[0].triggerId = 1234;
    loadResult.snapshot.frames[1].triggerId = 65432;
    loadResult.snapshot.frames[0].durationMs = 75;
    loadResult.snapshot.frames[1].durationMs = 125;
    loadResult.snapshot.backgrounds[0].colorizedSd.pixels[12] = 0x9999;
    loadResult.snapshot.backgrounds[0].colorizedHd->pixels[13] = 0xaaaa;
    loadResult.snapshot.frames[0].backgroundData->backgroundId = 0xffff;
    loadResult.snapshot.frames[1].backgroundData->backgroundId = 0;
    loadResult.snapshot.frames[1].backgroundData->maskSd.pixels[14] = 7;
    loadResult.snapshot.frames[1].backgroundData->maskHd.pixels[15] = 8;
    loadResult.snapshot.frames[0].colorizedHd.reset();
    loadResult.snapshot.frames[1].dynamicData->maskSd.pixels[kSdPixels - 1] = 255;
    loadResult.snapshot.frames[1].dynamicData->maskHd.pixels[5] = 255;
    loadResult.snapshot.frames[1].colorizedSd.pixels[kSdPixels - 1] = 0x2468;
    expect(loadResult.snapshot.frames[1].inactiveHd.has_value() &&
               loadResult.snapshot.frames[1].inactiveHd->pixels.front() == 0xbbbb,
           "file reader should keep HD pixels of a frame whose HD flag is off");
    loadResult.snapshot.frames[1].inactiveHd.reset();
    loadResult.snapshot.frames[1].colorizedHd =
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0xcafe);
    loadResult.snapshot.frames[1].colorizedHd->pixels[5] = 0xbeef;
    loadResult.snapshot.palettes[0].colors[0] = 0xabcd;
    loadResult.snapshot.palettes[1].name = "Edited palette";

    const LegacyProjectFileWriter writer;
    const LegacySaveResult saveResult = writer.save(paths, loadResult.snapshot);
    expect(saveResult.ok, "file writer should save edited modern cROM");

    expect(!std::filesystem::exists(savingPathFor(cromPath)) &&
               !std::filesystem::exists(savingPathFor(crpPath)),
           "file writer should not leave temporary files behind");

    const LegacyLoadResult reloadResult = reader.load(paths);

    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);

    expect(reloadResult.ok, "file writer saved cROM should reload");
    expect(reloadResult.snapshot.frames[0].colorizedSd.pixels[0] == 0x1357,
           "file writer should update first SD pixel");
    expect(reloadResult.snapshot.frames[0].shapeComparisonMode == 9,
           "file writer should update shape comparison mode");
    expect(reloadResult.snapshot.frames[0].comparisonMaskId == 0,
           "file writer should update comparison mask ID");
    expect(reloadResult.snapshot.comparisonMasks[0].pixels[7] == 42,
           "file writer should update comparison mask pixels");
    expect(reloadResult.snapshot.frames[0].dynamicData->maskSd.pixels[8] == 5,
           "file writer should update SD dynamic mask pixels");
    expect(reloadResult.snapshot.frames[0].dynamicData->maskHd.pixels[9] == 6,
           "file writer should update HD dynamic mask pixels");
    expect(reloadResult.snapshot.frames[0].dynamicData->colorsSd[10] == 0x7777,
           "file writer should update SD dynamic color sets");
    expect(reloadResult.snapshot.frames[0].dynamicData->colorsHd[11] == 0x8888,
           "file writer should update HD dynamic color sets");
    expect(reloadResult.snapshot.frames[0].dynamicData->shadowDirectionsSd[2] == 0x04,
           "file writer should update SD dynamic shadow directions");
    expect(reloadResult.snapshot.frames[0].dynamicData->shadowColorsSd[2] == 0x9990,
           "file writer should update SD dynamic shadow colors");
    expect(reloadResult.snapshot.frames[0].dynamicData->shadowDirectionsHd[3] == 0x40,
           "file writer should update HD dynamic shadow directions");
    expect(reloadResult.snapshot.frames[0].dynamicData->shadowColorsHd[3] == 0x9991,
           "file writer should update HD dynamic shadow colors");
    expect(reloadResult.snapshot.frames[0].colorRotationsSd[0] == 2,
           "file writer should update SD color rotation length");
    expect(reloadResult.snapshot.frames[0].colorRotationsSd[1] == 120,
           "file writer should update SD color rotation delay");
    expect(reloadResult.snapshot.frames[0].colorRotationsSd[3] == 0xbcde,
           "file writer should update SD color rotation colors");
    expect(reloadResult.snapshot.frames[0].colorRotationsHd[0] == 1,
           "file writer should update HD color rotation length");
    expect(reloadResult.snapshot.frames[0].colorRotationsHd[2] == 0xcdef,
           "file writer should update HD color rotation colors");
    expect(!reloadResult.snapshot.sprites[0].hasHd,
           "file writer should update sprite HD flag");
    expect(reloadResult.snapshot.sprites[0].name == "Renamed sprite",
           "file writer should update cRP sprite names");
    expect(reloadResult.snapshot.sprites[0].originalSd.pixels[16] == 11,
           "file writer should update sprite original pixels");
    expect(reloadResult.snapshot.sprites[0].colorizedSd.pixels[17] == 0x7770,
           "file writer should update SD sprite pixels");
    expect(reloadResult.snapshot.sprites[0].maskHd.pixels[18] == 12,
           "file writer should update HD sprite mask pixels");
    expect(reloadResult.snapshot.sprites[0].colorizedHd.pixels[19] == 0x8880,
           "file writer should update HD sprite pixels");
    expect(reloadResult.snapshot.sprites[0].detectionDwords[1] == 0x87654321,
           "file writer should update sprite detection dwords");
    expect(reloadResult.snapshot.sprites[0].detectionDwordPositions[1] == 34,
           "file writer should update sprite detection dword positions");
    expect(reloadResult.snapshot.sprites[0].detectionAreas[1].width == 11,
           "file writer should update sprite detection areas");
    expect(reloadResult.snapshot.frames[0].spriteReferences[0].spriteId == 255,
           "file writer should update frame sprite assignment removals");
    expect(reloadResult.snapshot.frames[0].spriteReferences[1].spriteId == 0,
           "file writer should update frame sprite assignment additions");
    expect(reloadResult.snapshot.frames[0].spriteReferences[1].maxY == 16,
           "file writer should update frame sprite bounding boxes");
    expect(reloadResult.snapshot.frames[0].triggerId == 1234,
           "file writer should update first trigger ID");
    expect(reloadResult.snapshot.frames[1].triggerId == 65432,
           "file writer should update second trigger ID");
    expect(reloadResult.snapshot.frames[0].durationMs == 75,
           "file writer should update first cRP frame duration");
    expect(reloadResult.snapshot.frames[1].durationMs == 125,
           "file writer should update second cRP frame duration");
    expect(reloadResult.snapshot.backgrounds[0].colorizedSd.pixels[12] == 0x9999,
           "file writer should update SD background pixels");
    expect(reloadResult.snapshot.backgrounds[0].colorizedHd->pixels[13] == 0xaaaa,
           "file writer should update HD background pixels");
    expect(reloadResult.snapshot.frames[0].backgroundData->backgroundId == 0xffff,
           "file writer should update first background ID");
    expect(reloadResult.snapshot.frames[1].backgroundData->backgroundId == 0,
           "file writer should update second background ID");
    expect(reloadResult.snapshot.frames[1].backgroundData->maskSd.pixels[14] == 7,
           "file writer should update SD background masks");
    expect(reloadResult.snapshot.frames[1].backgroundData->maskHd.pixels[15] == 8,
           "file writer should update HD background masks");
    expect(!reloadResult.snapshot.frames[0].colorizedHd,
           "file writer should clear first HD presence flag");
    expect(reloadResult.snapshot.frames[1].colorizedSd.pixels[kSdPixels - 1] == 0x2468,
           "file writer should update second SD pixel");
    expect(reloadResult.snapshot.frames[1].colorizedHd.has_value(),
           "file writer should set second HD presence flag");
    expect(reloadResult.snapshot.frames[1].colorizedHd->pixels[5] == 0xbeef,
           "file writer should update second HD pixels");
    expect(reloadResult.snapshot.frames[1].colorizedSd.pixels[0] == 0 &&
               reloadResult.snapshot.frames[1].colorizedHd->pixels[0] == 0,
           "file writer should blank colorized pixels under dynamic masks");
    expect(reloadResult.snapshot.palettes[0].colors[0] == 0xabcd,
           "file writer should update cRP palette colors");
    expect(reloadResult.snapshot.palettes[1].name == "Edited palette",
           "file writer should update cRP palette names");
}

void testFileWriterCreatesProjectWithoutExistingFiles()
{
    const std::filesystem::path sourceCromPath = uniqueTempPath(".cROM");
    const std::filesystem::path sourceCrpPath = uniqueTempPath(".cRP");
    writeBytes(sourceCromPath, makeModernCromWithFrames());
    writeBytes(sourceCrpPath, makeCrpWithPalettes("file_rom"));

    const LegacyProjectFileReader reader;
    const LegacyLoadResult loadResult = reader.load(LegacyProjectPaths{
        utf8FromPath(sourceCromPath),
        utf8FromPath(sourceCrpPath),
    });
    std::filesystem::remove(sourceCromPath);
    std::filesystem::remove(sourceCrpPath);
    expect(loadResult.ok, "new project writer test initial load");

    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    const LegacyProjectPaths paths{utf8FromPath(cromPath), utf8FromPath(crpPath)};
    const LegacyProjectFileWriter writer;
    const LegacySaveResult saveResult = writer.save(paths, loadResult.snapshot);
    expect(saveResult.ok, "file writer should create files that do not exist yet");

    const LegacyLoadResult reloadResult = reader.load(paths);
    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);

    expect(reloadResult.ok, "newly created project should load");
    expect(reloadResult.snapshot.frames.size() == 2, "newly created project frame count");
    expect(reloadResult.snapshot.sprites[0].name == "Initial sprite",
           "newly created project sprite name");
    expect(reloadResult.snapshot.palettes[1].name == "Warm palette",
           "newly created project palette name");
}

void testFileWriterRequiresBothPaths()
{
    colorizing::document::ProjectSnapshot snapshot;
    const LegacyProjectFileWriter writer;
    const LegacySaveResult saveResult =
        writer.save(LegacyProjectPaths{"/tmp/only.cROM", ""}, snapshot);
    const LegacySaveResult noCrom = writer.save(LegacyProjectPaths{"", "/tmp/only.cRPX"}, snapshot);

    expect(!saveResult.ok && !noCrom.ok, "file writer should require a cROM or cRZ and a cRP");
    expect(saveResult.error.message == "a .cRZ or .cROM path and the .cRP path are required",
           "file writer missing path error");
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
}

bool savingFileExists(const std::filesystem::path& path)
{
    return std::filesystem::exists(savingPathFor(path));
}

// Builds the archive the way a project is handed around: only the .cRZ, no loose .cROM.
std::filesystem::path makeCrzFile(const std::string& suffix,
                                  const std::string& entryName,
                                  const std::vector<uint8_t>& cromBytes)
{
    const std::filesystem::path crzPath = uniqueTempPath(suffix);
    std::string error;
    const bool written = colorizing::legacy::writeCrzArchive(
        utf8FromPath(crzPath), entryName, cromBytes.size(),
        [&cromBytes](std::ostream& stream) {
            stream.write(reinterpret_cast<const char*>(cromBytes.data()),
                         static_cast<std::streamsize>(cromBytes.size()));
            return static_cast<bool>(stream);
        },
        colorizing::legacy::kCrzCompressionLevel, error);
    expect(written, "test archive should be written: " + error);
    return crzPath;
}

void testCrzExtensionIsCaseInsensitive()
{
    using colorizing::legacy::hasCrzExtension;
    expect(hasCrzExtension("/a/rom.cRZ") && hasCrzExtension("rom.crz") &&
               hasCrzExtension("rom.CRZ"),
           "cRZ extension should match in any case");
    expect(!hasCrzExtension("rom.cROM") && !hasCrzExtension("rom.cRZ.cROM") &&
               !hasCrzExtension("crz") && !hasCrzExtension(""),
           "cRZ extension should only match as the extension");
}

void testFileReaderLoadsProjectFromCrz()
{
    // The entry name is deliberately unrelated: the reader takes the archive's entry as data.
    const std::filesystem::path crzPath =
        makeCrzFile(".CRZ", "unrelated name.bin", makeModernCromWithFrames());
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    writeBytes(crpPath, makeCrpWithPalettes("file_rom"));

    const LegacyProjectFileReader reader;
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        utf8FromPath(crzPath),
        utf8FromPath(crpPath),
    });

    std::filesystem::remove(crzPath);
    std::filesystem::remove(crpPath);

    expect(result.ok, "file reader should load a project from a cRZ: " + result.error.message);
    expect(result.snapshot.romName == "file_rom", "cRZ project ROM name");
    expect(result.snapshot.frames.size() == 2, "cRZ project frame count");
    expect(result.snapshot.frames[1].colorizedSd.pixels.front() == 0x2222,
           "cRZ project colorized pixels");
    expect(result.snapshot.palettes[1].name == "Warm palette", "cRZ project cRP data");
}

void testFileReaderReportsUnreadableCrz()
{
    const std::filesystem::path crzPath = uniqueTempPath(".cRZ");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    // A cROM renamed to .cRZ is not an archive.
    writeBytes(crzPath, makeModernCromWithFrames());
    writeBytes(crpPath, makeCrpWithPalettes("file_rom"));

    const LegacyProjectFileReader reader;
    const LegacyLoadResult invalid = reader.load(LegacyProjectPaths{
        utf8FromPath(crzPath),
        utf8FromPath(crpPath),
    });
    std::filesystem::remove(crzPath);
    const LegacyLoadResult missing = reader.load(LegacyProjectPaths{
        utf8FromPath(crzPath),
        utf8FromPath(crpPath),
    });
    std::filesystem::remove(crpPath);

    expect(!invalid.ok, "a cRZ that is not an archive should fail");
    expect(invalid.error.message.find("could not read .cRZ file:") == 0,
           "invalid cRZ error message");
    expect(!missing.ok, "missing cRZ should fail");
    expect(missing.error.message.find("could not read .cRZ file:") == 0,
           "missing cRZ error message");
}

LegacyLoadResult loadSyntheticProject()
{
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    writeBytes(cromPath, makeModernCromWithFrames());
    writeBytes(crpPath, makeCrpWithPalettes("file_rom"));
    const LegacyProjectFileReader reader;
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        utf8FromPath(cromPath),
        utf8FromPath(crpPath),
    });
    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);
    expect(result.ok, "synthetic project should load");
    return result;
}

void testFileWriterWritesCrzOfTheSavedCrom()
{
    LegacyLoadResult loadResult = loadSyntheticProject();
    loadResult.snapshot.frames[0].colorizedSd.pixels[0] = 0x1357;

    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    const std::filesystem::path crzPath = uniqueTempPath(".cRZ");
    // A stale archive must be replaced, not kept or appended to.
    writeBytes(crzPath, {1, 2, 3});
    const LegacyProjectPaths paths{
        utf8FromPath(cromPath), utf8FromPath(crpPath), utf8FromPath(crzPath)};

    const LegacyProjectFileWriter writer;
    const LegacySaveResult saveResult = writer.save(paths, loadResult.snapshot);
    expect(saveResult.ok, "file writer should save with a cRZ: " + saveResult.error.message);
    expect(!savingFileExists(cromPath) && !savingFileExists(crpPath) &&
               !savingFileExists(crzPath),
           "file writer should not leave temporary files behind after writing a cRZ");

    const std::vector<uint8_t> cromBytes = readBytes(cromPath);
    std::vector<uint8_t> archivedBytes;
    std::string entryName;
    std::string error;
    const bool archiveRead =
        colorizing::legacy::readCrzArchive(utf8FromPath(crzPath), archivedBytes, error);
    const bool nameRead =
        colorizing::legacy::readCrzEntryName(utf8FromPath(crzPath), entryName, error);

    const LegacyProjectFileReader reader;
    const LegacyLoadResult reloadResult = reader.load(LegacyProjectPaths{
        utf8FromPath(crzPath),
        utf8FromPath(crpPath),
    });

    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);
    std::filesystem::remove(crzPath);

    expect(archiveRead && nameRead, "written cRZ should be readable: " + error);
    expect(!cromBytes.empty() && archivedBytes == cromBytes,
           "cRZ entry should be byte-identical to the written cROM");
    expect(entryName == "file_rom.cRom", "cRZ entry should be named <romName>.cRom");
    expect(reloadResult.ok, "project should reload from the written cRZ");
    expect(reloadResult.snapshot.frames[0].colorizedSd.pixels[0] == 0x1357,
           "project reloaded from the written cRZ should carry the edit");
}

// The editor saves a project as its .cRZ and .cRPX only: the archive gets the cROM compressed
// while it is written, no .cROM is written, not even a temporary one.
void testFileWriterSavesCrzWithoutCrom()
{
    LegacyLoadResult loadResult = loadSyntheticProject();
    const std::filesystem::path crpxPath = uniqueTempPath(".cRPX");
    const std::filesystem::path crzPath = uniqueTempPath(".cRZ");
    const LegacySaveResult saved = LegacyProjectFileWriter().save(
        LegacyProjectPaths{{}, utf8FromPath(crpxPath), utf8FromPath(crzPath)},
        loadResult.snapshot);
    expect(saved.ok, "the writer saves the .cRZ alone: " + saved.error.message);
    std::ostringstream crom;
    std::string error;
    expect(colorizing::legacy::writeLegacyCrom(crom, loadResult.snapshot, error),
           "the cROM bytes to compare with");
    std::vector<uint8_t> archived;
    expect(colorizing::legacy::readCrzArchive(utf8FromPath(crzPath), archived, error) &&
               std::string(archived.begin(), archived.end()) == crom.str(),
           "the archive holds the cROM: " + error);
    const LegacyLoadResult reloaded =
        LegacyProjectFileReader().load(LegacyProjectPaths{utf8FromPath(crzPath), utf8FromPath(crpxPath)});
    expect(reloaded.ok, "and the project opens from it: " + reloaded.error.message);
    std::filesystem::remove(crpxPath);
    std::filesystem::remove(crzPath);

    // zip64, which a cROM of 4 GiB needs, as libserum reads it.
    const std::vector<uint8_t> bytes(3000, 7);
    const std::filesystem::path zip64Path = uniqueTempPath(".cRZ");
    const auto writeBytesInto = [&bytes](std::ostream& stream) {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(stream);
    };
    expect(colorizing::legacy::writeCrzArchive(utf8FromPath(zip64Path), "rom.cRom", bytes.size(),
                                               writeBytesInto, 1, error, true),
           "a zip64 archive is written: " + error);
    std::vector<uint8_t> unpacked;
    std::string entryName;
    expect(colorizing::legacy::readCrzArchive(utf8FromPath(zip64Path), unpacked, error) &&
               unpacked == bytes &&
               colorizing::legacy::readCrzEntryName(utf8FromPath(zip64Path), entryName, error) &&
               entryName == "rom.cRom",
           "and read: " + error);
    expect(std::filesystem::file_size(zip64Path) < 300, "compressed");
    expect(!colorizing::legacy::writeCrzArchive(utf8FromPath(zip64Path), "rom.cRom",
                                                bytes.size() + 1, writeBytesInto, 1, error),
           "a cROM of another size than announced fails the archive");
    std::filesystem::remove(zip64Path);
}

void testFileWriterFailureLeavesNoTemporaryFiles()
{
    LegacyLoadResult loadResult = loadSyntheticProject();
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    const std::filesystem::path crzPath = uniqueTempPath(".cRZ");
    const std::vector<uint8_t> oldCrom = {9, 8, 7};
    const std::vector<uint8_t> oldCrz = {6, 5, 4};
    writeBytes(cromPath, oldCrom);
    writeBytes(crzPath, oldCrz);
    const LegacyProjectFileWriter writer;

    // The archive cannot be created: its directory does not exist.
    const std::filesystem::path unreachableCrz = uniqueTempPath("_missing_dir") / "rom.cRZ";
    const LegacySaveResult unreachable = writer.save(
        LegacyProjectPaths{
            utf8FromPath(cromPath), utf8FromPath(crpPath), utf8FromPath(unreachableCrz)},
        loadResult.snapshot);
    expect(!unreachable.ok, "save should fail when the cRZ cannot be created");
    expect(unreachable.error.message.find("could not write .cRZ file:") == 0,
           "unreachable cRZ error message");
    expect(!savingFileExists(cromPath) && !savingFileExists(crpPath),
           "failed cRZ creation should leave no temporary files");
    expect(readBytes(cromPath) == oldCrom && !std::filesystem::exists(crpPath),
           "failed cRZ creation should leave the project files untouched");

    // The archive is created but its entry is refused: a ROM name that is not a plain file name.
    loadResult.snapshot.romName = "bad:name";
    const LegacyProjectPaths paths{
        utf8FromPath(cromPath), utf8FromPath(crpPath), utf8FromPath(crzPath)};
    const LegacySaveResult refused = writer.save(paths, loadResult.snapshot);
    expect(!refused.ok, "save should fail when the cRZ entry cannot be written");
    expect(!savingFileExists(cromPath) && !savingFileExists(crpPath) &&
               !savingFileExists(crzPath),
           "failed cRZ entry should leave no temporary files");
    expect(readBytes(cromPath) == oldCrom && readBytes(crzPath) == oldCrz &&
               !std::filesystem::exists(crpPath),
           "failed cRZ entry should leave the project files untouched");

    // Raw cROM bytes must never land in a file named .cRZ.
    const LegacySaveResult swapped = writer.save(
        LegacyProjectPaths{utf8FromPath(crzPath), utf8FromPath(crpPath)}, loadResult.snapshot);
    expect(!swapped.ok, "a cRZ path should be refused as the cROM target");
    expect(readBytes(crzPath) == oldCrz && !savingFileExists(crzPath),
           "refused cRZ target should stay untouched");

    std::filesystem::remove(cromPath);
    std::filesystem::remove(crzPath);
}

}  // namespace

void testFileWriterSavesCrpx()
{
    const std::filesystem::path sourceCromPath = uniqueTempPath(".cROM");
    const std::filesystem::path sourceCrpPath = uniqueTempPath(".cRP");
    writeBytes(sourceCromPath, makeModernCromWithFrames());
    writeBytes(sourceCrpPath, makeCrpWithPalettes("file_rom"));
    const LegacyProjectFileReader reader;
    const LegacyLoadResult source = reader.load(
        LegacyProjectPaths{utf8FromPath(sourceCromPath), utf8FromPath(sourceCrpPath)});
    std::filesystem::remove(sourceCromPath);
    std::filesystem::remove(sourceCrpPath);
    expect(source.ok, "cRPX test initial load");

    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpxPath = uniqueTempPath(".cRPX");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    const LegacyProjectFileWriter writer;
    expect(writer.save({utf8FromPath(cromPath), utf8FromPath(crpxPath)}, source.snapshot).ok,
           "the writer saves a .cRPX");
    expect(writer.save({utf8FromPath(uniqueTempPath(".cROM")), utf8FromPath(crpPath)},
                       source.snapshot)
               .ok,
           "and a .cRP when asked for one by name");

    // The Win32 editor's cRP behind a header it does not know.
    const std::vector<uint8_t> crpx = readBytes(crpxPath);
    const std::vector<uint8_t> crp = readBytes(crpPath);
    expect(crpx.size() == colorizing::legacy::kCrpxHeaderBytes + crp.size(),
           "the .cRPX is the header and the cRP");
    expect(std::string(crpx.begin(), crpx.begin() + 8) == "CDMDcRPX" && crpx[8] == 1 &&
               crpx[12] == colorizing::legacy::kCrpxHeaderBytes,
           "the .cRPX header names the format and its version");
    uint64_t crpSize = 0;
    for (int i = 7; i >= 0; --i) {
        crpSize = (crpSize << 8) | crpx[16 + i];
    }
    expect(crpSize == crp.size() &&
               std::equal(crp.begin(), crp.end(),
                          crpx.begin() + colorizing::legacy::kCrpxHeaderBytes),
           "the cRP part is the .cRP");

    const LegacyProjectPaths paths{utf8FromPath(cromPath), utf8FromPath(crpxPath)};
    const LegacyLoadResult reloaded = reader.load(paths);
    expect(reloaded.ok && reloaded.snapshot.frames.size() == 2 &&
               reloaded.snapshot.palettes[1].name == "Warm palette",
           "the .cRPX loads");

    // Chunks of later versions after the cRP part are skipped.
    std::vector<uint8_t> extended = crpx;
    for (const char c : std::string("TEST")) {
        extended.push_back(static_cast<uint8_t>(c));
    }
    appendU32(extended, 0);
    appendU32(extended, 3);
    appendU32(extended, 0);
    appendRepeatedU8(extended, 7, 3);
    writeBytes(crpxPath, extended);
    expect(reader.load(paths).ok, "a .cRPX with a chunk it does not know loads");

    // The helper lines are a chunk of their own, read back as they were saved.
    colorizing::document::ProjectSnapshot withLines = source.snapshot;
    withLines.helperLines = {
        {"Score line", false, 24, colorizing::render::Resolution::Sd, true},
        {"Ball column", true, 200, colorizing::render::Resolution::Hd, false},
    };
    expect(writer.save(paths, withLines).ok, "a .cRPX with helper lines is saved");
    const std::vector<uint8_t> withChunk = readBytes(crpxPath);
    expect(std::string(withChunk.begin() + static_cast<std::ptrdiff_t>(crpx.size()),
                       withChunk.begin() + static_cast<std::ptrdiff_t>(crpx.size()) + 4) ==
               "HLIN",
           "the helper lines follow the cRP part as the chunk HLIN");
    const LegacyLoadResult linesBack = reader.load(paths);
    expect(linesBack.ok && linesBack.snapshot.helperLines == withLines.helperLines,
           "and come back as they were");
    std::vector<uint8_t> cutLines = withChunk;
    cutLines.resize(cutLines.size() - 3);
    writeBytes(crpxPath, cutLines);
    expect(!reader.load(paths).ok, "a .cRPX whose helper lines are cut off is refused");
    writeBytes(crpxPath, crpx);
    expect(reader.load(paths).ok && reader.load(paths).snapshot.helperLines.empty(),
           "a .cRPX without them has none");

    // The Images window's saved zones: a chunk of their own, with and without an end zone.
    colorizing::document::ProjectSnapshot withZones = source.snapshot;
    colorizing::document::SavedImageZone scroll;
    scroll.name = "Sc\xc3\xb6re";
    scroll.start = colorizing::document::PictureZone{100000, 200000, 500000, 250000};
    scroll.end = colorizing::document::PictureZone{0, 0, 1000000, 500000};
    scroll.brightness = -50;
    scroll.contrast = 20;
    scroll.blur = 10;
    colorizing::document::SavedImageZone still;
    still.name = "Logo";
    withZones.imageZones = {scroll, still};
    expect(writer.save(paths, withZones).ok, "a .cRPX with saved zones is saved");
    const LegacyLoadResult zonesBack = reader.load(paths);
    expect(zonesBack.ok && zonesBack.snapshot.imageZones == withZones.imageZones,
           "the saved zones come back as they were (chunk IZON)");
    std::vector<uint8_t> cutZones = readBytes(crpxPath);
    cutZones.resize(cutZones.size() - 2);
    writeBytes(crpxPath, cutZones);
    expect(!reader.load(paths).ok, "a .cRPX whose saved zones are cut off is refused");
    writeBytes(crpxPath, crpx);

    // The frames' tags: a chunk of their own, a frame in several groups.
    colorizing::document::ProjectSnapshot withTags = source.snapshot;
    expect(withTags.frames.size() >= 2, "the fixture has two frames to tag");
    withTags.frames[0].tags = {"Multiball", "Wizard M\xc3\xb6" "de"};
    withTags.frames[1].tags = {"Multiball"};
    expect(writer.save(paths, withTags).ok, "a .cRPX with frame tags is saved");
    const LegacyLoadResult tagsBack = reader.load(paths);
    expect(tagsBack.ok && tagsBack.snapshot.frames[0].tags == withTags.frames[0].tags &&
               tagsBack.snapshot.frames[1].tags == withTags.frames[1].tags,
           "the frame tags come back as they were (chunk FTAG)");
    std::vector<uint8_t> cutTags = readBytes(crpxPath);
    cutTags.resize(cutTags.size() - 2);
    writeBytes(crpxPath, cutTags);
    expect(!reader.load(paths).ok, "a .cRPX whose frame tags are cut off is refused");
    writeBytes(crpxPath, crpx);
    expect(reader.load(paths).ok && reader.load(paths).snapshot.frames[0].tags.empty(),
           "a .cRPX without them has no tags");

    // The PuP pack name: UTF-8 in a chunk of its own, the cRP part's UTF-16 field left empty;
    // in a .cRP UTF-16 in that field, as the Win32 editor writes it.
    colorizing::document::ProjectSnapshot withPack = source.snapshot;
    withPack.editorMetadata.hasPupPack = 1;
    withPack.editorMetadata.pupPackName = "B\xc3\xa4renj\xc3\xa4ger \xf0\x9f\x8e\xaf";
    expect(writer.save(paths, withPack).ok, "a .cRPX with a PuP pack is saved");
    const std::vector<uint8_t> withPackBytes = readBytes(crpxPath);
    expect(withPackBytes.size() > crpx.size() &&
               std::equal(crpx.begin(), crpx.end(), withPackBytes.begin()) &&
               std::string(withPackBytes.begin() + static_cast<std::ptrdiff_t>(crpx.size()),
                           withPackBytes.begin() + static_cast<std::ptrdiff_t>(crpx.size()) +
                               4) == "PUPP",
           "the pack is the chunk PUPP after an unchanged cRP part");
    const LegacyLoadResult packBack = reader.load(paths);
    expect(packBack.ok && packBack.snapshot.editorMetadata.hasPupPack == 1 &&
               packBack.snapshot.editorMetadata.pupPackName ==
                   withPack.editorMetadata.pupPackName,
           "and comes back as it was");
    const std::filesystem::path packCromPath = uniqueTempPath(".cROM");
    const LegacyProjectPaths crpPaths{utf8FromPath(packCromPath), utf8FromPath(crpPath)};
    expect(writer.save(crpPaths, withPack).ok, "a .cRP with a PuP pack is saved");
    const std::vector<uint8_t> crpWithPack = readBytes(crpPath);
    const std::vector<uint8_t> expectedField = {'B', 0, 0xe4, 0, 'r', 0, 'e', 0, 'n', 0,
                                                'j', 0, 0xe4, 0, 'g', 0, 'e', 0, 'r', 0,
                                                ' ', 0, 0x3c, 0xd8, 0xaf, 0xdf, 0, 0};
    expect(crpWithPack.size() == crp.size() &&
               std::equal(expectedField.begin(), expectedField.end(),
                          crpWithPack.end() - static_cast<std::ptrdiff_t>(
                                                  colorizing::document::kPupPackNameBytes)),
           "a .cRP holds it in UTF-16 in the Win32 editor's field");
    const LegacyLoadResult crpPackBack = reader.load(crpPaths);
    expect(crpPackBack.ok && crpPackBack.snapshot.editorMetadata.pupPackName ==
                                 withPack.editorMetadata.pupPackName,
           "and reads it back");
    const std::string longName(300, 'x');
    const std::vector<uint8_t> cut = colorizing::document::legacyPupPackNameBytes(longName);
    expect(colorizing::document::pupPackNameFromLegacyBytes(cut.data(), cut.size()) ==
               std::string(255, 'x'),
           "a name too long for the field is cut to 255 characters");
    writeBytes(crpxPath, crpx);

    // The colorization settings: the chunk SCEN with the frames' scene links, and the
    // settings block at the end of the cROM. A project without settings has neither.
    const std::vector<uint8_t> plainCrom = readBytes(cromPath);
    colorizing::document::ProjectSnapshot withScenes = source.snapshot;
    colorizing::document::ColorizationSettings settings;
    colorizing::document::SceneDefinition opening;
    opening.sceneId = 1001;
    opening.frameCount = 2;
    opening.durationMs = 40;
    opening.frameGroups = 3;
    opening.repeat = 1;
    opening.autoStartSeconds = 9;
    opening.options = colorizing::document::kSceneAsBackground;
    colorizing::document::SceneDefinition replay;
    replay.sceneId = 7;
    replay.interruptable = true;
    replay.immediateStart = true;
    replay.random = true;
    settings.scenes = {opening, replay};
    settings.scalingAlgorithm = colorizing::document::kScalingLineDoubling;
    settings.shadowOffset = colorizing::document::kShadowOffsetProportional;
    withScenes.colorizationSettings = settings;
    withScenes.frames[1].scene = colorizing::document::SceneFrameRef{1001, 3, 1};
    expect(writer.save(paths, withScenes).ok, "a .cRPX with scenes is saved");
    const std::vector<uint8_t> withScenesBytes = readBytes(crpxPath);
    expect(std::string(withScenesBytes.begin() + static_cast<std::ptrdiff_t>(crpx.size()),
                       withScenesBytes.begin() + static_cast<std::ptrdiff_t>(crpx.size()) + 4) ==
               "SCEN",
           "the scenes follow the cRP part as the chunk SCEN");
    const LegacyLoadResult scenesBack = reader.load(paths);
    expect(scenesBack.ok && scenesBack.snapshot.colorizationSettings == settings &&
               scenesBack.snapshot.frames[1].scene == withScenes.frames[1].scene &&
               !scenesBack.snapshot.frames[0].scene,
           "and come back as they were, with the frame's scene link");
    const std::vector<uint8_t> cromWithBlock = readBytes(cromPath);
    const std::vector<uint8_t> block = colorizing::legacy::cromSettingsBlock(withScenes);
    expect(cromWithBlock.size() == plainCrom.size() + block.size() &&
               std::equal(plainCrom.begin(), plainCrom.end(), cromWithBlock.begin()) &&
               std::equal(block.begin(), block.end(),
                          cromWithBlock.end() - static_cast<std::ptrdiff_t>(block.size())) &&
               std::string(block.begin(), block.begin() + 4) == "SCX1",
           "the cROM is the same with the settings block at its end");
    // magic, size, contents, count, two scenes of 13 bytes, scaling.
    expect(block.size() == 4 + 4 + 4 + 4 + 2 * 13 + 2 && block[8] == 3,
           "the block holds both scenes and the scaling settings");
    std::vector<uint8_t> cutScenes = withScenesBytes;
    cutScenes.resize(cutScenes.size() - 3);
    writeBytes(crpxPath, cutScenes);
    expect(!reader.load(paths).ok, "a .cRPX whose scenes are cut off is refused");
    colorizing::document::ProjectSnapshot noScenes = source.snapshot;
    noScenes.colorizationSettings = colorizing::document::ColorizationSettings{};
    expect(writer.save(paths, noScenes).ok, "settings without scenes are saved");
    const LegacyLoadResult noScenesBack = reader.load(paths);
    expect(noScenesBack.ok && noScenesBack.snapshot.colorizationSettings &&
               noScenesBack.snapshot.colorizationSettings->scenes.empty(),
           "and stay settings, so that a pup.csv is not read again");
    expect(writer.save(paths, source.snapshot).ok && readBytes(cromPath) == plainCrom &&
               !reader.load(paths).snapshot.colorizationSettings,
           "a project without settings writes the cROM as before");

    std::vector<uint8_t> newer = crpx;
    newer[8] = 2;
    writeBytes(crpxPath, newer);
    const LegacyLoadResult refused = reader.load(paths);
    expect(!refused.ok && refused.error.message.find("newer") != std::string::npos,
           "a .cRPX of a newer format is refused");

    std::vector<uint8_t> damaged = crpx;
    damaged[20] = 1;
    writeBytes(crpxPath, damaged);
    expect(!reader.load(paths).ok, "a .cRPX whose cRP part is cut off is refused");

    for (const std::filesystem::path& path : {cromPath, crpxPath, crpPath, packCromPath}) {
        std::filesystem::remove(path);
    }
}

void runLegacyProjectFileReaderTests()
{
    testFileWriterSavesCrpx();
    testFileReaderLoadsModernProject();
    testFileReaderReportsMissingCrom();
    testFileReaderRejectsLegacyPalettedCrom();
    testFileWriterSavesEditedModernCrom();
    testFileWriterCreatesProjectWithoutExistingFiles();
    testFileWriterRequiresBothPaths();
    testCrzExtensionIsCaseInsensitive();
    testFileReaderLoadsProjectFromCrz();
    testFileReaderReportsUnreadableCrz();
    testFileWriterWritesCrzOfTheSavedCrom();
    testFileWriterSavesCrzWithoutCrom();
    testFileWriterFailureLeavesNoTemporaryFiles();
}
