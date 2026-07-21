#include "document/ProjectDocument.h"

#include <stdexcept>
#include <utility>

#include "document/ProjectSnapshot.h"
#include "render/CanvasLayout.h"

namespace colorizing::document {

namespace {

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
    if (existing.x == area.x &&
        existing.y == area.y &&
        existing.width == area.width &&
        existing.height == area.height) {
        return;
    }
    existing = area;
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
        });
    }
    snapshot.comparisonMasks = m_comparisonMasks;
    snapshot.backgrounds = m_backgrounds;
    snapshot.sprites = m_sprites;
    snapshot.unsupportedPayloads = m_unsupportedPayloads;
    return snapshot;
}

}  // namespace colorizing::document
