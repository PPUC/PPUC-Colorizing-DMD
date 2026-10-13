#include "document/ProjectDocument.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <stdexcept>
#include <utility>

#include "document/ProjectSnapshot.h"

namespace colorizing::document {

namespace {

uint64_t nextRevision()
{
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

bool hasSize(Size actual, Size expected)
{
    return actual == expected;
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

Rgb565Frame plainRgb565Frame(const IndexedFrame& original, uint32_t colorCount)
{
    const std::vector<uint16_t> colors = plainOriginalColors(colorCount);
    Rgb565Frame frame = makeRgb565Frame(original.size);
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        const uint8_t value = original.pixels[i];
        frame.pixels[i] = value < colors.size() ? colors[value] : uint16_t{0};
    }
    return frame;
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

// What error messages call the extra plane: "HD" where it is an extra resolution, its size
// where it is the only plane a project renders.
std::string extraPlaneName(const render::ProjectGeometry& geometry)
{
    if (geometry.layout() == render::PlaneLayout::Ordinary) {
        return "HD";
    }
    const Size size = geometry.planeSize(Resolution::Hd);
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

[[noreturn]] void throwMissingExtra(const render::ProjectGeometry& geometry, const char* item)
{
    throw std::logic_error(extraPlaneName(geometry) + " " + item + " is not available");
}

const Rgb565Frame& requireSpriteColorizedFrame(const SpriteRecord& sprite,
                                               Resolution resolution,
                                               const render::ProjectGeometry& geometry)
{
    if (resolution == Resolution::Hd && !sprite.hasHd) {
        throwMissingExtra(geometry, "sprite");
    }
    return resolution == Resolution::Hd ? sprite.colorizedHd : sprite.colorizedSd;
}

Rgb565Frame& requireSpriteColorizedFrame(SpriteRecord& sprite,
                                         Resolution resolution,
                                         const render::ProjectGeometry& geometry)
{
    if (resolution == Resolution::Hd && !sprite.hasHd) {
        throwMissingExtra(geometry, "sprite");
    }
    return resolution == Resolution::Hd ? sprite.colorizedHd : sprite.colorizedSd;
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

std::vector<uint16_t> plainOriginalColors(uint32_t colorCount)
{
    std::vector<uint16_t> colors(colorCount, 0);
    for (uint32_t i = 0; i < colorCount && colorCount > 1; ++i) {
        // Same float math as the Win32 editor, including the 5-bit value in the green field.
        const auto level = static_cast<uint16_t>(
            31.0 * static_cast<float>(i) / static_cast<float>(colorCount - 1));
        colors[i] = static_cast<uint16_t>((level << 11) | (level << 5));
    }
    return colors;
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
    document.m_colorCount = options.colorCount;
    document.m_frames.reserve(options.frameCount);
    for (uint32_t i = 0; i < options.frameCount; ++i) {
        document.m_frames.push_back(makeEmptyFrame(options.createHdFrames));
    }
    document.m_dirty = false;
    document.m_revision = nextRevision();
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
            frameSnapshot.inactiveHd,
        });
    }
    document.m_comparisonMasks = snapshot.comparisonMasks;
    document.m_backgrounds = snapshot.backgrounds;
    document.m_sprites = snapshot.sprites;
    for (SpriteRecord& sprite : document.m_sprites) {
        normalizeSpriteRecord(sprite, snapshot.colorCount);
    }
    document.m_palettes = snapshot.palettes;
    document.m_unsupportedPayloads = snapshot.unsupportedPayloads;
    document.m_editorMetadata = snapshot.editorMetadata;
    document.m_colorCount = snapshot.colorCount;
    document.m_cromRevision = snapshot.cromRevision;
    document.m_geometry = snapshot.geometry;
    if (document.m_geometry.discardsExtraPlane()) {
        for (FrameRecord& frame : document.m_frames) {
            frame.colorizedHd.reset();
            frame.inactiveHd.reset();
            frame.colorRotationsHd = {};
            if (frame.dynamicData) {
                frame.dynamicData->maskHd = {};
                frame.dynamicData->colorsHd = {};
                frame.dynamicData->shadowDirectionsHd = {};
                frame.dynamicData->shadowColorsHd = {};
            }
            if (frame.backgroundData) {
                frame.backgroundData->maskHd = {};
            }
        }
        for (BackgroundRecord& background : document.m_backgrounds) {
            background.colorizedHd.reset();
            background.inactiveHd.reset();
        }
        for (SpriteRecord& sprite : document.m_sprites) {
            sprite.hasHd = false;
            sprite.maskHd = {};
            sprite.colorizedHd = {};
            sprite.dynamicMaskHd = {};
            sprite.dynamicColorsHd = {};
        }
    }
    document.m_dirty = false;
    document.m_revision = nextRevision();
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
    markChanged();
}

std::size_t ProjectDocument::frameCount() const
{
    return m_frames.size();
}

uint32_t ProjectDocument::colorCount() const
{
    return m_colorCount;
}

const render::ProjectGeometry& ProjectDocument::geometry() const
{
    return m_geometry;
}

Size ProjectDocument::originalFrameSize() const
{
    return m_geometry.romFrameSize();
}

bool ProjectDocument::rendersHdPlaneOnly() const
{
    return m_geometry.layout() == render::PlaneLayout::NativeExtra;
}

bool ProjectDocument::editsOriginalPlaneOnly() const
{
    return !m_geometry.isEditable(Resolution::Hd);
}

// The carried SD plane of a project with 256x64 originals stays reachable: its data is read
// back through the accessors.
void ProjectDocument::requireEditablePlane(Resolution resolution) const
{
    if (resolution == Resolution::Hd) {
        requireEditableExtraPlane();
    }
}

void ProjectDocument::requireEditableExtraPlane() const
{
    if (editsOriginalPlaneOnly()) {
        throw std::logic_error("this project has no extra plane: only its original plane is "
                               "edited");
    }
}

FrameHdGaps ProjectDocument::frameHdGaps(std::size_t frameIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    FrameHdGaps gaps;
    if (!rendersHdPlaneOnly()) {
        return gaps;
    }
    gaps.frame = !frame.colorizedHd.has_value();
    gaps.background = frame.backgroundData &&
                      frame.backgroundData->backgroundId < m_backgrounds.size() &&
                      !m_backgrounds[frame.backgroundData->backgroundId].colorizedHd;
    for (const FrameSpriteReference& reference : frame.spriteReferences) {
        if (reference.spriteId < m_sprites.size() && !m_sprites[reference.spriteId].hasHd) {
            gaps.sprites.push_back(reference.spriteId);
        }
    }
    std::sort(gaps.sprites.begin(), gaps.sprites.end());
    gaps.sprites.erase(std::unique(gaps.sprites.begin(), gaps.sprites.end()),
                       gaps.sprites.end());
    return gaps;
}

const EditorMetadata& ProjectDocument::editorMetadata() const
{
    return m_editorMetadata;
}

bool ProjectDocument::isDirty() const
{
    return m_dirty;
}

uint64_t ProjectDocument::revision() const
{
    return m_revision;
}

void ProjectDocument::markChanged()
{
    m_dirty = true;
    m_revision = nextRevision();
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
    requireEditableExtraPlane();
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.colorizedHd) {
        return;
    }
    frame.colorizedHd = rendersHdPlaneOnly() ? plainRgb565Frame(frame.originalSd, m_colorCount)
                                           : upscaleRgb565Frame2x(frame.colorizedSd);
    frame.inactiveHd.reset();
    markChanged();
}

void ProjectDocument::removeHdFrame(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.colorizedHd) {
        return;
    }
    if (rendersHdPlaneOnly()) {
        throw std::logic_error("the " + extraPlaneName(m_geometry) +
                               " frame is the only one this project renders");
    }
    frame.colorizedHd.reset();
    markChanged();
}

void ProjectDocument::upscaleDynamicMaskToHd(std::size_t frameIndex)
{
    requireEditableExtraPlane();
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic mask data is not available");
    }
    if (rendersHdPlaneOnly()) {
        throw std::logic_error("this project has no smaller dynamic mask to upscale");
    }
    frame.dynamicData->maskHd = upscaleIndexedFrame2x(frame.dynamicData->maskSd);
    frame.dynamicData->colorsHd = frame.dynamicData->colorsSd;
    frame.dynamicData->shadowDirectionsHd = frame.dynamicData->shadowDirectionsSd;
    frame.dynamicData->shadowColorsHd = frame.dynamicData->shadowColorsSd;
    markChanged();
}

