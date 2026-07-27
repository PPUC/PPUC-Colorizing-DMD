#include "document/ProjectSnapshot.h"

#include "render/CanvasLayout.h"

namespace colorizing::document {

namespace {

bool hasSize(Size actual, Size expected)
{
    return actual.width == expected.width && actual.height == expected.height;
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

    for (std::size_t i = 0; i < snapshot.comparisonMasks.size(); ++i) {
        const IndexedFrame& mask = snapshot.comparisonMasks[i];
        if (!mask.isValid() || !hasSize(mask.size, render::kSdFrameSize)) {
            return fail(errorMessage, "comparison mask " + std::to_string(i) +
                                          " has invalid SD data");
        }
    }

    for (std::size_t i = 0; i < snapshot.backgrounds.size(); ++i) {
        const BackgroundRecord& background = snapshot.backgrounds[i];
        if (!background.colorizedSd.isValid() ||
            !hasSize(background.colorizedSd.size, render::kSdFrameSize)) {
            return fail(errorMessage, "background " + std::to_string(i) +
                                          " has invalid SD data");
        }
        if (background.colorizedHd &&
            (!background.colorizedHd->isValid() ||
             !hasSize(background.colorizedHd->size, render::kHdFrameSize))) {
            return fail(errorMessage, "background " + std::to_string(i) +
                                          " has invalid HD data");
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
    }

    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        const ProjectFrameSnapshot& frame = snapshot.frames[i];
        if (!frame.originalSd.isValid() || !hasSize(frame.originalSd.size, render::kSdFrameSize)) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid SD original data");
        }
        if (!frame.colorizedSd.isValid() || !hasSize(frame.colorizedSd.size, render::kSdFrameSize)) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid SD colorized data");
        }
        if (frame.colorizedHd &&
            (!frame.colorizedHd->isValid() || !hasSize(frame.colorizedHd->size, render::kHdFrameSize))) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid HD colorized data");
        }
        if (frame.dynamicData) {
            if (!frame.dynamicData->maskSd.isValid() ||
                !hasSize(frame.dynamicData->maskSd.size, render::kSdFrameSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid SD dynamic mask data");
            }
            if (!frame.dynamicData->maskHd.isValid() ||
                !hasSize(frame.dynamicData->maskHd.size, render::kHdFrameSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid HD dynamic mask data");
            }
            if (frame.dynamicData->colorsSd.empty() || frame.dynamicData->colorsHd.empty()) {
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
                !hasSize(frame.backgroundData->maskSd.size, render::kSdFrameSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid SD background mask data");
            }
            if (!frame.backgroundData->maskHd.isValid() ||
                !hasSize(frame.backgroundData->maskHd.size, render::kHdFrameSize)) {
                return fail(errorMessage, "frame " + std::to_string(i) +
                                              " has invalid HD background mask data");
            }
        }
        if (!frame.spriteReferences.empty() &&
            frame.spriteReferences.size() != kMaxSpritesPerFrame) {
            return fail(errorMessage, "frame " + std::to_string(i) +
                                          " has invalid sprite reference data");
        }
    }

    return true;
}

}  // namespace colorizing::document
