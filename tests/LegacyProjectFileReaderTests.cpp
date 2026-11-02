#include "legacy/LegacyProjectFileReader.h"
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
#include "ProjectTestFiles.h"

using colorizing::document::makeRgb565Frame;
using colorizing::legacy::LegacyLoadResult;
using colorizing::legacy::LegacyProjectFileReader;
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
std::filesystem::path fixture(const std::string& name)
{
    return colorizing::test_files::legacyFixturePath(name);
}

std::filesystem::path uniqueTempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           pathFromUtf8("colorizing_qt_v2_reader_\xce\xa9_" + run + "_" +
                        std::to_string(counter++) + suffix);
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
    // Of the monochrome range, as libserum splits one ID per frame.
    expect(result.snapshot.frames[0].monochromeTriggerId == 0x11112222 &&
               result.snapshot.frames[0].pupTriggerId == colorizing::document::kNoTriggerId,
           "file reader first trigger ID");
    expect(result.snapshot.frames[1].monochromeTriggerId == 0x33334444,
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

std::vector<uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
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
    // In capitals, as the extension's case does not matter.
    const std::filesystem::path crzPath = uniqueTempPath(".CRZ");
    std::filesystem::copy_file(fixture("crz.cRZ"), crzPath);
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

}  // namespace

// The files of tests/fixtures/legacy, written by the editor's last legacy writers: a cRP or the
// .cRPX of its later versions, with the synthetic project of this file.
void testCrpxFileHoldsTheCrp()
{
    const std::vector<uint8_t> crpx = readBytes(fixture("crpx-plain.cRPX"));
    const std::vector<uint8_t> crp = readBytes(fixture("crp-plain.cRP"));
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
}

void testFileReaderLoadsCrpx()
{
    const LegacyProjectFileReader reader;
    const std::vector<uint8_t> crpx = readBytes(fixture("crpx-plain.cRPX"));
    const std::filesystem::path crpxPath = uniqueTempPath(".cRPX");
    const LegacyProjectPaths paths{utf8FromPath(fixture("crpx-plain.cROM")),
                                   utf8FromPath(crpxPath)};
    writeBytes(crpxPath, crpx);
    const LegacyLoadResult plain = reader.load(paths);
    expect(plain.ok && plain.snapshot.frames.size() == 2 &&
               plain.snapshot.palettes[1].name == "Warm palette",
           "the .cRPX loads");
    expect(plain.snapshot.helperLines.empty() && plain.snapshot.frames[0].tags.empty() &&
               !plain.snapshot.colorizationSettings,
           "a .cRPX without chunks has no helper lines, tags or settings");

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
    std::filesystem::remove(crpxPath);
}

