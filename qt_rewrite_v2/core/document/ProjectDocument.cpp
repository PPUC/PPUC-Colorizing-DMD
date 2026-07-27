#include "document/ProjectDocument.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

#include "document/ProjectSnapshot.h"
#include "render/CanvasLayout.h"

namespace colorizing::document {

namespace {

bool hasSize(Size actual, Size expected)
{
    return actual.width == expected.width && actual.height == expected.height;
}

std::size_t checkedPixelCount(Size size)
{
    if (size.width <= 0 || size.height <= 0) {
        throw std::invalid_argument("frame size must be positive");
    }
    return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

FrameRecord makeEmptyFrame(bool createHd)
{
    FrameRecord frame;
    frame.originalSd = makeIndexedFrame(render::kSdFrameSize);
    frame.colorizedSd = makeRgb565Frame(render::kSdFrameSize);
    frame.colorRotationsSd.assign(kMaxColorRotations * kMaxColorRotationLength, 0);
    frame.colorRotationsHd.assign(kMaxColorRotations * kMaxColorRotationLength, 0);
    if (createHd) {
        frame.colorizedHd = makeRgb565Frame(render::kHdFrameSize);
    }
    return frame;
}

FrameRecord& requireFrame(std::vector<FrameRecord>& frames, std::size_t frameIndex)
{
    if (frameIndex >= frames.size()) {
        throw std::out_of_range("frame index out of range");
    }
    return frames[frameIndex];
}

const FrameRecord& requireFrame(const std::vector<FrameRecord>& frames, std::size_t frameIndex)
{
    if (frameIndex >= frames.size()) {
        throw std::out_of_range("frame index out of range");
    }
    return frames[frameIndex];
}

std::vector<uint16_t> normalizedColorRotations(const std::vector<uint16_t>& rotations)
{
    if (rotations.empty()) {
        return std::vector<uint16_t>(kMaxColorRotations * kMaxColorRotationLength, 0);
    }
    return rotations;
}

std::vector<uint16_t>& requireColorRotations(FrameRecord& frame, Resolution resolution)
{
    return resolution == Resolution::Hd ? frame.colorRotationsHd : frame.colorRotationsSd;
}

const std::vector<uint16_t>& requireColorRotations(const FrameRecord& frame,
                                                   Resolution resolution)
{
    return resolution == Resolution::Hd ? frame.colorRotationsHd : frame.colorRotationsSd;
}

std::size_t colorRotationOffset(std::size_t slotIndex)
{
    if (slotIndex >= kMaxColorRotations) {
        throw std::out_of_range("color rotation slot index out of range");
    }
    return slotIndex * kMaxColorRotationLength;
}

std::size_t colorRotationColorOffset(std::size_t slotIndex, std::size_t colorIndex)
{
    if (colorIndex >= kMaxColorRotationColors) {
        throw std::out_of_range("color rotation color index out of range");
    }
    return colorRotationOffset(slotIndex) + 2 + colorIndex;
}

Rgb565Frame upscaleRgb565Frame2x(const Rgb565Frame& source)
{
    Rgb565Frame target = makeRgb565Frame(Size{source.size.width * 2, source.size.height * 2});
    for (int y = 0; y < target.size.height; ++y) {
        for (int x = 0; x < target.size.width; ++x) {
            target.pixels[static_cast<std::size_t>(y) *
                              static_cast<std::size_t>(target.size.width) +
                          static_cast<std::size_t>(x)] =
                source.pixels[static_cast<std::size_t>(y / 2) *
                                  static_cast<std::size_t>(source.size.width) +
                              static_cast<std::size_t>(x / 2)];
        }
    }
    return target;
}

IndexedFrame upscaleIndexedFrame2x(const IndexedFrame& source)
{
    IndexedFrame target = makeIndexedFrame(Size{source.size.width * 2, source.size.height * 2});
    for (int y = 0; y < target.size.height; ++y) {
        for (int x = 0; x < target.size.width; ++x) {
            target.pixels[static_cast<std::size_t>(y) *
                              static_cast<std::size_t>(target.size.width) +
                          static_cast<std::size_t>(x)] =
                source.pixels[static_cast<std::size_t>(y / 2) *
                                  static_cast<std::size_t>(source.size.width) +
                              static_cast<std::size_t>(x / 2)];
        }
    }
    return target;
}

IndexedFrame upscaleSpriteIndexedFrame2x(const IndexedFrame& source)
{
    IndexedFrame target = makeIndexedFrame(Size{static_cast<int>(kMaxSpriteWidth),
                                                static_cast<int>(kMaxSpriteHeight)}, 255);
    for (int y = 0; y < target.size.height; ++y) {
        for (int x = 0; x < target.size.width; ++x) {
            target.pixels[static_cast<std::size_t>(y) * kMaxSpriteWidth +
                          static_cast<std::size_t>(x)] =
                source.pixels[static_cast<std::size_t>(y / 2) * kMaxSpriteWidth +
                              static_cast<std::size_t>(x / 2)];
        }
    }
    return target;
}

Rgb565Frame upscaleSpriteRgb565Frame2x(const Rgb565Frame& source)
{
    Rgb565Frame target = makeRgb565Frame(Size{static_cast<int>(kMaxSpriteWidth),
                                              static_cast<int>(kMaxSpriteHeight)});
    for (int y = 0; y < target.size.height; ++y) {
        for (int x = 0; x < target.size.width; ++x) {
            target.pixels[static_cast<std::size_t>(y) * kMaxSpriteWidth +
                          static_cast<std::size_t>(x)] =
                source.pixels[static_cast<std::size_t>(y / 2) * kMaxSpriteWidth +
                              static_cast<std::size_t>(x / 2)];
        }
    }
    return target;
}

std::size_t spritePixelIndex(int x, int y)
{
    return static_cast<std::size_t>(y) * kMaxSpriteWidth + static_cast<std::size_t>(x);
}

std::size_t checkedSpritePixelIndex(int x, int y)
{
    if (x < 0 || y < 0 ||
        x >= static_cast<int>(kMaxSpriteWidth) ||
        y >= static_cast<int>(kMaxSpriteHeight)) {
        throw std::out_of_range("sprite pixel coordinate out of range");
    }
    return spritePixelIndex(x, y);
}

const Rgb565Frame& requireSpriteColorizedFrame(const SpriteRecord& sprite,
                                               Resolution resolution)
{
    if (resolution == Resolution::Hd && sprite.hasHd) {
        return sprite.colorizedHd;
    }
    if (resolution == Resolution::Hd && !sprite.hasHd) {
        throw std::logic_error("HD sprite is not available");
    }
    return sprite.colorizedSd;
}

Rgb565Frame& requireSpriteColorizedFrame(SpriteRecord& sprite, Resolution resolution)
{
    if (resolution == Resolution::Hd && sprite.hasHd) {
        return sprite.colorizedHd;
    }
    if (resolution == Resolution::Hd && !sprite.hasHd) {
        throw std::logic_error("HD sprite is not available");
    }
    return sprite.colorizedSd;
}

bool isValidDetectionArea(SpriteDetectionArea area)
{
    return area.x != 0xffff && area.y != 0xffff &&
           area.width != 0xffff && area.height != 0xffff &&
           area.width > 0 && area.height > 0;
}

uint32_t spriteOriginalDwordAt(const IndexedFrame& original, int x, int y)
{
    return static_cast<uint32_t>(original.pixels[spritePixelIndex(x, y)]) |
           (static_cast<uint32_t>(original.pixels[spritePixelIndex(x + 1, y)]) << 8) |
           (static_cast<uint32_t>(original.pixels[spritePixelIndex(x + 2, y)]) << 16) |
           (static_cast<uint32_t>(original.pixels[spritePixelIndex(x + 3, y)]) << 24);
}

std::optional<std::pair<uint32_t, uint16_t>> detectionDwordForArea(
    const IndexedFrame& original,
    SpriteDetectionArea area)
{
    if (!isValidDetectionArea(area) || area.width < 4) {
        return std::nullopt;
    }

    const int minX = std::clamp(static_cast<int>(area.x), 0, original.size.width - 1);
    const int minY = std::clamp(static_cast<int>(area.y), 0, original.size.height - 1);
    const int maxX = std::clamp(static_cast<int>(area.x) + static_cast<int>(area.width),
                                0,
                                original.size.width);
    const int maxY = std::clamp(static_cast<int>(area.y) + static_cast<int>(area.height),
                                0,
                                original.size.height);
    if (maxX - minX < 4 || maxY <= minY) {
        return std::nullopt;
    }

    struct Candidate {
        uint32_t dword = 0;
        uint16_t position = 0xffff;
        int count = 0;
    };
    std::map<uint32_t, Candidate> candidates;
    for (int y = minY; y < maxY; ++y) {
        for (int x = minX; x <= maxX - 4; ++x) {
            bool contiguous = true;
            for (int offset = 0; offset < 4; ++offset) {
                if (original.pixels[spritePixelIndex(x + offset, y)] == 255) {
                    contiguous = false;
                    break;
                }
            }
            if (!contiguous) {
                continue;
            }
            const uint32_t dword = spriteOriginalDwordAt(original, x, y);
            Candidate& candidate = candidates[dword];
            candidate.dword = dword;
            ++candidate.count;
            if (candidate.position == 0xffff) {
                candidate.position = static_cast<uint16_t>(spritePixelIndex(x, y));
            }
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    const auto best = std::min_element(
        candidates.begin(),
        candidates.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.second.count != rhs.second.count) {
                return lhs.second.count < rhs.second.count;
            }
            return lhs.second.position < rhs.second.position;
        });
    return std::make_pair(best->second.dword, best->second.position);
}

void recalculateSpriteDetectionDwords(SpriteRecord& sprite)
{
    for (std::size_t areaIndex = 0;
         areaIndex < sprite.detectionAreas.size() &&
             areaIndex < sprite.detectionDwords.size() &&
             areaIndex < sprite.detectionDwordPositions.size();
         ++areaIndex) {
        uint32_t dword = 0xffffffff;
        uint16_t dwordPosition = 0xffff;
        if (const std::optional<std::pair<uint32_t, uint16_t>> calculated =
                detectionDwordForArea(sprite.originalSd, sprite.detectionAreas[areaIndex])) {
            dword = calculated->first;
            dwordPosition = calculated->second;
        }
        sprite.detectionDwords[areaIndex] = dword;
        sprite.detectionDwordPositions[areaIndex] = dwordPosition;
    }
}

}  // namespace

bool Rgb565Frame::isValid() const
{
    return size.width > 0 && size.height > 0 && pixels.size() == pixelCount();
}

std::size_t Rgb565Frame::pixelCount() const
{
    if (size.width <= 0 || size.height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

bool IndexedFrame::isValid() const
{
    return size.width > 0 && size.height > 0 && pixels.size() == pixelCount();
}

std::size_t IndexedFrame::pixelCount() const
{
    if (size.width <= 0 || size.height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

Rgb565Frame makeRgb565Frame(Size size, uint16_t fill)
{
    Rgb565Frame frame;
    frame.size = size;
    frame.pixels.assign(checkedPixelCount(size), fill);
    return frame;
}

IndexedFrame makeIndexedFrame(Size size, uint8_t fill)
{
    IndexedFrame frame;
    frame.size = size;
    frame.pixels.assign(checkedPixelCount(size), fill);
    return frame;
}

ProjectDocument ProjectDocument::createEmpty(const ProjectCreateOptions& options)
{
    ProjectDocument document;
    document.m_romName = options.romName;
    document.m_frames.reserve(options.frameCount);
    for (uint32_t i = 0; i < options.frameCount; ++i) {
        document.m_frames.push_back(makeEmptyFrame(options.createHdFrames));
    }
    document.m_dirty = false;
    return document;
}

ProjectDocument ProjectDocument::fromSnapshot(const ProjectSnapshot& snapshot)
{
    std::string errorMessage;
    if (!isValidProjectSnapshot(snapshot, &errorMessage)) {
        throw std::invalid_argument(errorMessage);
    }

    ProjectDocument document;
    document.m_romName = snapshot.romName;
    document.m_frames.reserve(snapshot.frames.size());
    for (const ProjectFrameSnapshot& frameSnapshot : snapshot.frames) {
        document.m_frames.push_back(FrameRecord{
            frameSnapshot.originalSd,
            frameSnapshot.colorizedSd,
            frameSnapshot.colorizedHd,
            frameSnapshot.shapeComparisonMode,
            frameSnapshot.comparisonMaskId,
            frameSnapshot.dynamicData,
            frameSnapshot.backgroundData,
            frameSnapshot.spriteReferences,
            frameSnapshot.triggerId,
            frameSnapshot.durationMs,
            normalizedColorRotations(frameSnapshot.colorRotationsSd),
            normalizedColorRotations(frameSnapshot.colorRotationsHd),
        });
    }
    document.m_comparisonMasks = snapshot.comparisonMasks;
    document.m_backgrounds = snapshot.backgrounds;
    document.m_sprites = snapshot.sprites;
    document.m_unsupportedPayloads = snapshot.unsupportedPayloads;
    document.m_dirty = false;
    return document;
}

const std::string& ProjectDocument::romName() const
{
    return m_romName;
}

void ProjectDocument::setRomName(std::string name)
{
    if (m_romName == name) {
        return;
    }
    m_romName = std::move(name);
    m_dirty = true;
}

std::size_t ProjectDocument::frameCount() const
{
    return m_frames.size();
}

bool ProjectDocument::isDirty() const
{
    return m_dirty;
}

void ProjectDocument::markClean()
{
    m_dirty = false;
}

bool ProjectDocument::hasHdFrame(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).colorizedHd.has_value();
}

void ProjectDocument::createHdFrame(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.colorizedHd) {
        return;
    }
    frame.colorizedHd = upscaleRgb565Frame2x(frame.colorizedSd);
    m_dirty = true;
}

void ProjectDocument::removeHdFrame(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.colorizedHd) {
        return;
    }
    frame.colorizedHd.reset();
    m_dirty = true;
}

void ProjectDocument::upscaleDynamicMaskToHd(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic mask data is not available");
    }
    frame.dynamicData->maskHd = upscaleIndexedFrame2x(frame.dynamicData->maskSd);
    frame.dynamicData->colorsHd = frame.dynamicData->colorsSd;
    frame.dynamicData->shadowDirectionsHd = frame.dynamicData->shadowDirectionsSd;
    frame.dynamicData->shadowColorsHd = frame.dynamicData->shadowColorsSd;
    m_dirty = true;
}

void ProjectDocument::upscaleBackgroundMaskToHd(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    frame.backgroundData->maskHd = upscaleIndexedFrame2x(frame.backgroundData->maskSd);
    m_dirty = true;
}

void ProjectDocument::upscaleAssignedBackgroundToHd(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    if (frame.backgroundData->backgroundId >= m_backgrounds.size()) {
        throw std::logic_error("assigned background is not available");
    }
    BackgroundRecord& background = m_backgrounds[frame.backgroundData->backgroundId];
    background.colorizedHd = upscaleRgb565Frame2x(background.colorizedSd);
    m_dirty = true;
}

const IndexedFrame& ProjectDocument::originalFrame(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).originalSd;
}

const Rgb565Frame& ProjectDocument::colorizedFrame(std::size_t frameIndex,
                                                   Resolution resolution) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (resolution == Resolution::Sd) {
        return frame.colorizedSd;
    }
    if (!frame.colorizedHd) {
        throw std::logic_error("HD frame is not available");
    }
    return *frame.colorizedHd;
}

