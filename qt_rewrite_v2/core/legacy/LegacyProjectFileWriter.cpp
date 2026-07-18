#include "legacy/LegacyProjectFileWriter.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "legacy/LegacyCromFrames.h"
#include "render/CanvasLayout.h"

namespace colorizing::legacy {

namespace {

struct FileReadResult {
    std::vector<uint8_t> bytes;
    std::string errorMessage;
    bool ok = false;
};

FileReadResult readFileBytes(const std::string& path, const char* label)
{
    if (path.empty()) {
        return FileReadResult{{}, std::string(label) + " path is empty", false};
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return FileReadResult{{}, "could not open " + std::string(label) + " file: " + path, false};
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (!stream.eof() && stream.fail()) {
        return FileReadResult{{}, "could not read " + std::string(label) + " file: " + path, false};
    }
    return FileReadResult{std::move(bytes), "", true};
}

bool writeFileBytes(const std::string& path,
                    const std::vector<uint8_t>& bytes,
                    std::string& errorMessage)
{
    if (path.empty()) {
        errorMessage = ".cROM path is empty";
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        errorMessage = "could not open .cROM file for writing: " + path;
        return false;
    }

    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        errorMessage = "could not write .cROM file: " + path;
        return false;
    }
    return true;
}

void writeU16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

bool validateFrameShape(const document::Rgb565Frame& frame,
                        render::Size expectedSize,
                        const char* label,
                        std::string& errorMessage)
{
    if (frame.size.width != expectedSize.width || frame.size.height != expectedSize.height ||
        frame.pixels.size() != static_cast<std::size_t>(expectedSize.width) *
                                   static_cast<std::size_t>(expectedSize.height)) {
        errorMessage = std::string(label) + " frame dimensions do not match existing cROM";
        return false;
    }
    return true;
}

bool validateSnapshotAgainstTemplate(const document::ProjectSnapshot& snapshot,
                                     const LegacyCromFrameData& frameData,
                                     std::string& errorMessage)
{
    if (snapshot.romName != frameData.metadata.romName) {
        errorMessage = "snapshot ROM name does not match existing cROM";
        return false;
    }
    if (snapshot.frames.size() != frameData.metadata.frameCount) {
        errorMessage = "snapshot frame count does not match existing cROM";
        return false;
    }
    if (snapshot.comparisonMasks.size() != frameData.metadata.comparisonMaskCount) {
        errorMessage = "snapshot comparison mask count does not match existing cROM";
        return false;
    }

    const render::Size sdSize{static_cast<int>(frameData.metadata.frameWidth),
                              static_cast<int>(frameData.metadata.frameHeight)};
    const render::Size hdSize{static_cast<int>(frameData.metadata.hdFrameWidth),
                              static_cast<int>(frameData.metadata.hdFrameHeight)};
    for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
        if (!validateFrameShape(frame.colorizedSd, sdSize, "SD colorized", errorMessage)) {
            return false;
        }
        if (frame.colorizedHd &&
            !validateFrameShape(*frame.colorizedHd, hdSize, "HD colorized", errorMessage)) {
            return false;
        }
    }
    for (const document::IndexedFrame& mask : snapshot.comparisonMasks) {
        if (mask.size.width != sdSize.width || mask.size.height != sdSize.height ||
            mask.pixels.size() != static_cast<std::size_t>(sdSize.width) *
                                      static_cast<std::size_t>(sdSize.height)) {
            errorMessage = "comparison mask dimensions do not match existing cROM";
            return false;
        }
    }
    return true;
}

void writeRgb565Frame(std::vector<uint8_t>& bytes,
                      std::size_t offset,
                      const document::Rgb565Frame& frame)
{
    for (uint16_t pixel : frame.pixels) {
        writeU16(bytes, offset, pixel);
        offset += sizeof(uint16_t);
    }
}

}  // namespace

LegacySaveResult LegacyProjectFileWriter::save(const LegacyProjectPaths& paths,
                                               const document::ProjectSnapshot& snapshot) const
{
    const FileReadResult cromFile = readFileBytes(paths.cromPath, ".cROM");
    if (!cromFile.ok) {
        return makeSaveFailure(cromFile.errorMessage);
    }

    const LegacyCromFrameParseResult cromFrames =
        parseModernLegacyCromFrames(cromFile.bytes);
    if (!cromFrames.ok) {
        return makeSaveFailure(cromFrames.errorMessage);
    }

    std::string errorMessage;
    if (!validateSnapshotAgainstTemplate(snapshot, cromFrames.frameData, errorMessage)) {
        return makeSaveFailure(errorMessage);
    }

    std::vector<uint8_t> bytes = cromFile.bytes;
    const std::size_t frameCount = snapshot.frames.size();
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        bytes[cromFrames.frameData.shapeComparisonModesOffset + frameIndex] =
            snapshot.frames[frameIndex].shapeComparisonMode;
        bytes[cromFrames.frameData.comparisonMaskIdsOffset + frameIndex] =
            snapshot.frames[frameIndex].comparisonMaskId;
        bytes[cromFrames.frameData.extraFrameFlagsOffset + frameIndex] =
            snapshot.frames[frameIndex].colorizedHd ? uint8_t{1} : uint8_t{0};

        const std::size_t sdOffset =
            cromFrames.frameData.colorizedSdFramesOffset +
            frameIndex * snapshot.frames[frameIndex].colorizedSd.pixels.size() * sizeof(uint16_t);
        writeRgb565Frame(bytes, sdOffset, snapshot.frames[frameIndex].colorizedSd);

        if (snapshot.frames[frameIndex].colorizedHd) {
            const std::size_t hdOffset =
                cromFrames.frameData.colorizedHdFramesOffset +
                frameIndex * snapshot.frames[frameIndex].colorizedHd->pixels.size() *
                    sizeof(uint16_t);
            writeRgb565Frame(bytes, hdOffset, *snapshot.frames[frameIndex].colorizedHd);
        }
    }

    std::size_t maskOffset = cromFrames.frameData.comparisonMasksOffset;
    for (const document::IndexedFrame& mask : snapshot.comparisonMasks) {
        for (uint8_t pixel : mask.pixels) {
            bytes[maskOffset++] = pixel;
        }
    }

    if (!writeFileBytes(paths.cromPath, bytes, errorMessage)) {
        return makeSaveFailure(errorMessage);
    }
    return makeSaveSuccess();
}

}  // namespace colorizing::legacy