// Helper lines, saved zones, tags, a PuP pack and scenes, each a chunk of its own.
void testFileReaderLoadsCrpxChunks()
{
    namespace document = colorizing::document;
    const LegacyProjectFileReader reader;
    const std::vector<uint8_t> plainCrpx = readBytes(fixture("crpx-plain.cRPX"));
    const std::vector<uint8_t> crpx = readBytes(fixture("crpx-chunks.cRPX"));
    const LegacyLoadResult back = reader.load(LegacyProjectPaths{
        utf8FromPath(fixture("crpx-chunks.cROM")), utf8FromPath(fixture("crpx-chunks.cRPX"))});
    expect(back.ok, "the .cRPX with chunks loads: " + back.error.message);
    const document::ProjectSnapshot& loaded = back.snapshot;

    const std::vector<document::HelperLine> lines{
        {"Score line", false, 24, colorizing::render::Resolution::Sd, true},
        {"Ball column", true, 200, colorizing::render::Resolution::Hd, false},
    };
    expect(loaded.helperLines == lines, "the helper lines come back as they were (chunk HLIN)");

    document::SavedImageZone scroll;
    scroll.name = "Sc\xc3\xb6re";
    scroll.start = document::PictureZone{100000, 200000, 500000, 250000};
    scroll.end = document::PictureZone{0, 0, 1000000, 500000};
    scroll.brightness = -50;
    scroll.contrast = 20;
    scroll.blur = 10;
    document::SavedImageZone still;
    still.name = "Logo";
    const std::vector<document::SavedImageZone> zones{scroll, still};
    expect(loaded.imageZones == zones, "the saved zones come back as they were (chunk IZON)");

    expect(loaded.frames[0].tags ==
                   std::vector<std::string>{"Multiball", "Wizard M\xc3\xb6" "de"} &&
               loaded.frames[1].tags == std::vector<std::string>{"Multiball"},
           "the frame tags come back as they were (chunk FTAG)");

    const std::string packName = "B\xc3\xa4renj\xc3\xa4ger \xf0\x9f\x8e\xaf";
    expect(loaded.editorMetadata.hasPupPack == 1 && loaded.editorMetadata.pupPackName == packName,
           "the PuP pack comes back as it was (chunk PUPP)");

    document::ColorizationSettings settings;
    document::SceneDefinition opening;
    opening.sceneId = 1001;
    opening.frameCount = 2;
    opening.durationMs = 40;
    opening.frameGroups = 3;
    opening.repeat = 1;
    opening.autoStartSeconds = 9;
    opening.options = document::kSceneAsBackground;
    document::SceneDefinition replay;
    replay.sceneId = 7;
    replay.interruptable = true;
    replay.immediateStart = true;
    replay.random = true;
    settings.scenes = {opening, replay};
    settings.scalingAlgorithm = document::kScalingLineDoubling;
    settings.shadowOffset = document::kShadowOffsetProportional;
    expect(loaded.colorizationSettings == settings &&
               loaded.frames[1].scene == document::SceneFrameRef{1001, 3, 1} &&
               !loaded.frames[0].scene,
           "the scenes come back as they were, with the frame's scene link (chunk SCEN)");

    // The settings block at the end of the cROM: the same cROM otherwise.
    const std::vector<uint8_t> block = colorizing::legacy::cromSettingsBlock(loaded);
    const std::vector<uint8_t> crom = readBytes(fixture("crpx-chunks.cROM"));
    const std::vector<uint8_t> plainCrom = readBytes(fixture("crpx-plain.cROM"));
    expect(std::string(block.begin(), block.begin() + 4) == "SCX1" &&
               std::equal(block.begin(), block.end(),
                          crom.end() - static_cast<std::ptrdiff_t>(block.size())) &&
               std::equal(plainCrom.begin(), plainCrom.end(), crom.begin()),
           "the cROM has the settings block at its end");
    // magic, size, contents, count, two scenes of 13 bytes, scaling, then the frames of
    // scenes: count, frame 1 as frame 1 of group 3 of scene 1001.
    const std::size_t sceneFrames = 4 + 4 + 4 + 4 + 2 * 13 + 2;
    expect(block.size() == sceneFrames + 4 + 10 && block[8] == (3 | 16),
           "the block holds both scenes, the scaling settings and the frames of scenes");
    const std::vector<uint8_t> frameOfScene{1, 0, 0, 0, 1, 0, 0, 0, 0xe9, 0x03, 1, 0, 3, 0};
    expect(std::equal(frameOfScene.begin(), frameOfScene.end(),
                      block.begin() + static_cast<std::ptrdiff_t>(sceneFrames)),
           "frame 1 is listed with its scene, frame index and group");

    // Cut anywhere in its last chunk, a .cRPX is refused; without chunks it is whole again.
    std::vector<uint8_t> cut = crpx;
    cut.resize(cut.size() - 3);
    const std::filesystem::path cutPath = uniqueTempPath(".cRPX");
    writeBytes(cutPath, cut);
    expect(!reader.load(LegacyProjectPaths{utf8FromPath(fixture("crpx-chunks.cROM")),
                                           utf8FromPath(cutPath)})
                .ok,
           "a .cRPX whose last chunk is cut off is refused");
    std::filesystem::remove(cutPath);
    expect(crpx.size() > plainCrpx.size(), "the chunks follow the cRP part");

    const LegacyLoadResult noScenes = reader.load(LegacyProjectPaths{
        utf8FromPath(fixture("crpx-noscenes.cROM")), utf8FromPath(fixture("crpx-noscenes.cRPX"))});
    expect(noScenes.ok && noScenes.snapshot.colorizationSettings &&
               noScenes.snapshot.colorizationSettings->scenes.empty(),
           "settings without scenes stay settings, so that a pup.csv is not read again");
}

// In a .cRP the PuP pack name is UTF-16 in the Win32 editor's field.
void testFileReaderLoadsPupPackOfCrp()
{
    const std::vector<uint8_t> crp = readBytes(fixture("crp-pup.cRP"));
    const std::vector<uint8_t> expectedField = {'B', 0, 0xe4, 0, 'r', 0, 'e', 0, 'n', 0,
                                                'j', 0, 0xe4, 0, 'g', 0, 'e', 0, 'r', 0,
                                                ' ', 0, 0x3c, 0xd8, 0xaf, 0xdf, 0, 0};
    expect(std::equal(expectedField.begin(), expectedField.end(),
                      crp.end() - static_cast<std::ptrdiff_t>(
                                      colorizing::document::kPupPackNameBytes)),
           "a .cRP holds it in UTF-16 in the Win32 editor's field");
    const LegacyLoadResult back = LegacyProjectFileReader().load(LegacyProjectPaths{
        utf8FromPath(fixture("crp-pup.cROM")), utf8FromPath(fixture("crp-pup.cRP"))});
    expect(back.ok && back.snapshot.editorMetadata.pupPackName ==
                          "B\xc3\xa4renj\xc3\xa4ger \xf0\x9f\x8e\xaf",
           "and reads it back");
    const std::string longName(300, 'x');
    const std::vector<uint8_t> cut = colorizing::document::legacyPupPackNameBytes(longName);
    expect(colorizing::document::pupPackNameFromLegacyBytes(cut.data(), cut.size()) ==
               std::string(255, 'x'),
           "a name too long for the field is cut to 255 characters");
}