void ProjectDocument::upscaleBackgroundMaskToHd(std::size_t frameIndex)
{
    requireEditableExtraPlane();
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    if (rendersHdPlaneOnly()) {
        throw std::logic_error("this project has no smaller background mask to upscale");
    }
    frame.backgroundData->maskHd = upscaleIndexedFrame2x(frame.backgroundData->maskSd);
    markChanged();
}

void ProjectDocument::upscaleAssignedBackgroundToHd(std::size_t frameIndex)
{
    requireEditableExtraPlane();
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    if (frame.backgroundData->backgroundId >= m_backgrounds.size()) {
        throw std::logic_error("assigned background is not available");
    }
    BackgroundRecord& background = m_backgrounds[frame.backgroundData->backgroundId];
    if (rendersHdPlaneOnly()) {
        if (background.colorizedHd) {
            return;
        }
        background.colorizedHd = makeRgb565Frame(m_geometry.planeSize(Resolution::Hd));
        background.inactiveHd.reset();
        markChanged();
        return;
    }
    background.colorizedHd = upscaleRgb565Frame2x(background.colorizedSd);
    background.inactiveHd.reset();
    markChanged();
}

FrameHdState ProjectDocument::frameHdState(std::size_t frameIndex, FrameHdParts parts) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    FrameHdState state;
    state.colorizedHd = frame.colorizedHd;
    state.inactiveHd = frame.inactiveHd;
    if (parts.dynamic) {
        if (!frame.dynamicData) {
            throw std::logic_error("dynamic mask data is not available");
        }
        state.dynamic = FrameDynamicHdData{frame.dynamicData->maskHd,
                                           frame.dynamicData->colorsHd,
                                           frame.dynamicData->shadowDirectionsHd,
                                           frame.dynamicData->shadowColorsHd};
    }
    if ((parts.backgroundMask || parts.backgroundImage) && !frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    if (parts.backgroundMask) {
        state.backgroundMask = frame.backgroundData->maskHd;
    }
    if (parts.backgroundImage) {
        const std::size_t backgroundIndex = frame.backgroundData->backgroundId;
        if (backgroundIndex >= m_backgrounds.size()) {
            throw std::logic_error("assigned background is not available");
        }
        const BackgroundRecord& background = m_backgrounds[backgroundIndex];
        state.background =
            BackgroundHdData{backgroundIndex, background.colorizedHd, background.inactiveHd};
    }
    return state;
}

