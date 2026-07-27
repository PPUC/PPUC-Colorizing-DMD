#include "legacy/LegacyProjectFileWriter.h"

#include <cstddef>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "legacy/LegacyCromFrames.h"
#include "render/CanvasLayout.h"

namespace colorizing::legacy {

namespace {

constexpr std::size_t kCrpRomNameBytes = 64;
constexpr std::size_t kCrpBoolBytes = 4;
constexpr std::size_t kCrpU32Bytes = 4;
constexpr std::size_t kCrpIntBytes = 4;
constexpr std::size_t kCrpMaxColorSets = 64;
constexpr std::size_t kCrpColorSetColorCount = 16;
constexpr std::size_t kCrpMaskCount = 64;
constexpr std::size_t kCrpMaskNameBytes = 32;
constexpr std::size_t kCrpMaxSections = 512;
constexpr std::size_t kCrpSectionNameBytes = 32;
constexpr std::size_t kCrpMaxSpriteNames = 255;
constexpr std::size_t kCrpSpriteNameBytes = 32;

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
                    std::string& errorMessage,
                    const char* label = ".cROM")
{
    if (path.empty()) {
        errorMessage = std::string(label) + " path is empty";
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        errorMessage = "could not open " + std::string(label) + " file for writing: " + path;
        return false;
    }

    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        errorMessage = "could not write " + std::string(label) + " file: " + path;
        return false;
    }
    return true;
}

std::size_t crpSpriteNamesOffset(const LegacyCromMetadata& metadata)
{
    const std::size_t frameBytes =
        static_cast<std::size_t>(metadata.frameWidth) *
        static_cast<std::size_t>(metadata.frameHeight);
    const std::size_t beforeSpriteNamesBytes =
        kCrpMaxColorSets * kCrpBoolBytes +
        kCrpMaxColorSets * kCrpColorSetColorCount +
        1 +
        1 +
        kCrpMaxColorSets * kCrpRomNameBytes +
        kCrpU32Bytes +
        1 +
        kCrpIntBytes +
        kCrpBoolBytes +
        kCrpMaskCount * kCrpMaskNameBytes +
        kCrpU32Bytes +
        kCrpMaxSections * kCrpU32Bytes +
        kCrpMaxSections * kCrpSectionNameBytes;
    return kCrpRomNameBytes +
           metadata.frameCount * frameBytes +
           beforeSpriteNamesBytes;
}

std::size_t crpFrameDurationsOffset(const LegacyCromMetadata& metadata)
{
    return crpSpriteNamesOffset(metadata) +
           kCrpMaxSpriteNames * kCrpSpriteNameBytes +
           kCrpMaxSpriteNames * kCrpU32Bytes;
}