// A project for libserum 2.9: the cROM of format version 1 (header revision 21, 128 slots,
// 16-bit sprite IDs), the sprites beyond the cRP part's 255 in .cRPX chunks. Its big cROM is
// in a .cRZ, as such projects were handed around.
void testFileReaderLoadsLibserum29Project()
{
    namespace document = colorizing::document;
    const LegacyLoadResult back = LegacyProjectFileReader().load(LegacyProjectPaths{
        utf8FromPath(fixture("crpx-libserum29.cRZ")),
        utf8FromPath(fixture("crpx-libserum29.cRPX"))});
    expect(back.ok, "a libserum 2.9 project loads: " + back.error.message);
    const document::ProjectSnapshot& loaded = back.snapshot;
    expect(loaded.sprites.size() == 300 &&
               loaded.frames[0].spriteReferences.size() == 128 &&
               loaded.frames[0].spriteReferences[100].spriteId == 299 &&
               loaded.frames[0].spriteReferences[100].maxX == 30 &&
               loaded.frames[0].spriteReferences[101].spriteId == document::kNoSpriteId,
           "as one for libserum 2.9, with sprite 299 in slot 100");
    expect(loaded.sprites[299].name == "Gr\xc3\xbcn 299" &&
               loaded.editorMetadata.spriteSourceFrames.size() == 300 &&
               loaded.editorMetadata.spriteSourceFrames[299] == 1 &&
               loaded.editorMetadata.spriteSourceRects[299 * 4 + 3] == 13 &&
               loaded.editorMetadata.spriteSourceMirrors[299 * 2 + 1] == 1,
           "sprite 299 keeps its name, source frame, rectangle and mirrors (chunk SPRX)");
    expect(loaded.frames[0].originalOnlyDynamicSets == 0x24 &&
               loaded.frames[1].originalOnlyDynamicSets == 0,
           "frame 0 keeps its sets that color the SD picture only (chunk DSSD)");

    // The cRP part has the size of one with 255 sprites: what it has no room for is in chunks.
    const std::vector<uint8_t> crpx = readBytes(fixture("crpx-libserum29.cRPX"));
    uint64_t crpSize = 0;
    for (int i = 7; i >= 0; --i) {
        crpSize = (crpSize << 8) | crpx[16 + i];
    }
    expect(crpSize == readBytes(fixture("crp-plain.cRP")).size(),
           "the cRP part keeps the Win32 editor's size");
    const std::string chunks(crpx.begin() + static_cast<std::ptrdiff_t>(
                                                colorizing::legacy::kCrpxHeaderBytes + crpSize),
                             crpx.end());
    expect(chunks.find("SPRX") != std::string::npos, "the sprites beyond 255 are in a chunk");
}

// zip64, which a cROM of 4 GiB needs, as libserum reads it.
void testFileReaderReadsZip64Crz()
{
    const std::string path = utf8FromPath(fixture("zip64.cRZ"));
    std::vector<uint8_t> unpacked;
    std::string entryName;
    std::string error;
    expect(colorizing::legacy::readCrzArchive(path, unpacked, error) &&
               unpacked == std::vector<uint8_t>(3000, 7) &&
               colorizing::legacy::readCrzEntryName(path, entryName, error) &&
               entryName == "rom.cRom",
           "a zip64 archive is read: " + error);
}

void runLegacyProjectFileReaderTests()
{
    testCrpxFileHoldsTheCrp();
    testFileReaderLoadsCrpx();
    testFileReaderLoadsCrpxChunks();
    testFileReaderLoadsPupPackOfCrp();
    testFileReaderLoadsLibserum29Project();
    testFileReaderReadsZip64Crz();
    testFileReaderLoadsModernProject();
    testFileReaderReportsMissingCrom();
    testFileReaderRejectsLegacyPalettedCrom();
    testCrzExtensionIsCaseInsensitive();
    testFileReaderLoadsProjectFromCrz();
    testFileReaderReportsUnreadableCrz();
}
