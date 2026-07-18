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
        });
    }
    document.m_comparisonMasks = snapshot.comparisonMasks;
    document.m_backgrounds = snapshot.backgrounds;
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
    frame.colorizedHd = makeRgb565Frame(render::kHdFrameSize);
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
        });
    }
    snapshot.comparisonMasks = m_comparisonMasks;
    snapshot.backgrounds = m_backgrounds;
    snapshot.unsupportedPayloads = m_unsupportedPayloads;
    return snapshot;
}

}  // namespace colorizing::document