uint8_t ProjectDocument::shapeComparisonMode(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).shapeComparisonMode;
}

void ProjectDocument::setShapeComparisonMode(std::size_t frameIndex, uint8_t mode)
{
    if (mode > 1) {
        throw std::invalid_argument("shape comparison mode must be 0 or 1");
    }
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.shapeComparisonMode == mode) {
        return;
    }
    frame.shapeComparisonMode = mode;
    m_dirty = true;
}

uint8_t ProjectDocument::comparisonMaskId(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).comparisonMaskId;
}

void ProjectDocument::setComparisonMaskId(std::size_t frameIndex, uint8_t maskId)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.comparisonMaskId == maskId) {
        return;
    }
    frame.comparisonMaskId = maskId;
    m_dirty = true;
}

uint32_t ProjectDocument::triggerId(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).triggerId;
}

void ProjectDocument::setTriggerId(std::size_t frameIndex, uint32_t triggerId)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.triggerId == triggerId) {
        return;
    }
    frame.triggerId = triggerId;
    m_dirty = true;
}

uint32_t ProjectDocument::frameDurationMs(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).durationMs;
}

void ProjectDocument::setFrameDurationMs(std::size_t frameIndex, uint32_t durationMs)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.durationMs == durationMs) {
        return;
    }
    frame.durationMs = durationMs;
    m_dirty = true;
}