void ProjectDocument::setFrameHdState(std::size_t frameIndex, const FrameHdState& state)
{
    requireEditableExtraPlane();
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    const Size hdSize = m_geometry.planeSize(Resolution::Hd);
    if (state.colorizedHd && !hasSize(state.colorizedHd->size, hdSize)) {
        throw std::invalid_argument("HD frame has invalid size");
    }
    if (state.dynamic && !frame.dynamicData) {
        throw std::logic_error("dynamic mask data is not available");
    }
    if (state.backgroundMask && !frame.backgroundData) {
        throw std::logic_error("background data is not available");
    }
    if (state.background) {
        if (state.background->backgroundIndex >= m_backgrounds.size()) {
            throw std::out_of_range("background index out of range");
        }
        if (state.background->colorizedHd &&
            !hasSize(state.background->colorizedHd->size, hdSize)) {
            throw std::invalid_argument("HD background frame has invalid size");
        }
    }

    frame.colorizedHd = state.colorizedHd;
    frame.inactiveHd = state.inactiveHd;
    if (state.dynamic) {
        frame.dynamicData->maskHd = state.dynamic->mask;
        frame.dynamicData->colorsHd = state.dynamic->colors;
        frame.dynamicData->shadowDirectionsHd = state.dynamic->shadowDirections;
        frame.dynamicData->shadowColorsHd = state.dynamic->shadowColors;
    }
    if (state.backgroundMask) {
        frame.backgroundData->maskHd = *state.backgroundMask;
    }
    if (state.background) {
        BackgroundRecord& background = m_backgrounds[state.background->backgroundIndex];
        background.colorizedHd = state.background->colorizedHd;
        background.inactiveHd = state.background->inactiveHd;
    }
    markChanged();
}

