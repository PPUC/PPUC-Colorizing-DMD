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
        });
    }
    document.m_comparisonMasks = snapshot.comparisonMasks;
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

uint8_t ProjectDocument::comparisonMaskId(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).comparisonMaskId;
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
        });
    }
    snapshot.comparisonMasks = m_comparisonMasks;
    snapshot.unsupportedPayloads = m_unsupportedPayloads;
    return snapshot;
}

}  // namespace colorizing::document