const std::vector<uint16_t>& ProjectDocument::colorRotations(std::size_t frameIndex,
                                                             Resolution resolution) const
{
    return requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
}

uint16_t ProjectDocument::colorRotationLength(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t slotIndex) const
{
    const std::vector<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationOffset(slotIndex)];
}

uint16_t ProjectDocument::colorRotationDelayMs(std::size_t frameIndex,
                                               Resolution resolution,
                                               std::size_t slotIndex) const
{
    const std::vector<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationOffset(slotIndex) + 1];
}

uint16_t ProjectDocument::colorRotationColor(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex,
                                             std::size_t colorIndex) const
{
    const std::vector<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationColorOffset(slotIndex, colorIndex)];
}

void ProjectDocument::setColorRotationLength(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex,
                                             uint16_t length)
{
    if (length > kMaxColorRotationColors) {
        throw std::out_of_range("color rotation length out of range");
    }
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex);
    if (rotations[offset] == length) {
        return;
    }
    rotations[offset] = length;
    m_dirty = true;
}

void ProjectDocument::setColorRotationDelayMs(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t slotIndex,
                                              uint16_t delayMs)
{
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex) + 1;
    if (rotations[offset] == delayMs) {
        return;
    }
    rotations[offset] = delayMs;
    m_dirty = true;
}