const IndexedFrame& ProjectDocument::originalFrame(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).originalSd;
}

const Rgb565Frame& ProjectDocument::colorizedFrame(std::size_t frameIndex,
                                                   Resolution resolution) const
{
    requireEditablePlane(resolution);
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (resolution == Resolution::Sd) {
        return frame.colorizedSd;
    }
    if (!frame.colorizedHd) {
        throwMissingExtra(m_geometry, "frame");
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
    markChanged();
}

void ProjectDocument::restoreShapeComparisonMode(std::size_t frameIndex, uint8_t mode)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.shapeComparisonMode == mode) {
        return;
    }
    frame.shapeComparisonMode = mode;
    markChanged();
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
    markChanged();
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
    markChanged();
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
    markChanged();
}

const std::vector<uint16_t>& ProjectDocument::colorRotations(std::size_t frameIndex,
                                                             Resolution resolution) const
{
    requireEditablePlane(resolution);
    return requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
}

uint16_t ProjectDocument::colorRotationLength(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t slotIndex) const
{
    requireEditablePlane(resolution);
    const std::vector<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationOffset(slotIndex)];
}

uint16_t ProjectDocument::colorRotationDelayMs(std::size_t frameIndex,
                                               Resolution resolution,
                                               std::size_t slotIndex) const
{
    requireEditablePlane(resolution);
    const std::vector<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationOffset(slotIndex) + 1];
}

uint16_t ProjectDocument::colorRotationColor(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex,
                                             std::size_t colorIndex) const
{
    requireEditablePlane(resolution);
    const std::vector<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationColorOffset(slotIndex, colorIndex)];
}

void ProjectDocument::setColorRotationLength(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex,
                                             uint16_t length)
{
    requireEditablePlane(resolution);
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
    markChanged();
}

void ProjectDocument::setColorRotationDelayMs(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t slotIndex,
                                              uint16_t delayMs)
{
    requireEditablePlane(resolution);
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex) + 1;
    if (rotations[offset] == delayMs) {
        return;
    }
    rotations[offset] = delayMs;
    markChanged();
}

void ProjectDocument::setColorRotationColor(std::size_t frameIndex,
                                            Resolution resolution,
                                            std::size_t slotIndex,
                                            std::size_t colorIndex,
                                            uint16_t rgb565)
{
    requireEditablePlane(resolution);
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationColorOffset(slotIndex, colorIndex);
    if (rotations[offset] == rgb565) {
        return;
    }
    rotations[offset] = rgb565;
    markChanged();
}

void ProjectDocument::clearColorRotationSlot(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex)
{
    requireEditablePlane(resolution);
    std::vector<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex);
    const auto begin = rotations.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = begin + static_cast<std::ptrdiff_t>(kMaxColorRotationLength);
    if (std::all_of(begin, end, [](uint16_t value) { return value == 0; })) {
        return;
    }
    std::fill(begin, end, uint16_t{0});
    markChanged();
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
    markChanged();
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
    requireEditablePlane(resolution);
    const IndexedFrame& mask = requireDynamicMask(requireFrame(m_frames, frameIndex), resolution);
    return mask.pixels[checkedIndexedPixelIndex(mask, x, y)];
}

