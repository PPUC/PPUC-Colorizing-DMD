#include "document/ProjectSnapshot.h"

#include <stdexcept>
#include <utility>

namespace colorizing::document {

namespace {

bool hasSize(Size actual, Size expected)
{
    return actual == expected;
}

bool fail(std::string* errorMessage, const std::string& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

template <typename T>
bool isEmptyOrSize(const std::vector<T>& values, std::size_t size)
{
    return values.empty() || values.size() == size;
}

}  // namespace

bool isValidProjectSnapshot(const ProjectSnapshot& snapshot, std::string* errorMessage)
{
    if (snapshot.romName.empty()) {
        return fail(errorMessage, "ROM name is empty");
    }
    if (snapshot.colorCount == 0 || snapshot.colorCount > kColorSetColorCount) {
        return fail(errorMessage, "original color count must be between 1 and 16");
    }
    if (snapshot.geometry.original256x64Flag() && snapshot.cromRevision < 20) {
        return fail(errorMessage, "256x64 original frames need cROM revision 20");
    }
    if (snapshot.comparisonMasks.size() > kMaxComparisonMasks) {
        return fail(errorMessage, "too many comparison masks");
    }
    if (snapshot.sprites.size() > kMaxSprites) {
        return fail(errorMessage, "too many sprites");
    }
    if (!isValidEditorMetadata(snapshot.editorMetadata, errorMessage)) {
        return false;
    }

    const Size originalSize = originalFrameSize(snapshot);
    const Size sdSize = snapshot.geometry.planeSize(Resolution::Sd);
    const Size hdSize = snapshot.geometry.planeSize(Resolution::Hd);
    for (std::size_t i = 0; i < snapshot.comparisonMasks.size(); ++i) {
        const IndexedFrame& mask = snapshot.comparisonMasks[i];
        if (!mask.isValid() || !hasSize(mask.size, originalSize)) {
            return fail(errorMessage, "comparison mask " + std::to_string(i) +
                                          " has invalid SD data");
        }
    }

    for (std::size_t i = 0; i < snapshot.backgrounds.size(); ++i) {
        const BackgroundRecord& background = snapshot.backgrounds[i];
        if (!background.colorizedSd.isValid() ||
            !hasSize(background.colorizedSd.size, sdSize)) {
            return fail(errorMessage, "background " + std::to_string(i) +
                                          " has invalid SD data");
        }
        if (background.colorizedHd &&
            (!background.colorizedHd->isValid() ||
             !hasSize(background.colorizedHd->size, hdSize))) {
            return fail(errorMessage, "background " + std::to_string(i) +
                                          " has invalid HD data");
        }
    }

    for (std::size_t i = 0; i < snapshot.backgrounds.size(); ++i) {
        const BackgroundRecord& background = snapshot.backgrounds[i];
        if (background.inactiveHd &&
            (background.colorizedHd || !background.inactiveHd->isValid() ||
             !hasSize(background.inactiveHd->size, hdSize))) {
            return fail(errorMessage, "background " + std::to_string(i) +
                                          " has invalid inactive HD data");
        }
    }

    const Size spriteSize{static_cast<int>(kMaxSpriteWidth),
                          static_cast<int>(kMaxSpriteHeight)};
    for (std::size_t i = 0; i < snapshot.sprites.size(); ++i) {
        const SpriteRecord& sprite = snapshot.sprites[i];
        if (!sprite.originalSd.isValid() || !hasSize(sprite.originalSd.size, spriteSize)) {
            return fail(errorMessage, "sprite " + std::to_string(i) +
                                          " has invalid SD original data");
        }
        if (!sprite.colorizedSd.isValid() || !hasSize(sprite.colorizedSd.size, spriteSize)) {
            return fail(errorMessage, "sprite " + std::to_string(i) +
                                          " has invalid SD colorized data");
        }
        if (!sprite.maskHd.isValid() || !hasSize(sprite.maskHd.size, spriteSize)) {
            return fail(errorMessage, "sprite " + std::to_string(i) +
                                          " has invalid HD mask data");
        }
        if (!sprite.colorizedHd.isValid() || !hasSize(sprite.colorizedHd.size, spriteSize)) {
            return fail(errorMessage, "sprite " + std::to_string(i) +
                                          " has invalid HD colorized data");
        }
        if (sprite.detectionDwords.size() != kMaxSpriteDetectAreas ||
            sprite.detectionDwordPositions.size() != kMaxSpriteDetectAreas ||
            sprite.detectionAreas.size() != kMaxSpriteDetectAreas) {
            return fail(errorMessage, "sprite " + std::to_string(i) +
                                          " has invalid detection metadata");
        }
        const std::size_t spriteDynamicColorCount =
            kMaxDynamicSetsPerSprite * static_cast<std::size_t>(snapshot.colorCount);
        if ((!sprite.dynamicMaskSd.pixels.empty() &&
             (!sprite.dynamicMaskSd.isValid() || !hasSize(sprite.dynamicMaskSd.size, spriteSize))) ||
            (!sprite.dynamicMaskHd.pixels.empty() &&
             (!sprite.dynamicMaskHd.isValid() || !hasSize(sprite.dynamicMaskHd.size, spriteSize))) ||
            !isEmptyOrSize(sprite.dynamicColorsSd, spriteDynamicColorCount) ||
            !isEmptyOrSize(sprite.dynamicColorsHd, spriteDynamicColorCount)) {
            return fail(errorMessage, "sprite " + std::to_string(i) +
                                          " has invalid dynamic data");
        }
    }

    for (std::size_t i = 0; i < snapshot.palettes.size(); ++i) {
        if (snapshot.palettes[i].colors.size() != kPaletteColorCount) {
            return fail(errorMessage, "palette " + std::to_string(i) +
                                          " has invalid color data");
        }
    }

    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        const ProjectFrameSnapshot& frame = snapshot.frames[i];
        if (!frame.originalSd.isValid() || !hasSize(frame.originalSd.size, originalSize)) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid SD original data");
        }
        if (!frame.colorizedSd.isValid() || !hasSize(frame.colorizedSd.size, sdSize)) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid SD colorized data");
        }
        if (frame.colorizedHd &&
            (!frame.colorizedHd->isValid() || !hasSize(frame.colorizedHd->size, hdSize))) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid HD colorized data");
        }
        if (frame.inactiveHd &&
            (frame.colorizedHd || !frame.inactiveHd->isValid() ||
             !hasSize(frame.inactiveHd->size, hdSize))) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid inactive HD data");
        }
        if (frame.dynamicData) {
            if (!frame.dynamicData->maskSd.isValid() ||
                !hasSize(frame.dynamicData->maskSd.size, sdSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid SD dynamic mask data");
            }
            if (!frame.dynamicData->maskHd.isValid() ||
                !hasSize(frame.dynamicData->maskHd.size, hdSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid HD dynamic mask data");
            }
            const std::size_t dynamicColorCount =
                kMaxDynamicSetsPerFrame * static_cast<std::size_t>(snapshot.colorCount);
            if (frame.dynamicData->colorsSd.size() != dynamicColorCount ||
                frame.dynamicData->colorsHd.size() != dynamicColorCount) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                          " has invalid dynamic color data");
            }
            if (!isEmptyOrSize(frame.dynamicData->shadowDirectionsSd, kMaxDynamicSetsPerFrame) ||
                !isEmptyOrSize(frame.dynamicData->shadowColorsSd, kMaxDynamicSetsPerFrame) ||
                !isEmptyOrSize(frame.dynamicData->shadowDirectionsHd, kMaxDynamicSetsPerFrame) ||
                !isEmptyOrSize(frame.dynamicData->shadowColorsHd, kMaxDynamicSetsPerFrame)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                          " has invalid dynamic shadow data");
            }
        }
        if (frame.backgroundData) {
            if (!frame.backgroundData->maskSd.isValid() ||
                !hasSize(frame.backgroundData->maskSd.size, sdSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid SD background mask data");
            }
            if (!frame.backgroundData->maskHd.isValid() ||
                !hasSize(frame.backgroundData->maskHd.size, hdSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid HD background mask data");
            }
        }
        if (!frame.spriteReferences.empty() &&
            frame.spriteReferences.size() != kMaxSpritesPerFrame) {
            return fail(errorMessage, "frame " + std::to_string(i) +
                                          " has invalid sprite reference data");
        }
        const std::size_t rotationValueCount = kMaxColorRotations * kMaxColorRotationLength;
        if ((!frame.colorRotationsSd.empty() &&
             frame.colorRotationsSd.size() != rotationValueCount) ||
            (!frame.colorRotationsHd.empty() &&
             frame.colorRotationsHd.size() != rotationValueCount)) {
            return fail(errorMessage, "frame " + std::to_string(i) +
                                          " has invalid color rotation data");
        }
    }

    return true;
}