bool writeCrpSpriteNames(const LegacyProjectPaths& paths,
                         const LegacyCromMetadata& metadata,
                         const document::ProjectSnapshot& snapshot,
                         std::string& errorMessage)
{
    const FileReadResult crpFile = readFileBytes(paths.crpPath, ".cRP");
    if (!crpFile.ok) {
        errorMessage = crpFile.errorMessage;
        return false;
    }

    std::vector<uint8_t> crpBytes = crpFile.bytes;
    const std::size_t frameBytes =
        static_cast<std::size_t>(metadata.frameWidth) *
        static_cast<std::size_t>(metadata.frameHeight);
    const std::size_t originalFramesEnd =
        kCrpRomNameBytes + metadata.frameCount * frameBytes;
    const std::size_t namesOffset = crpSpriteNamesOffset(metadata);
    const std::size_t namesBytes = kCrpMaxSpriteNames * kCrpSpriteNameBytes;
    if (crpBytes.size() <= originalFramesEnd) {
        return true;
    }
    if (namesOffset > crpBytes.size()) {
        errorMessage = ".cRP is truncated before sprite names";
        return false;
    }
    if (crpBytes.size() < namesOffset + namesBytes) {
        return true;
    }

    for (std::size_t spriteIndex = 0;
         spriteIndex < snapshot.sprites.size() && spriteIndex < kCrpMaxSpriteNames;
         ++spriteIndex) {
        const std::string& name = snapshot.sprites[spriteIndex].name;
        const std::size_t offset = namesOffset + spriteIndex * kCrpSpriteNameBytes;
        const std::size_t count =
            std::min(name.size(), kCrpSpriteNameBytes - 1);
        std::fill(crpBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                  crpBytes.begin() + static_cast<std::ptrdiff_t>(offset + kCrpSpriteNameBytes),
                  uint8_t{0});
        std::copy_n(reinterpret_cast<const uint8_t*>(name.data()),
                    count,
                    crpBytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    const std::size_t durationsOffset = crpFrameDurationsOffset(metadata);
    const std::size_t durationsBytes =
        static_cast<std::size_t>(metadata.frameCount) * kCrpU32Bytes;
    if (durationsOffset <= crpBytes.size() &&
        crpBytes.size() >= durationsOffset + durationsBytes) {
        const std::size_t frameCount =
            std::min<std::size_t>(snapshot.frames.size(), metadata.frameCount);
        for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            const uint32_t value = snapshot.frames[frameIndex].durationMs;
            const std::size_t offset = durationsOffset + frameIndex * kCrpU32Bytes;
            crpBytes[offset] = static_cast<uint8_t>(value & 0xff);
            crpBytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
            crpBytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
            crpBytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
        }
    }

    if (!writeFileBytes(paths.crpPath, crpBytes, errorMessage, ".cRP")) {
        return false;
    }
    return true;
}

void writeU16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void writeU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
{
    bytes[offset] = static_cast<uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
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

bool validateIndexedFrameShape(const document::IndexedFrame& frame,
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
    if (snapshot.backgrounds.size() != frameData.metadata.backgroundCount) {
        errorMessage = "snapshot background count does not match existing cROM";
        return false;
    }
    if (snapshot.sprites.size() != frameData.metadata.spriteCount) {
        errorMessage = "snapshot sprite count does not match existing cROM";
        return false;
    }

    const render::Size sdSize{static_cast<int>(frameData.metadata.frameWidth),
                              static_cast<int>(frameData.metadata.frameHeight)};
    const render::Size hdSize{static_cast<int>(frameData.metadata.hdFrameWidth),
                              static_cast<int>(frameData.metadata.hdFrameHeight)};
    const std::size_t dynamicColorCount =
        document::kMaxDynamicSetsPerFrame *
        static_cast<std::size_t>(frameData.metadata.colorCount);
    for (const document::ProjectFrameSnapshot& frame : snapshot.frames) {
        if (!validateFrameShape(frame.colorizedSd, sdSize, "SD colorized", errorMessage)) {
            return false;
        }
        if (frame.colorizedHd &&
            !validateFrameShape(*frame.colorizedHd, hdSize, "HD colorized", errorMessage)) {
            return false;
        }
        if (frame.dynamicData) {
            if (!validateIndexedFrameShape(frame.dynamicData->maskSd,
                                           sdSize,
                                           "SD dynamic mask",
                                           errorMessage) ||
                !validateIndexedFrameShape(frame.dynamicData->maskHd,
                                           hdSize,
                                           "HD dynamic mask",
                                           errorMessage)) {
                return false;
            }
            if (frame.dynamicData->colorsSd.size() != dynamicColorCount ||
                frame.dynamicData->colorsHd.size() != dynamicColorCount) {
                errorMessage = "dynamic color set dimensions do not match existing cROM";
                return false;
            }
        }
        if (!frame.backgroundData) {
            errorMessage = "snapshot frame is missing background data";
            return false;
        }
        if (!validateIndexedFrameShape(frame.backgroundData->maskSd,
                                       sdSize,
                                       "SD background mask",
                                       errorMessage) ||
            !validateIndexedFrameShape(frame.backgroundData->maskHd,
                                       hdSize,
                                       "HD background mask",
                                       errorMessage)) {
            return false;
        }
        if (frame.spriteReferences.size() != document::kMaxSpritesPerFrame) {
            errorMessage = "frame sprite reference count does not match existing cROM";
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
    for (const document::BackgroundRecord& background : snapshot.backgrounds) {
        if (!validateFrameShape(background.colorizedSd, sdSize, "SD background", errorMessage)) {
            return false;
        }
        if (background.colorizedHd &&
            !validateFrameShape(*background.colorizedHd, hdSize, "HD background", errorMessage)) {
            return false;
        }
    }
    const render::Size spriteSize{static_cast<int>(document::kMaxSpriteWidth),
                                  static_cast<int>(document::kMaxSpriteHeight)};
    for (const document::SpriteRecord& sprite : snapshot.sprites) {
        if (!validateIndexedFrameShape(sprite.originalSd,
                                       spriteSize,
                                       "sprite original",
                                       errorMessage) ||
            !validateFrameShape(sprite.colorizedSd,
                                spriteSize,
                                "SD sprite",
                                errorMessage) ||
            !validateIndexedFrameShape(sprite.maskHd,
                                       spriteSize,
                                       "HD sprite mask",
                                       errorMessage) ||
            !validateFrameShape(sprite.colorizedHd,
                                spriteSize,
                                "HD sprite",
                                errorMessage)) {
            return false;
        }
        if (sprite.detectionDwords.size() != document::kMaxSpriteDetectAreas ||
            sprite.detectionDwordPositions.size() != document::kMaxSpriteDetectAreas ||
            sprite.detectionAreas.size() != document::kMaxSpriteDetectAreas) {
            errorMessage = "sprite detection metadata does not match existing cROM";
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

void writeIndexedFrame(std::vector<uint8_t>& bytes,
                       std::size_t offset,
                       const document::IndexedFrame& frame)
{
    for (uint8_t pixel : frame.pixels) {
        bytes[offset++] = pixel;
    }
}

void writeRgb565Values(std::vector<uint8_t>& bytes,
                       std::size_t offset,
                       const std::vector<uint16_t>& values)
{
    for (uint16_t value : values) {
        writeU16(bytes, offset, value);
        offset += sizeof(uint16_t);
    }
}

void writeU8ValuesOrZeros(std::vector<uint8_t>& bytes,
                          std::size_t offset,
                          const std::vector<uint8_t>& values,
                          std::size_t expectedCount)
{
    for (std::size_t index = 0; index < expectedCount; ++index) {
        bytes[offset + index] = index < values.size() ? values[index] : uint8_t{0};
    }
}

void writeRgb565ValuesOrZeros(std::vector<uint8_t>& bytes,
                              std::size_t offset,
                              const std::vector<uint16_t>& values,
                              std::size_t expectedCount)
{
    for (std::size_t index = 0; index < expectedCount; ++index) {
        writeU16(bytes,
                 offset + index * sizeof(uint16_t),
                 index < values.size() ? values[index] : uint16_t{0});
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

    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        if (!snapshot.frames[frameIndex].dynamicData) {
            continue;
        }
        const document::FrameDynamicData& dynamicData = *snapshot.frames[frameIndex].dynamicData;
        const std::size_t sdMaskOffset =
            cromFrames.frameData.dynamicMasksSdOffset +
            frameIndex * dynamicData.maskSd.pixels.size();
        writeIndexedFrame(bytes, sdMaskOffset, dynamicData.maskSd);

        const std::size_t hdMaskOffset =
            cromFrames.frameData.dynamicMasksHdOffset +
            frameIndex * dynamicData.maskHd.pixels.size();
        writeIndexedFrame(bytes, hdMaskOffset, dynamicData.maskHd);

        const std::size_t sdColorsOffset =
            cromFrames.frameData.dynamicColorsSdOffset +
            frameIndex * dynamicData.colorsSd.size() * sizeof(uint16_t);
        writeRgb565Values(bytes, sdColorsOffset, dynamicData.colorsSd);

        const std::size_t hdColorsOffset =
            cromFrames.frameData.dynamicColorsHdOffset +
            frameIndex * dynamicData.colorsHd.size() * sizeof(uint16_t);
        writeRgb565Values(bytes, hdColorsOffset, dynamicData.colorsHd);

        const std::size_t shadowCount = document::kMaxDynamicSetsPerFrame;
        writeU8ValuesOrZeros(bytes,
                             cromFrames.frameData.dynamicShadowDirectionsSdOffset +
                                 frameIndex * shadowCount,
                             dynamicData.shadowDirectionsSd,
                             shadowCount);
        writeRgb565ValuesOrZeros(bytes,
                                 cromFrames.frameData.dynamicShadowColorsSdOffset +
                                     frameIndex * shadowCount * sizeof(uint16_t),
                                 dynamicData.shadowColorsSd,
                                 shadowCount);
        writeU8ValuesOrZeros(bytes,
                             cromFrames.frameData.dynamicShadowDirectionsHdOffset +
                                 frameIndex * shadowCount,
                             dynamicData.shadowDirectionsHd,
                             shadowCount);
        writeRgb565ValuesOrZeros(bytes,
                                 cromFrames.frameData.dynamicShadowColorsHdOffset +
                                     frameIndex * shadowCount * sizeof(uint16_t),
                                 dynamicData.shadowColorsHd,
                                 shadowCount);
    }

    const std::size_t spritePixels =
        document::kMaxSpriteWidth * document::kMaxSpriteHeight;
    for (std::size_t spriteIndex = 0; spriteIndex < snapshot.sprites.size(); ++spriteIndex) {
        const document::SpriteRecord& sprite = snapshot.sprites[spriteIndex];
        bytes[cromFrames.frameData.extraSpriteFlagsOffset + spriteIndex] =
            sprite.hasHd ? uint8_t{1} : uint8_t{0};

        writeIndexedFrame(bytes,
                          cromFrames.frameData.spriteOriginalsOffset +
                              spriteIndex * spritePixels,
                          sprite.originalSd);
        writeRgb565Frame(bytes,
                         cromFrames.frameData.spriteColorsSdOffset +
                             spriteIndex * spritePixels * sizeof(uint16_t),
                         sprite.colorizedSd);
        writeIndexedFrame(bytes,
                          cromFrames.frameData.spriteMasksHdOffset +
                              spriteIndex * spritePixels,
                          sprite.maskHd);
        writeRgb565Frame(bytes,
                         cromFrames.frameData.spriteColorsHdOffset +
                             spriteIndex * spritePixels * sizeof(uint16_t),
                         sprite.colorizedHd);

        for (std::size_t areaIndex = 0; areaIndex < document::kMaxSpriteDetectAreas;
             ++areaIndex) {
            writeU32(bytes,
                     cromFrames.frameData.spriteDetectionDwordsOffset +
                         (spriteIndex * document::kMaxSpriteDetectAreas + areaIndex) *
                             sizeof(uint32_t),
                     sprite.detectionDwords[areaIndex]);
            writeU16(bytes,
                     cromFrames.frameData.spriteDetectionDwordPositionsOffset +
                         (spriteIndex * document::kMaxSpriteDetectAreas + areaIndex) *
                             sizeof(uint16_t),
                     sprite.detectionDwordPositions[areaIndex]);

            const document::SpriteDetectionArea& area = sprite.detectionAreas[areaIndex];
            const std::size_t areaOffset =
                cromFrames.frameData.spriteDetectionAreasOffset +
                (spriteIndex * document::kMaxSpriteDetectAreas * 4 + areaIndex * 4) *
                    sizeof(uint16_t);
            writeU16(bytes, areaOffset, area.x);
            writeU16(bytes, areaOffset + sizeof(uint16_t), area.y);
            writeU16(bytes, areaOffset + 2 * sizeof(uint16_t), area.width);
            writeU16(bytes, areaOffset + 3 * sizeof(uint16_t), area.height);
        }
    }

    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        writeU32(bytes,
                 cromFrames.frameData.triggerIdsOffset + frameIndex * sizeof(uint32_t),
                 snapshot.frames[frameIndex].triggerId);

        const std::vector<document::FrameSpriteReference>& references =
            snapshot.frames[frameIndex].spriteReferences;
        for (std::size_t spriteSlot = 0; spriteSlot < document::kMaxSpritesPerFrame;
             ++spriteSlot) {
            const document::FrameSpriteReference& reference = references[spriteSlot];
            bytes[cromFrames.frameData.frameSpritesOffset +
                  frameIndex * document::kMaxSpritesPerFrame + spriteSlot] =
                reference.spriteId;

            const std::size_t boundingBoxOffset =
                cromFrames.frameData.frameSpriteBoundingBoxesOffset +
                (frameIndex * document::kMaxSpritesPerFrame * 4 + spriteSlot * 4) *
                    sizeof(uint16_t);
            writeU16(bytes, boundingBoxOffset, reference.minX);
            writeU16(bytes, boundingBoxOffset + sizeof(uint16_t), reference.minY);
            writeU16(bytes, boundingBoxOffset + 2 * sizeof(uint16_t), reference.maxX);
            writeU16(bytes, boundingBoxOffset + 3 * sizeof(uint16_t), reference.maxY);
        }
    }

    for (std::size_t backgroundIndex = 0; backgroundIndex < snapshot.backgrounds.size();
         ++backgroundIndex) {
        bytes[cromFrames.frameData.extraBackgroundFlagsOffset + backgroundIndex] =
            snapshot.backgrounds[backgroundIndex].colorizedHd ? uint8_t{1} : uint8_t{0};

        const std::size_t sdBackgroundOffset =
            cromFrames.frameData.backgroundFramesSdOffset +
            backgroundIndex * snapshot.backgrounds[backgroundIndex].colorizedSd.pixels.size() *
                sizeof(uint16_t);
        writeRgb565Frame(bytes, sdBackgroundOffset, snapshot.backgrounds[backgroundIndex].colorizedSd);

        if (snapshot.backgrounds[backgroundIndex].colorizedHd) {
            const std::size_t hdBackgroundOffset =
                cromFrames.frameData.backgroundFramesHdOffset +
                backgroundIndex *
                    snapshot.backgrounds[backgroundIndex].colorizedHd->pixels.size() *
                    sizeof(uint16_t);
            writeRgb565Frame(bytes,
                             hdBackgroundOffset,
                             *snapshot.backgrounds[backgroundIndex].colorizedHd);
        }
    }

    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const document::FrameBackgroundData& backgroundData =
            *snapshot.frames[frameIndex].backgroundData;
        writeU16(bytes,
                 cromFrames.frameData.backgroundIdsOffset + frameIndex * sizeof(uint16_t),
                 backgroundData.backgroundId);

        const std::size_t sdMaskOffset =
            cromFrames.frameData.backgroundMasksSdOffset +
            frameIndex * backgroundData.maskSd.pixels.size();
        writeIndexedFrame(bytes, sdMaskOffset, backgroundData.maskSd);

        const std::size_t hdMaskOffset =
            cromFrames.frameData.backgroundMasksHdOffset +
            frameIndex * backgroundData.maskHd.pixels.size();
        writeIndexedFrame(bytes, hdMaskOffset, backgroundData.maskHd);
    }

    if (!writeFileBytes(paths.cromPath, bytes, errorMessage)) {
        return makeSaveFailure(errorMessage);
    }
    if (!writeCrpSpriteNames(paths, cromFrames.frameData.metadata, snapshot, errorMessage)) {
        return makeSaveFailure(errorMessage);
    }
    return makeSaveSuccess();
}

}  // namespace colorizing::legacy