void ProjectDocument::setColorRotationColor(std::size_t frameIndex,
                                            Resolution resolution,
                                            std::size_t slotIndex,
                                            std::size_t colorIndex,
                                            uint16_t rgb565)
{
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationColorOffset(slotIndex, colorIndex);
    if (rotations[offset] == rgb565) {
        return;
    }
    rotations[offset] = rgb565;
    m_dirty = true;
}

void ProjectDocument::clearColorRotationSlot(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex)
{
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex);
    const auto begin = rotations.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = begin + static_cast<std::ptrdiff_t>(kMaxColorRotationLength);
    if (std::all_of(begin, end, [](uint16_t value) { return value == 0; })) {
        return;
    }
    std::fill(begin, end, uint16_t{0});
    m_dirty = true;
}

std::size_t ProjectDocument::comparisonMaskCount() const
{
    return m_comparisonMasks.size();
}

const IndexedFrame& ProjectDocument::comparisonMask(std::size_t maskIndex) const
{
    if (maskIndex >= m_comparisonMasks.size()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    return m_comparisonMasks[maskIndex];
}

uint8_t ProjectDocument::comparisonMaskPixel(std::size_t maskIndex, int x, int y) const
{
    const IndexedFrame& mask = comparisonMask(maskIndex);
    if (x < 0 || y < 0 || x >= mask.size.width || y >= mask.size.height) {
        throw std::out_of_range("mask pixel coordinate out of range");
    }
    const std::size_t pixelIndex =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.size.width) +
        static_cast<std::size_t>(x);
    return mask.pixels[pixelIndex];
}