Size originalFrameSize(const ProjectSnapshot& snapshot)
{
    return snapshot.geometry.romFrameSize();
}

void discardExtraPlane(ProjectSnapshot& snapshot)
{
    const Size extraSize = snapshot.geometry.planeSize(Resolution::Hd);
    const Size spriteSize{static_cast<int>(kMaxSpriteWidth),
                          static_cast<int>(kMaxSpriteHeight)};
    const auto colorCount = static_cast<std::size_t>(snapshot.colorCount);
    for (ProjectFrameSnapshot& frame : snapshot.frames) {
        frame.colorizedHd.reset();
        frame.inactiveHd.reset();
        if (frame.dynamicData) {
            frame.dynamicData->maskHd = makeIndexedFrame(extraSize, 255);
            frame.dynamicData->colorsHd.assign(kMaxDynamicSetsPerFrame * colorCount, 0);
            frame.dynamicData->shadowDirectionsHd.assign(kMaxDynamicSetsPerFrame, 0);
            frame.dynamicData->shadowColorsHd.assign(kMaxDynamicSetsPerFrame, 0);
        }
        if (frame.backgroundData) {
            frame.backgroundData->maskHd = makeIndexedFrame(extraSize, 1);
        }
        frame.colorRotationsHd.assign(kMaxColorRotations * kMaxColorRotationLength, 0);
    }
    for (BackgroundRecord& background : snapshot.backgrounds) {
        background.colorizedHd.reset();
        background.inactiveHd.reset();
    }
    for (SpriteRecord& sprite : snapshot.sprites) {
        sprite.hasHd = false;
        sprite.maskHd = makeIndexedFrame(spriteSize, 255);
        sprite.colorizedHd = makeRgb565Frame(spriteSize, 0);
        sprite.dynamicMaskHd = makeIndexedFrame(spriteSize, 255);
        sprite.dynamicColorsHd.assign(kMaxDynamicSetsPerSprite * colorCount, 0);
    }
}

