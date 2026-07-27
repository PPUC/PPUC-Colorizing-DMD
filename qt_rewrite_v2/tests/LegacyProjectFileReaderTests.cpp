#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

#include "render/CanvasLayout.h"

using colorizing::document::makeRgb565Frame;
using colorizing::legacy::LegacyLoadResult;
using colorizing::legacy::LegacyProjectFileReader;
using colorizing::legacy::LegacyProjectFileWriter;
using colorizing::legacy::LegacyProjectPaths;
using colorizing::legacy::LegacySaveResult;

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
    appendRepeatedU16(bytes, 0, 2 * kMaxColorRotations * kMaxColorRotationLength);
    appendRepeatedU16(bytes, 0, 2 * kMaxColorRotations * kMaxColorRotationLength);
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
    appendFixedName(bytes, "Initial sprite", 32);
    for (std::size_t i = 1; i < 255; ++i) {
        appendFixedName(bytes, "", 32);
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

std::filesystem::path uniqueTempPath(const std::string& suffix)
{
    static int counter = 0;
    return std::filesystem::temp_directory_path() /
           ("colorizing_qt_v2_reader_" + std::to_string(::getpid()) + "_" +
            std::to_string(counter++) + suffix);
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        fail("could not create temp file: " + path.string());
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        fail("could not write temp file: " + path.string());
    }
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        fail("could not open temp file: " + path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
}

void testFileReaderLoadsModernProject()
{
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    const std::filesystem::path crpPath = uniqueTempPath(".cRP");
    writeBytes(cromPath, makeModernCromWithFrames());
    writeBytes(crpPath, makeCrpWithSpriteNames("file_rom"));

    const LegacyProjectFileReader reader;
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        cromPath.string(),
        crpPath.string(),
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
        cromPath.string(),
        crpPath.string(),
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
    writeBytes(crpPath, makeCrpWithSpriteNames("file_rom"));

    const LegacyProjectPaths paths{
        cromPath.string(),
        crpPath.string(),
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
    loadResult.snapshot.backgrounds[0].colorizedSd.pixels[12] = 0x9999;
    loadResult.snapshot.backgrounds[0].colorizedHd->pixels[13] = 0xaaaa;
    loadResult.snapshot.frames[0].backgroundData->backgroundId = 0xffff;
    loadResult.snapshot.frames[1].backgroundData->backgroundId = 0;
    loadResult.snapshot.frames[1].backgroundData->maskSd.pixels[14] = 7;
    loadResult.snapshot.frames[1].backgroundData->maskHd.pixels[15] = 8;
    loadResult.snapshot.frames[0].colorizedHd.reset();
    loadResult.snapshot.frames[1].colorizedSd.pixels[kSdPixels - 1] = 0x2468;
    loadResult.snapshot.frames[1].colorizedHd =
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0xcafe);
    loadResult.snapshot.frames[1].colorizedHd->pixels[5] = 0xbeef;

    const LegacyProjectFileWriter writer;
    const LegacySaveResult saveResult = writer.save(paths, loadResult.snapshot);
    expect(saveResult.ok, "file writer should save edited modern cROM");

    const std::vector<uint8_t> savedBytes = readBytes(cromPath);
    expect(savedBytes.size() >= 2 && savedBytes[savedBytes.size() - 2] == 0x99 &&
               savedBytes[savedBytes.size() - 1] == 0x88,
           "file writer should preserve trailing bytes");

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
}

void testFileWriterRejectsMismatchedTemplate()
{
    const std::filesystem::path cromPath = uniqueTempPath(".cROM");
    writeBytes(cromPath, makeModernCromWithFrames());

    const LegacyProjectFileReader reader;
    LegacyLoadResult loadResult = reader.load(LegacyProjectPaths{
        cromPath.string(),
        cromPath.string(),
    });
    loadResult.snapshot.romName = "other_rom";

    const LegacyProjectFileWriter writer;
    const LegacySaveResult saveResult = writer.save(LegacyProjectPaths{
        cromPath.string(),
        "",
    }, loadResult.snapshot);

    std::filesystem::remove(cromPath);

    expect(!saveResult.ok, "file writer should reject mismatched ROM name");
    expect(saveResult.error.message == "snapshot ROM name does not match existing cROM",
           "file writer mismatch error");
}

}  // namespace

void runLegacyProjectFileReaderTests()
{
    testFileReaderLoadsModernProject();
    testFileReaderReportsMissingCrom();
    testFileReaderRejectsLegacyPalettedCrom();
    testFileWriterSavesEditedModernCrom();
    testFileWriterRejectsMismatchedTemplate();
}