void ProjectDocument::setComparisonMaskPixel(std::size_t maskIndex,
                                             int x,
                                             int y,
                                             uint8_t value)
{
    if (maskIndex >= m_comparisonMasks.size()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    IndexedFrame& mask = m_comparisonMasks[maskIndex];
    if (x < 0 || y < 0 || x >= mask.size.width || y >= mask.size.height) {
        throw std::out_of_range("mask pixel coordinate out of range");
    }
    const std::size_t pixelIndex =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.size.width) +
        static_cast<std::size_t>(x);
    if (mask.pixels[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    m_dirty = true;
}

const std::optional<FrameDynamicData>& ProjectDocument::dynamicData(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).dynamicData;
}

namespace {

const IndexedFrame& requireDynamicMask(const FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic mask data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->maskHd : frame.dynamicData->maskSd;
}

IndexedFrame& requireDynamicMask(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic mask data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->maskHd : frame.dynamicData->maskSd;
}

const std::vector<uint16_t>& requireDynamicColors(const FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->colorsHd : frame.dynamicData->colorsSd;
}

std::vector<uint16_t>& requireDynamicColors(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->colorsHd : frame.dynamicData->colorsSd;
}

const std::vector<uint8_t>& requireDynamicShadowDirections(const FrameRecord& frame,
                                                           Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow direction data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowDirectionsHd
                                        : frame.dynamicData->shadowDirectionsSd;
}

std::vector<uint8_t>& requireDynamicShadowDirections(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow direction data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowDirectionsHd
                                        : frame.dynamicData->shadowDirectionsSd;
}

const std::vector<uint16_t>& requireDynamicShadowColors(const FrameRecord& frame,
                                                       Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowColorsHd
                                        : frame.dynamicData->shadowColorsSd;
}

std::vector<uint16_t>& requireDynamicShadowColors(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowColorsHd
                                        : frame.dynamicData->shadowColorsSd;
}

template <typename T>
void ensureDynamicSetVectorSize(std::vector<T>& values, T fill)
{
    if (values.size() < kMaxDynamicSetsPerFrame) {
        values.resize(kMaxDynamicSetsPerFrame, fill);
    }
}

std::size_t checkedIndexedPixelIndex(const IndexedFrame& frame, int x, int y)
{
    if (x < 0 || y < 0 || x >= frame.size.width || y >= frame.size.height) {
        throw std::out_of_range("mask pixel coordinate out of range");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
           static_cast<std::size_t>(x);
}

std::size_t checkedDynamicColorIndex(const std::vector<uint16_t>& colors,
                                     std::size_t setIndex,
                                     std::size_t originalColorIndex)
{
    if (setIndex >= kMaxDynamicSetsPerFrame) {
        throw std::out_of_range("dynamic set index out of range");
    }
    const std::size_t colorCount = colors.size() / kMaxDynamicSetsPerFrame;
    if (colorCount == 0 || originalColorIndex >= colorCount) {
        throw std::out_of_range("dynamic color index out of range");
    }
    return setIndex * colorCount + originalColorIndex;
}

void requireDynamicSetIndex(std::size_t setIndex)
{
    if (setIndex >= kMaxDynamicSetsPerFrame) {
        throw std::out_of_range("dynamic set index out of range");
    }
}

}  // namespace

uint8_t ProjectDocument::dynamicMaskPixel(std::size_t frameIndex,
                                          Resolution resolution,
                                          int x,
                                          int y) const
{
    const IndexedFrame& mask = requireDynamicMask(requireFrame(m_frames, frameIndex), resolution);
    return mask.pixels[checkedIndexedPixelIndex(mask, x, y)];
}

void ProjectDocument::setDynamicMaskPixel(std::size_t frameIndex,
                                          Resolution resolution,
                                          int x,
                                          int y,
                                          uint8_t value)
{
    IndexedFrame& mask = requireDynamicMask(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t pixelIndex = checkedIndexedPixelIndex(mask, x, y);
    if (mask.pixels[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    m_dirty = true;
}

uint16_t ProjectDocument::dynamicColor(std::size_t frameIndex,
                                       Resolution resolution,
                                       std::size_t setIndex,
                                       std::size_t originalColorIndex) const
{
    const std::vector<uint16_t>& colors =
        requireDynamicColors(requireFrame(m_frames, frameIndex), resolution);
    return colors[checkedDynamicColorIndex(colors, setIndex, originalColorIndex)];
}

void ProjectDocument::setDynamicColor(std::size_t frameIndex,
                                      Resolution resolution,
                                      std::size_t setIndex,
                                      std::size_t originalColorIndex,
                                      uint16_t rgb565)
{
    std::vector<uint16_t>& colors =
        requireDynamicColors(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t colorIndex =
        checkedDynamicColorIndex(colors, setIndex, originalColorIndex);
    if (colors[colorIndex] == rgb565) {
        return;
    }
    colors[colorIndex] = rgb565;
    m_dirty = true;
}

uint8_t ProjectDocument::dynamicShadowDirection(std::size_t frameIndex,
                                                Resolution resolution,
                                                std::size_t setIndex) const
{
    requireDynamicSetIndex(setIndex);
    const std::vector<uint8_t>& directions =
        requireDynamicShadowDirections(requireFrame(m_frames, frameIndex), resolution);
    if (setIndex >= directions.size()) {
        return 0;
    }
    return directions[setIndex];
}

uint16_t ProjectDocument::dynamicShadowColor(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t setIndex) const
{
    requireDynamicSetIndex(setIndex);
    const std::vector<uint16_t>& colors =
        requireDynamicShadowColors(requireFrame(m_frames, frameIndex), resolution);
    if (setIndex >= colors.size()) {
        return 0;
    }
    return colors[setIndex];
}

void ProjectDocument::setDynamicShadowDirection(std::size_t frameIndex,
                                                Resolution resolution,
                                                std::size_t setIndex,
                                                uint8_t direction)
{
    requireDynamicSetIndex(setIndex);
    std::vector<uint8_t>& directions =
        requireDynamicShadowDirections(requireFrame(m_frames, frameIndex), resolution);
    ensureDynamicSetVectorSize<uint8_t>(directions, 0);
    if (directions[setIndex] == direction) {
        return;
    }
    directions[setIndex] = direction;
    m_dirty = true;
}

void ProjectDocument::setDynamicShadowColor(std::size_t frameIndex,
                                            Resolution resolution,
                                            std::size_t setIndex,
                                            uint16_t rgb565)
{
    requireDynamicSetIndex(setIndex);
    std::vector<uint16_t>& colors =
        requireDynamicShadowColors(requireFrame(m_frames, frameIndex), resolution);
    ensureDynamicSetVectorSize<uint16_t>(colors, 0);
    if (colors[setIndex] == rgb565) {
        return;
    }
    colors[setIndex] = rgb565;
    m_dirty = true;
}

std::size_t ProjectDocument::backgroundCount() const
{
    return m_backgrounds.size();
}

const BackgroundRecord& ProjectDocument::background(std::size_t backgroundIndex) const
{
    if (backgroundIndex >= m_backgrounds.size()) {
        throw std::out_of_range("background index out of range");
    }
    return m_backgrounds[backgroundIndex];
}

namespace {

const BackgroundRecord& requireBackground(const std::vector<BackgroundRecord>& backgrounds,
                                          std::size_t backgroundIndex)
{
    if (backgroundIndex >= backgrounds.size()) {
        throw std::out_of_range("background index out of range");
    }
    return backgrounds[backgroundIndex];
}

BackgroundRecord& requireBackground(std::vector<BackgroundRecord>& backgrounds,
                                    std::size_t backgroundIndex)
{
    if (backgroundIndex >= backgrounds.size()) {
        throw std::out_of_range("background index out of range");
    }
    return backgrounds[backgroundIndex];
}

const Rgb565Frame& requireBackgroundFrame(const BackgroundRecord& background,
                                          Resolution resolution)
{
    if (resolution == Resolution::Sd) {
        return background.colorizedSd;
    }
    if (!background.colorizedHd) {
        throw std::logic_error("HD background is not available");
    }
    return *background.colorizedHd;
}

Rgb565Frame& requireBackgroundFrame(BackgroundRecord& background, Resolution resolution)
{
    if (resolution == Resolution::Sd) {
        return background.colorizedSd;
    }
    if (!background.colorizedHd) {
        throw std::logic_error("HD background is not available");
    }
    return *background.colorizedHd;
}

std::size_t checkedRgb565PixelIndex(const Rgb565Frame& frame, int x, int y)
{
    if (x < 0 || y < 0 || x >= frame.size.width || y >= frame.size.height) {
        throw std::out_of_range("pixel coordinate out of range");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
           static_cast<std::size_t>(x);
}

}  // namespace

const Rgb565Frame& ProjectDocument::backgroundFrame(std::size_t backgroundIndex,
                                                    Resolution resolution) const
{
    return requireBackgroundFrame(requireBackground(m_backgrounds, backgroundIndex), resolution);
}

void ProjectDocument::setBackgroundFrame(std::size_t backgroundIndex,
                                         Resolution resolution,
                                         Rgb565Frame frame)
{
    if (resolution == Resolution::Sd && !hasSize(frame.size, render::kSdFrameSize)) {
        throw std::invalid_argument("SD background frame has invalid size");
    }
    if (resolution == Resolution::Hd && !hasSize(frame.size, render::kHdFrameSize)) {
        throw std::invalid_argument("HD background frame has invalid size");
    }

    BackgroundRecord& background = requireBackground(m_backgrounds, backgroundIndex);
    if (resolution == Resolution::Sd) {
        if (hasSize(background.colorizedSd.size, frame.size) &&
            background.colorizedSd.pixels == frame.pixels) {
            return;
        }
        background.colorizedSd = std::move(frame);
    } else {
        if (background.colorizedHd &&
            hasSize(background.colorizedHd->size, frame.size) &&
            background.colorizedHd->pixels == frame.pixels) {
            return;
        }
        background.colorizedHd = std::move(frame);
    }
    m_dirty = true;
}

uint16_t ProjectDocument::backgroundPixel(std::size_t backgroundIndex,
                                          Resolution resolution,
                                          int x,
                                          int y) const
{
    const Rgb565Frame& frame = backgroundFrame(backgroundIndex, resolution);
    return frame.pixels[checkedRgb565PixelIndex(frame, x, y)];
}

void ProjectDocument::setBackgroundPixel(std::size_t backgroundIndex,
                                         Resolution resolution,
                                         int x,
                                         int y,
                                         uint16_t rgb565)
{
    Rgb565Frame& frame =
        requireBackgroundFrame(requireBackground(m_backgrounds, backgroundIndex), resolution);
    const std::size_t pixelIndex = checkedRgb565PixelIndex(frame, x, y);
    if (frame.pixels[pixelIndex] == rgb565) {
        return;
    }
    frame.pixels[pixelIndex] = rgb565;
    m_dirty = true;
}

const std::optional<FrameBackgroundData>& ProjectDocument::backgroundData(
    std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).backgroundData;
}

uint16_t ProjectDocument::backgroundId(std::size_t frameIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    return frame.backgroundData->backgroundId;
}

void ProjectDocument::setBackgroundId(std::size_t frameIndex, uint16_t backgroundId)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    if (frame.backgroundData->backgroundId == backgroundId) {
        return;
    }
    frame.backgroundData->backgroundId = backgroundId;
    m_dirty = true;
}

namespace {

const IndexedFrame& requireBackgroundMask(const FrameRecord& frame, Resolution resolution)
{
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    return resolution == Resolution::Hd ? frame.backgroundData->maskHd : frame.backgroundData->maskSd;
}

IndexedFrame& requireBackgroundMask(FrameRecord& frame, Resolution resolution)
{
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    return resolution == Resolution::Hd ? frame.backgroundData->maskHd : frame.backgroundData->maskSd;
}

}  // namespace

uint8_t ProjectDocument::backgroundMaskPixel(std::size_t frameIndex,
                                             Resolution resolution,
                                             int x,
                                             int y) const
{
    const IndexedFrame& mask =
        requireBackgroundMask(requireFrame(m_frames, frameIndex), resolution);
    return mask.pixels[checkedIndexedPixelIndex(mask, x, y)];
}

void ProjectDocument::setBackgroundMaskPixel(std::size_t frameIndex,
                                             Resolution resolution,
                                             int x,
                                             int y,
                                             uint8_t value)
{
    IndexedFrame& mask = requireBackgroundMask(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t pixelIndex = checkedIndexedPixelIndex(mask, x, y);
    if (mask.pixels[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    m_dirty = true;
}

std::size_t ProjectDocument::spriteCount() const
{
    return m_sprites.size();
}

const SpriteRecord& ProjectDocument::sprite(std::size_t spriteIndex) const
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    return m_sprites[spriteIndex];
}

void ProjectDocument::setSpriteName(std::size_t spriteIndex, std::string name)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    if (m_sprites[spriteIndex].name == name) {
        return;
    }
    m_sprites[spriteIndex].name = std::move(name);
    m_dirty = true;
}

bool ProjectDocument::hasHdSprite(std::size_t spriteIndex) const
{
    return sprite(spriteIndex).hasHd;
}

void ProjectDocument::createHdSprite(std::size_t spriteIndex)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    if (sprite.hasHd) {
        return;
    }
    sprite.maskHd = upscaleSpriteIndexedFrame2x(sprite.originalSd);
    sprite.colorizedHd = upscaleSpriteRgb565Frame2x(sprite.colorizedSd);
    sprite.hasHd = true;
    m_dirty = true;
}

void ProjectDocument::removeHdSprite(std::size_t spriteIndex)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    if (!sprite.hasHd) {
        return;
    }
    sprite.hasHd = false;
    m_dirty = true;
}

uint8_t ProjectDocument::spriteOriginalPixel(std::size_t spriteIndex, int x, int y) const
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    const IndexedFrame& frame = m_sprites[spriteIndex].originalSd;
    return frame.pixels[checkedSpritePixelIndex(x, y)];
}

void ProjectDocument::replaceSpriteFromFrame(std::size_t spriteIndex,
                                             const IndexedFrame& originalFrame,
                                             const Rgb565Frame& colorizedFrame,
                                             int sourceX,
                                             int sourceY,
                                             int width,
                                             int height)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    if (!originalFrame.isValid() || !colorizedFrame.isValid() ||
        originalFrame.size.width != colorizedFrame.size.width ||
        originalFrame.size.height != colorizedFrame.size.height) {
        throw std::invalid_argument("source frame dimensions do not match");
    }
    if (sourceX < 0 || sourceY < 0 || width <= 0 || height <= 0 ||
        sourceX + width > originalFrame.size.width ||
        sourceY + height > originalFrame.size.height ||
        width > static_cast<int>(kMaxSpriteWidth) ||
        height > static_cast<int>(kMaxSpriteHeight)) {
        throw std::out_of_range("sprite capture bounds are out of range");
    }

    SpriteRecord& sprite = m_sprites[spriteIndex];
    IndexedFrame newOriginal =
        makeIndexedFrame(Size{static_cast<int>(kMaxSpriteWidth),
                              static_cast<int>(kMaxSpriteHeight)}, 255);
    Rgb565Frame newColorized =
        makeRgb565Frame(Size{static_cast<int>(kMaxSpriteWidth),
                             static_cast<int>(kMaxSpriteHeight)}, 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int frameX = sourceX + x;
            const int frameY = sourceY + y;
            const std::size_t framePixel =
                static_cast<std::size_t>(frameY) *
                    static_cast<std::size_t>(originalFrame.size.width) +
                static_cast<std::size_t>(frameX);
            const std::size_t spritePixel = spritePixelIndex(x, y);
            const uint8_t originalValue = originalFrame.pixels[framePixel];
            if (originalValue == 0) {
                continue;
            }
            newOriginal.pixels[spritePixel] = originalValue;
            newColorized.pixels[spritePixel] = colorizedFrame.pixels[framePixel];
        }
    }

    if (sprite.originalSd.pixels == newOriginal.pixels &&
        sprite.colorizedSd.pixels == newColorized.pixels) {
        return;
    }

    sprite.originalSd = std::move(newOriginal);
    sprite.colorizedSd = std::move(newColorized);
    if (sprite.hasHd) {
        sprite.maskHd = upscaleSpriteIndexedFrame2x(sprite.originalSd);
        sprite.colorizedHd = upscaleSpriteRgb565Frame2x(sprite.colorizedSd);
    }
    recalculateSpriteDetectionDwords(sprite);
    m_dirty = true;
}

void ProjectDocument::replaceSpriteFromFrameSelection(
    std::size_t spriteIndex,
    const IndexedFrame& originalFrame,
    const Rgb565Frame& colorizedFrame,
    const std::vector<render::Point>& sourcePixels)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    if (!originalFrame.isValid() || !colorizedFrame.isValid() ||
        originalFrame.size.width != colorizedFrame.size.width ||
        originalFrame.size.height != colorizedFrame.size.height) {
        throw std::invalid_argument("source frame dimensions do not match");
    }
    if (sourcePixels.empty()) {
        throw std::invalid_argument("sprite capture selection is empty");
    }

    int minX = originalFrame.size.width;
    int minY = originalFrame.size.height;
    int maxX = -1;
    int maxY = -1;
    std::vector<uint8_t> selected(originalFrame.pixels.size(), 0);
    for (const render::Point& point : sourcePixels) {
        if (point.x < 0 || point.y < 0 ||
            point.x >= originalFrame.size.width ||
            point.y >= originalFrame.size.height) {
            continue;
        }
        const std::size_t framePixel =
            static_cast<std::size_t>(point.y) *
                static_cast<std::size_t>(originalFrame.size.width) +
            static_cast<std::size_t>(point.x);
        if (selected[framePixel]) {
            continue;
        }
        selected[framePixel] = 1;
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    if (maxX < minX || maxY < minY) {
        throw std::invalid_argument("sprite capture selection is empty");
    }
    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    if (width > static_cast<int>(kMaxSpriteWidth) ||
        height > static_cast<int>(kMaxSpriteHeight)) {
        throw std::out_of_range("sprite capture selection is too large");
    }

    SpriteRecord& sprite = m_sprites[spriteIndex];
    IndexedFrame newOriginal =
        makeIndexedFrame(Size{static_cast<int>(kMaxSpriteWidth),
                              static_cast<int>(kMaxSpriteHeight)}, 255);
    Rgb565Frame newColorized =
        makeRgb565Frame(Size{static_cast<int>(kMaxSpriteWidth),
                             static_cast<int>(kMaxSpriteHeight)}, 0);

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const std::size_t framePixel =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(originalFrame.size.width) +
                static_cast<std::size_t>(x);
            if (!selected[framePixel]) {
                continue;
            }
            const std::size_t spritePixel = spritePixelIndex(x - minX, y - minY);
            newOriginal.pixels[spritePixel] = originalFrame.pixels[framePixel];
            newColorized.pixels[spritePixel] = colorizedFrame.pixels[framePixel];
        }
    }

    if (sprite.originalSd.pixels == newOriginal.pixels &&
        sprite.colorizedSd.pixels == newColorized.pixels) {
        return;
    }

    sprite.originalSd = std::move(newOriginal);
    sprite.colorizedSd = std::move(newColorized);
    if (sprite.hasHd) {
        sprite.maskHd = upscaleSpriteIndexedFrame2x(sprite.originalSd);
        sprite.colorizedHd = upscaleSpriteRgb565Frame2x(sprite.colorizedSd);
    }
    recalculateSpriteDetectionDwords(sprite);
    m_dirty = true;
}