ProjectFrameSnapshot makeUncolorizedFrameSnapshot(const render::ProjectGeometry& geometry,
                                                  uint32_t colorCount,
                                                  IndexedFrame original,
                                                  uint32_t durationMs)
{
    const Size romSize = geometry.romFrameSize();
    if (!original.isValid() || !hasSize(original.size, romSize)) {
        throw std::invalid_argument("the original has not the size of the ROM frame");
    }
    const Size sdSize = geometry.planeSize(Resolution::Sd);
    const Size hdSize = geometry.planeSize(Resolution::Hd);
    const std::vector<uint16_t> colors = plainOriginalColors(colorCount);
    Rgb565Frame plain = makeRgb565Frame(romSize);
    for (std::size_t index = 0; index < original.pixels.size(); ++index) {
        const uint8_t shade = original.pixels[index];
        plain.pixels[index] = shade < colors.size() ? colors[shade] : uint16_t{0};
    }

    ProjectFrameSnapshot frame;
    if (geometry.nativePlane() == Resolution::Hd) {
        frame.colorizedSd = makeRgb565Frame(sdSize);
        for (int y = 0; y < sdSize.height; ++y) {
            for (int x = 0; x < sdSize.width; ++x) {
                frame.colorizedSd.pixels[static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(sdSize.width) +
                                         static_cast<std::size_t>(x)] =
                    plain.pixels[static_cast<std::size_t>(2 * y) *
                                     static_cast<std::size_t>(romSize.width) +
                                 static_cast<std::size_t>(2 * x)];
            }
        }
        frame.colorizedHd = std::move(plain);
    } else {
        frame.colorizedSd = std::move(plain);
    }
    frame.originalSd = std::move(original);
    frame.shapeComparisonMode = 0;
    frame.comparisonMaskId = kNoComparisonMaskId;

    const std::size_t dynamicColorCount =
        kMaxDynamicSetsPerFrame * static_cast<std::size_t>(colorCount);
    FrameDynamicData dynamicData;
    dynamicData.maskSd = makeIndexedFrame(sdSize, 255);
    dynamicData.maskHd = makeIndexedFrame(hdSize, 255);
    dynamicData.colorsSd.assign(dynamicColorCount, 0);
    dynamicData.colorsHd.assign(dynamicColorCount, 0);
    dynamicData.shadowDirectionsSd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsSd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowDirectionsHd.assign(kMaxDynamicSetsPerFrame, 0);
    dynamicData.shadowColorsHd.assign(kMaxDynamicSetsPerFrame, 0);
    frame.dynamicData = std::move(dynamicData);

    FrameBackgroundData backgroundData;
    backgroundData.backgroundId = kNoBackgroundId;
    backgroundData.maskSd = makeIndexedFrame(sdSize, 1);
    backgroundData.maskHd = makeIndexedFrame(hdSize, 1);
    frame.backgroundData = std::move(backgroundData);

    FrameSpriteReference noSprite;
    noSprite.spriteId = kNoSpriteId;
    noSprite.maxX = static_cast<uint16_t>(romSize.width - 1);
    noSprite.maxY = static_cast<uint16_t>(romSize.height - 1);
    frame.spriteReferences.assign(kMaxSpritesPerFrame, noSprite);

    frame.triggerId = kNoTriggerId;
    frame.durationMs = durationMs;
    frame.colorRotationsSd.assign(kMaxColorRotations * kMaxColorRotationLength, 0);
    frame.colorRotationsHd.assign(kMaxColorRotations * kMaxColorRotationLength, 0);
    return frame;
}

void normalizeSpriteRecord(SpriteRecord& sprite, uint32_t colorCount)
{
    const Size spriteSize{static_cast<int>(kMaxSpriteWidth),
                          static_cast<int>(kMaxSpriteHeight)};
    if (sprite.dynamicMaskSd.pixels.empty()) {
        sprite.dynamicMaskSd = makeIndexedFrame(spriteSize, 255);
    }
    if (sprite.dynamicMaskHd.pixels.empty()) {
        sprite.dynamicMaskHd = makeIndexedFrame(spriteSize, 255);
    }
    const std::size_t dynamicColorCount =
        kMaxDynamicSetsPerSprite * static_cast<std::size_t>(colorCount);
    if (sprite.dynamicColorsSd.empty()) {
        sprite.dynamicColorsSd.assign(dynamicColorCount, 0);
    }
    if (sprite.dynamicColorsHd.empty()) {
        sprite.dynamicColorsHd.assign(dynamicColorCount, 0);
    }
}

}  // namespace colorizing::document