void ProjectDocument::setDynamicMaskPixel(std::size_t frameIndex,
                                          Resolution resolution,
                                          int x,
                                          int y,
                                          uint8_t value)
{
    requireEditablePlane(resolution);
    IndexedFrame& mask = requireDynamicMask(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t pixelIndex = checkedIndexedPixelIndex(mask, x, y);
    if (mask.pixels[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    markChanged();
}

uint16_t ProjectDocument::dynamicColor(std::size_t frameIndex,
                                       Resolution resolution,
                                       std::size_t setIndex,
                                       std::size_t originalColorIndex) const
{
    requireEditablePlane(resolution);
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
    requireEditablePlane(resolution);
    std::vector<uint16_t>& colors =
        requireDynamicColors(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t colorIndex =
        checkedDynamicColorIndex(colors, setIndex, originalColorIndex);
    if (colors[colorIndex] == rgb565) {
        return;
    }
    colors[colorIndex] = rgb565;
    markChanged();
}

uint8_t ProjectDocument::dynamicShadowDirection(std::size_t frameIndex,
                                                Resolution resolution,
                                                std::size_t setIndex) const
{
    requireEditablePlane(resolution);
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
    requireEditablePlane(resolution);
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
    requireEditablePlane(resolution);
    requireDynamicSetIndex(setIndex);
    std::vector<uint8_t>& directions =
        requireDynamicShadowDirections(requireFrame(m_frames, frameIndex), resolution);
    ensureDynamicSetVectorSize<uint8_t>(directions, 0);
    if (directions[setIndex] == direction) {
        return;
    }
    directions[setIndex] = direction;
    markChanged();
}

void ProjectDocument::setDynamicShadowColor(std::size_t frameIndex,
                                            Resolution resolution,
                                            std::size_t setIndex,
                                            uint16_t rgb565)
{
    requireEditablePlane(resolution);
    requireDynamicSetIndex(setIndex);
    std::vector<uint16_t>& colors =
        requireDynamicShadowColors(requireFrame(m_frames, frameIndex), resolution);
    ensureDynamicSetVectorSize<uint16_t>(colors, 0);
    if (colors[setIndex] == rgb565) {
        return;
    }
    colors[setIndex] = rgb565;
    markChanged();
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
                                          Resolution resolution,
                                          const render::ProjectGeometry& geometry)
{
    if (resolution == Resolution::Sd) {
        return background.colorizedSd;
    }
    if (!background.colorizedHd) {
        throwMissingExtra(geometry, "background");
    }
    return *background.colorizedHd;
}

Rgb565Frame& requireBackgroundFrame(BackgroundRecord& background,
                                    Resolution resolution,
                                    const render::ProjectGeometry& geometry)
{
    if (resolution == Resolution::Sd) {
        return background.colorizedSd;
    }
    if (!background.colorizedHd) {
        throwMissingExtra(geometry, "background");
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
    requireEditablePlane(resolution);
    return requireBackgroundFrame(requireBackground(m_backgrounds, backgroundIndex),
                                  resolution,
                                  m_geometry);
}

void ProjectDocument::setBackgroundFrame(std::size_t backgroundIndex,
                                         Resolution resolution,
                                         Rgb565Frame frame)
{
    requireEditablePlane(resolution);
    if (!hasSize(frame.size, m_geometry.planeSize(resolution))) {
        throw std::invalid_argument(resolution == Resolution::Sd
                                        ? "SD background frame has invalid size"
                                        : "HD background frame has invalid size");
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
        background.inactiveHd.reset();
    }
    markChanged();
}

void ProjectDocument::removeBackgroundHdFrame(std::size_t backgroundIndex)
{
    BackgroundRecord& background = requireBackground(m_backgrounds, backgroundIndex);
    if (!background.colorizedHd) {
        return;
    }
    background.colorizedHd.reset();
    markChanged();
}

uint16_t ProjectDocument::backgroundPixel(std::size_t backgroundIndex,
                                          Resolution resolution,
                                          int x,
                                          int y) const
{
    requireEditablePlane(resolution);
    const Rgb565Frame& frame = backgroundFrame(backgroundIndex, resolution);
    return frame.pixels[checkedRgb565PixelIndex(frame, x, y)];
}

void ProjectDocument::setBackgroundPixel(std::size_t backgroundIndex,
                                         Resolution resolution,
                                         int x,
                                         int y,
                                         uint16_t rgb565)
{
    requireEditablePlane(resolution);
    Rgb565Frame& frame =
        requireBackgroundFrame(requireBackground(m_backgrounds, backgroundIndex),
                               resolution,
                               m_geometry);
    const std::size_t pixelIndex = checkedRgb565PixelIndex(frame, x, y);
    if (frame.pixels[pixelIndex] == rgb565) {
        return;
    }
    frame.pixels[pixelIndex] = rgb565;
    markChanged();
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
    markChanged();
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
    requireEditablePlane(resolution);
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
    requireEditablePlane(resolution);
    IndexedFrame& mask = requireBackgroundMask(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t pixelIndex = checkedIndexedPixelIndex(mask, x, y);
    if (mask.pixels[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    markChanged();
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
    markChanged();
}

std::size_t ProjectDocument::paletteCount() const
{
    return m_palettes.size();
}

const PaletteRecord& ProjectDocument::palette(std::size_t paletteIndex) const
{
    if (paletteIndex >= m_palettes.size()) {
        throw std::out_of_range("palette index out of range");
    }
    return m_palettes[paletteIndex];
}

uint16_t ProjectDocument::paletteColor(std::size_t paletteIndex, std::size_t colorIndex) const
{
    const PaletteRecord& record = palette(paletteIndex);
    if (colorIndex >= record.colors.size()) {
        throw std::out_of_range("palette color index out of range");
    }
    return record.colors[colorIndex];
}

void ProjectDocument::setPaletteColor(std::size_t paletteIndex,
                                      std::size_t colorIndex,
                                      uint16_t rgb565)
{
    if (paletteIndex >= m_palettes.size()) {
        throw std::out_of_range("palette index out of range");
    }
    PaletteRecord& record = m_palettes[paletteIndex];
    if (colorIndex >= record.colors.size()) {
        throw std::out_of_range("palette color index out of range");
    }
    if (record.colors[colorIndex] == rgb565) {
        return;
    }
    record.colors[colorIndex] = rgb565;
    markChanged();
}

void ProjectDocument::setPaletteName(std::size_t paletteIndex, std::string name)
{
    if (paletteIndex >= m_palettes.size()) {
        throw std::out_of_range("palette index out of range");
    }
    if (m_palettes[paletteIndex].name == name) {
        return;
    }
    m_palettes[paletteIndex].name = std::move(name);
    markChanged();
}

bool ProjectDocument::hasHdSprite(std::size_t spriteIndex) const
{
    return sprite(spriteIndex).hasHd;
}

void ProjectDocument::createHdSprite(std::size_t spriteIndex)
{
    requireEditableExtraPlane();
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    if (sprite.hasHd) {
        return;
    }
    if (rendersHdPlaneOnly()) {
        sprite.maskHd = sprite.originalSd;
        sprite.colorizedHd = plainRgb565Frame(sprite.originalSd, m_colorCount);
    } else {
        sprite.maskHd = upscaleSpriteIndexedFrame2x(sprite.originalSd);
        sprite.colorizedHd = upscaleSpriteRgb565Frame2x(sprite.colorizedSd);
    }
    sprite.hasHd = true;
    markChanged();
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
    if (rendersHdPlaneOnly()) {
        throw std::logic_error("the " + extraPlaneName(m_geometry) +
                               " sprite is the only one this project renders");
    }
    sprite.hasHd = false;
    markChanged();
}

SpriteHdState ProjectDocument::spriteHdState(std::size_t spriteIndex) const
{
    const SpriteRecord& record = sprite(spriteIndex);
    return SpriteHdState{record.hasHd, record.maskHd, record.colorizedHd};
}

void ProjectDocument::setSpriteHdState(std::size_t spriteIndex, SpriteHdState state)
{
    requireEditableExtraPlane();
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    SpriteRecord& record = m_sprites[spriteIndex];
    record.hasHd = state.hasHd;
    record.maskHd = std::move(state.maskHd);
    record.colorizedHd = std::move(state.colorizedHd);
    markChanged();
}

void ProjectDocument::replaceCapturedHdOnlySprite(SpriteRecord& sprite,
                                                  IndexedFrame original,
                                                  Rgb565Frame colorized)
{
    if (sprite.hasHd && sprite.originalSd.pixels == original.pixels &&
        sprite.maskHd.pixels == original.pixels &&
        sprite.colorizedHd.pixels == colorized.pixels) {
        return;
    }
    sprite.maskHd = original;
    sprite.originalSd = std::move(original);
    sprite.colorizedHd = std::move(colorized);
    sprite.hasHd = true;
    // The runtime never reads the SD sprite of such a project; the old one belonged to the
    // replaced picture.
    std::fill(sprite.colorizedSd.pixels.begin(), sprite.colorizedSd.pixels.end(), uint16_t{0});
    std::fill(sprite.dynamicMaskSd.pixels.begin(),
              sprite.dynamicMaskSd.pixels.end(),
              uint8_t{255});
    std::fill(sprite.dynamicColorsSd.begin(), sprite.dynamicColorsSd.end(), uint16_t{0});
    recalculateSpriteDetectionDwords(sprite);
    markChanged();
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

    if (rendersHdPlaneOnly()) {
        replaceCapturedHdOnlySprite(sprite, std::move(newOriginal), std::move(newColorized));
        return;
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
    markChanged();
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

    if (rendersHdPlaneOnly()) {
        replaceCapturedHdOnlySprite(sprite, std::move(newOriginal), std::move(newColorized));
        return;
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
    markChanged();
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
    markChanged();
}

uint16_t ProjectDocument::spriteColorizedPixel(std::size_t spriteIndex,
                                               Resolution resolution,
                                               int x,
                                               int y) const
{
    requireEditablePlane(resolution);
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    const Rgb565Frame& frame =
        requireSpriteColorizedFrame(m_sprites[spriteIndex], resolution, m_geometry);
    return frame.pixels[checkedSpritePixelIndex(x, y)];
}

void ProjectDocument::setSpriteColorizedPixel(std::size_t spriteIndex,
                                              Resolution resolution,
                                              int x,
                                              int y,
                                              uint16_t rgb565)
{
    requireEditablePlane(resolution);
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    Rgb565Frame& frame =
        requireSpriteColorizedFrame(m_sprites[spriteIndex], resolution, m_geometry);
    const std::size_t pixelIndex = checkedSpritePixelIndex(x, y);
    if (frame.pixels[pixelIndex] == rgb565) {
        return;
    }
    frame.pixels[pixelIndex] = rgb565;
    markChanged();
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
    markChanged();
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
    markChanged();
}

void ProjectDocument::clearFrameSpriteReference(std::size_t frameIndex, std::size_t slotIndex)
{
    FrameSpriteReference reference = frameSpriteReferences(frameIndex).at(slotIndex);
    reference.spriteId = 255;
    setFrameSpriteReference(frameIndex, slotIndex, reference);
}

namespace {

Rgb565Frame& requireColorizedFrame(FrameRecord& frame,
                                   Resolution resolution,
                                   const render::ProjectGeometry& geometry)
{
    if (resolution == Resolution::Sd) {
        return frame.colorizedSd;
    }
    if (!frame.colorizedHd) {
        throwMissingExtra(geometry, "frame");
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
    requireEditablePlane(resolution);
    Rgb565Frame& frame =
        requireColorizedFrame(requireFrame(m_frames, frameIndex), resolution, m_geometry);
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
    markChanged();
}

uint16_t ProjectDocument::colorizedPixel(std::size_t frameIndex,
                                         Resolution resolution,
                                         int x,
                                         int y) const
{
    requireEditablePlane(resolution);
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
    markChanged();
}

const std::vector<UnsupportedLegacyPayload>& ProjectDocument::unsupportedPayloads() const
{
    return m_unsupportedPayloads;
}

namespace {

ProjectFrameSnapshot frameSnapshot(const FrameRecord& frame)
{
    return ProjectFrameSnapshot{
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
        frame.inactiveHd,
    };
}

// The subset's ID for a document item, added to the map on first use.
std::size_t subsetId(std::vector<std::size_t>& documentIndices, std::size_t documentIndex)
{
    const auto found =
        std::find(documentIndices.begin(), documentIndices.end(), documentIndex);
    if (found != documentIndices.end()) {
        return static_cast<std::size_t>(found - documentIndices.begin());
    }
    documentIndices.push_back(documentIndex);
    return documentIndices.size() - 1;
}

}  // namespace

ProjectSnapshot ProjectDocument::toSubsetSnapshot(const std::vector<std::size_t>& frameIndices,
                                                  SnapshotSubsetMap* map) const
{
    SnapshotSubsetMap localMap;
    SnapshotSubsetMap& ids = map ? *map : localMap;
    ids = SnapshotSubsetMap{};

    ProjectSnapshot snapshot;
    snapshot.romName = m_romName;
    snapshot.colorCount = m_colorCount;
    snapshot.cromRevision = m_cromRevision;
    snapshot.geometry = m_geometry;
    snapshot.frames.reserve(frameIndices.size());
    for (std::size_t frameIndex : frameIndices) {
        if (frameIndex >= m_frames.size()) {
            throw std::out_of_range("frame index is out of range");
        }
        ProjectFrameSnapshot frame = frameSnapshot(m_frames[frameIndex]);
        frame.comparisonMaskId =
            frame.comparisonMaskId < m_comparisonMasks.size()
                ? static_cast<uint8_t>(subsetId(ids.comparisonMasks, frame.comparisonMaskId))
                : uint8_t{255};
        if (frame.backgroundData) {
            const uint16_t backgroundId = frame.backgroundData->backgroundId;
            frame.backgroundData->backgroundId =
                backgroundId < m_backgrounds.size()
                    ? static_cast<uint16_t>(subsetId(ids.backgrounds, backgroundId))
                    : uint16_t{0xffff};
        }
        for (FrameSpriteReference& reference : frame.spriteReferences) {
            reference.spriteId =
                reference.spriteId < m_sprites.size()
                    ? static_cast<uint8_t>(subsetId(ids.sprites, reference.spriteId))
                    : uint8_t{255};
        }
        snapshot.frames.push_back(std::move(frame));
        ids.frames.push_back(frameIndex);
    }
    for (std::size_t maskIndex : ids.comparisonMasks) {
        snapshot.comparisonMasks.push_back(m_comparisonMasks[maskIndex]);
    }
    for (std::size_t backgroundIndex : ids.backgrounds) {
        snapshot.backgrounds.push_back(m_backgrounds[backgroundIndex]);
    }
    for (std::size_t spriteIndex : ids.sprites) {
        snapshot.sprites.push_back(m_sprites[spriteIndex]);
    }
    if (m_geometry.discardsExtraPlane()) {
        discardExtraPlane(snapshot);
    }
    return snapshot;
}

ProjectSnapshot ProjectDocument::toSnapshot() const
{
    ProjectSnapshot snapshot;
    snapshot.romName = m_romName;
    snapshot.frames.reserve(m_frames.size());
    for (const FrameRecord& frame : m_frames) {
        snapshot.frames.push_back(frameSnapshot(frame));
    }
    snapshot.comparisonMasks = m_comparisonMasks;
    snapshot.backgrounds = m_backgrounds;
    snapshot.sprites = m_sprites;
    snapshot.palettes = m_palettes;
    snapshot.unsupportedPayloads = m_unsupportedPayloads;
    snapshot.colorCount = m_colorCount;
    snapshot.cromRevision = m_cromRevision;
    snapshot.geometry = m_geometry;
    snapshot.editorMetadata = m_editorMetadata;
    if (m_geometry.discardsExtraPlane()) {
        discardExtraPlane(snapshot);
    }
    return snapshot;
}

}  // namespace colorizing::document