void ProjectDocument::setSpriteOriginalPixel(std::size_t spriteIndex,
                                             int x,
                                             int y,
                                             uint8_t value)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    const std::size_t pixelIndex = checkedSpritePixelIndex(x, y);
    if (sprite.originalSd.pixels[pixelIndex] == value) {
        return;
    }
    sprite.originalSd.pixels[pixelIndex] = value;
    recalculateSpriteDetectionDwords(sprite);
    m_dirty = true;
}

uint16_t ProjectDocument::spriteColorizedPixel(std::size_t spriteIndex,
                                               Resolution resolution,
                                               int x,
                                               int y) const
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    const Rgb565Frame& frame = requireSpriteColorizedFrame(m_sprites[spriteIndex], resolution);
    return frame.pixels[checkedSpritePixelIndex(x, y)];
}

void ProjectDocument::setSpriteColorizedPixel(std::size_t spriteIndex,
                                              Resolution resolution,
                                              int x,
                                              int y,
                                              uint16_t rgb565)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    Rgb565Frame& frame = requireSpriteColorizedFrame(m_sprites[spriteIndex], resolution);
    const std::size_t pixelIndex = checkedSpritePixelIndex(x, y);
    if (frame.pixels[pixelIndex] == rgb565) {
        return;
    }
    frame.pixels[pixelIndex] = rgb565;
    m_dirty = true;
}

