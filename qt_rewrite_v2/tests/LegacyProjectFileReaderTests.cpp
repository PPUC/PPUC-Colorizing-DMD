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
    appendU32(bytes, 0);
    appendU16(bytes, 0);

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
    writeBytes(crpPath, makeCrpOriginals("file_rom"));

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
    writeBytes(crpPath, makeCrpOriginals("file_rom"));

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