void ProjectDocument::setSpriteDetectionArea(std::size_t spriteIndex,
                                             std::size_t areaIndex,
                                             SpriteDetectionArea area)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    if (areaIndex >= sprite.detectionAreas.size() ||
        sprite.detectionAreas.size() != kMaxSpriteDetectAreas) {
        throw std::out_of_range("sprite detection area index out of range");
    }
    SpriteDetectionArea& existing = sprite.detectionAreas[areaIndex];
    uint32_t dword = 0xffffffff;
    uint16_t dwordPosition = 0xffff;
    if (const std::optional<std::pair<uint32_t, uint16_t>> calculated =
            detectionDwordForArea(sprite.originalSd, area)) {
        dword = calculated->first;
        dwordPosition = calculated->second;
    }
    if (existing.x == area.x &&
        existing.y == area.y &&
        existing.width == area.width &&
        existing.height == area.height &&
        sprite.detectionDwords[areaIndex] == dword &&
        sprite.detectionDwordPositions[areaIndex] == dwordPosition) {
        return;
    }
    existing = area;
    sprite.detectionDwords[areaIndex] = dword;
    sprite.detectionDwordPositions[areaIndex] = dwordPosition;
    m_dirty = true;
}

const std::vector<FrameSpriteReference>& ProjectDocument::frameSpriteReferences(
    std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).spriteReferences;
}

void ProjectDocument::setFrameSpriteReference(std::size_t frameIndex,
                                              std::size_t slotIndex,
                                              FrameSpriteReference reference)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.spriteReferences.empty()) {
        frame.spriteReferences.assign(kMaxSpritesPerFrame, FrameSpriteReference{});
    }
    if (slotIndex >= frame.spriteReferences.size() ||
        frame.spriteReferences.size() != kMaxSpritesPerFrame) {
        throw std::out_of_range("sprite slot index out of range");
    }
    if (reference.spriteId != 255 && reference.spriteId >= m_sprites.size()) {
        throw std::out_of_range("sprite reference index out of range");
    }
    if (frame.spriteReferences[slotIndex].spriteId == reference.spriteId &&
        frame.spriteReferences[slotIndex].minX == reference.minX &&
        frame.spriteReferences[slotIndex].minY == reference.minY &&
        frame.spriteReferences[slotIndex].maxX == reference.maxX &&
        frame.spriteReferences[slotIndex].maxY == reference.maxY) {
        return;
    }
    frame.spriteReferences[slotIndex] = reference;
    m_dirty = true;
}

void ProjectDocument::clearFrameSpriteReference(std::size_t frameIndex, std::size_t slotIndex)
{
    FrameSpriteReference reference = frameSpriteReferences(frameIndex).at(slotIndex);
    reference.spriteId = 255;
    setFrameSpriteReference(frameIndex, slotIndex, reference);
}

namespace {

Rgb565Frame& requireColorizedFrame(FrameRecord& frame, Resolution resolution)
{
    if (resolution == Resolution::Sd) {
        return frame.colorizedSd;
    }
    if (!frame.colorizedHd) {
        throw std::logic_error("HD frame is not available");
    }
    return *frame.colorizedHd;
}

}  // namespace

void ProjectDocument::setColorizedPixel(std::size_t frameIndex,
                                        Resolution resolution,
                                        int x,
                                        int y,
                                        uint16_t rgb565)
{
    Rgb565Frame& frame = requireColorizedFrame(requireFrame(m_frames, frameIndex), resolution);
    if (x < 0 || y < 0 || x >= frame.size.width || y >= frame.size.height) {
        throw std::out_of_range("pixel coordinate out of range");
    }
    const std::size_t pixelIndex =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
        static_cast<std::size_t>(x);
    if (frame.pixels[pixelIndex] == rgb565) {
        return;
    }
    frame.pixels[pixelIndex] = rgb565;
    m_dirty = true;
}

uint16_t ProjectDocument::colorizedPixel(std::size_t frameIndex,
                                         Resolution resolution,
                                         int x,
                                         int y) const
{
    const Rgb565Frame& frame = colorizedFrame(frameIndex, resolution);
    if (x < 0 || y < 0 || x >= frame.size.width || y >= frame.size.height) {
        throw std::out_of_range("pixel coordinate out of range");
    }
    const std::size_t pixelIndex =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.size.width) +
        static_cast<std::size_t>(x);
    return frame.pixels[pixelIndex];
}

void ProjectDocument::addUnsupportedPayload(UnsupportedLegacyPayload payload)
{
    m_unsupportedPayloads.push_back(std::move(payload));
    m_dirty = true;
}

const std::vector<UnsupportedLegacyPayload>& ProjectDocument::unsupportedPayloads() const
{
    return m_unsupportedPayloads;
}

ProjectSnapshot ProjectDocument::toSnapshot() const
{
    ProjectSnapshot snapshot;
    snapshot.romName = m_romName;
    snapshot.frames.reserve(m_frames.size());
    for (const FrameRecord& frame : m_frames) {
        snapshot.frames.push_back(ProjectFrameSnapshot{
            frame.originalSd,
            frame.colorizedSd,
            frame.colorizedHd,
            frame.shapeComparisonMode,
            frame.comparisonMaskId,
            frame.dynamicData,
            frame.backgroundData,
            frame.spriteReferences,
            frame.triggerId,
            frame.durationMs,
            frame.colorRotationsSd,
            frame.colorRotationsHd,
        });
    }
    snapshot.comparisonMasks = m_comparisonMasks;
    snapshot.backgrounds = m_backgrounds;
    snapshot.sprites = m_sprites;
    snapshot.unsupportedPayloads = m_unsupportedPayloads;
    return snapshot;
}

}  // namespace colorizing::document
