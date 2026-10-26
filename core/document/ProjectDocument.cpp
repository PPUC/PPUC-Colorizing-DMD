#include "document/ProjectDocument.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <atomic>
#include <map>
#include <string>
#include <stdexcept>
#include <utility>

#include "document/ProjectSnapshot.h"
#include "document/Upscale.h"

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

CompactArray<uint16_t> normalizedColorRotations(CompactArray<uint16_t> rotations)
{
    if (rotations.empty()) {
        return CompactArray<uint16_t>(kMaxColorRotations * kMaxColorRotationLength, 0);
    }
    return rotations;
}

CompactArray<uint16_t>& requireColorRotations(FrameRecord& frame, Resolution resolution)
{
    return resolution == Resolution::Hd ? frame.colorRotationsHd : frame.colorRotationsSd;
}

const CompactArray<uint16_t>& requireColorRotations(const FrameRecord& frame,
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

// The SD sprite fills the upper left quarter of its storage; the filter scales that quarter.
Rgb565Frame upscaleSpriteRgb565Frame2x(const Rgb565Frame& source,
                                       UpscaleFilter filter = UpscaleFilter::Nearest)
{
    if (filter != UpscaleFilter::Nearest) {
        const Size quarter{static_cast<int>(kMaxSpriteWidth / 2),
                           static_cast<int>(kMaxSpriteHeight / 2)};
        Rgb565Frame sd = makeRgb565Frame(quarter);
        for (int y = 0; y < quarter.height; ++y) {
            for (int x = 0; x < quarter.width; ++x) {
                sd.pixels[static_cast<std::size_t>(y) * quarter.width +
                          static_cast<std::size_t>(x)] =
                    source.pixels[static_cast<std::size_t>(y) * kMaxSpriteWidth +
                                  static_cast<std::size_t>(x)];
            }
        }
        return upscale2x(sd, filter);
    }
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

namespace {

template <typename Frame>
std::size_t ownedFrameBytes(const std::optional<Frame>& frame)
{
    return frame ? frame->pixels.ownedBytes() : 0;
}

}  // namespace

std::size_t ownedBytes(const FrameRecord& frame)
{
    std::size_t bytes = frame.originalSd.pixels.ownedBytes() + frame.colorizedSd.pixels.ownedBytes() +
                        ownedFrameBytes(frame.colorizedHd) + ownedFrameBytes(frame.inactiveHd) +
                        frame.colorRotationsSd.ownedBytes() + frame.colorRotationsHd.ownedBytes() +
                        frame.spriteReferences.capacity() * sizeof(FrameSpriteReference);
    if (frame.dynamicData) {
        const FrameDynamicData& dynamic = *frame.dynamicData;
        bytes += dynamic.maskSd.pixels.ownedBytes() + dynamic.maskHd.pixels.ownedBytes() +
                 dynamic.colorsSd.ownedBytes() + dynamic.colorsHd.ownedBytes() +
                 dynamic.shadowDirectionsSd.ownedBytes() + dynamic.shadowColorsSd.ownedBytes() +
                 dynamic.shadowDirectionsHd.ownedBytes() + dynamic.shadowColorsHd.ownedBytes();
    }
    if (frame.backgroundData) {
        bytes += frame.backgroundData->maskSd.pixels.ownedBytes() +
                 frame.backgroundData->maskHd.pixels.ownedBytes();
    }
    return bytes;
}

std::size_t ownedBytes(const SpriteRecord& sprite)
{
    return sprite.originalSd.pixels.ownedBytes() + sprite.colorizedSd.pixels.ownedBytes() +
           sprite.maskHd.pixels.ownedBytes() + sprite.colorizedHd.pixels.ownedBytes() +
           sprite.dynamicMaskSd.pixels.ownedBytes() + sprite.dynamicMaskHd.pixels.ownedBytes() +
           sprite.dynamicColorsSd.ownedBytes() + sprite.dynamicColorsHd.ownedBytes();
}

std::size_t ownedBytes(const BackgroundRecord& background)
{
    return background.colorizedSd.pixels.ownedBytes() +
           ownedFrameBytes(background.colorizedHd) + ownedFrameBytes(background.inactiveHd);
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
    document.ensureComponentIds();
    document.m_dirty = false;
    document.m_revision = nextRevision();
    document.m_identificationRevision = document.m_revision;
    return document;
}

namespace {

// What a document does not hold of a frame when the layout discards the extra plane.
void dropDiscardedPlane(FrameRecord& frame)
{
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

std::string sizeText(Size size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

// Throws std::invalid_argument, naming the block, when a frame that is to be inserted does
// not fit the document: every block in the size of its plane, as a frame of this document
// holds it (without extra-plane data where the document discards that plane).
void requireFittingFrameRecord(const FrameRecord& frame,
                               std::size_t position,
                               const render::ProjectGeometry& geometry,
                               uint32_t colorCount,
                               std::size_t spritesPerFrame)
{
    const std::string prefix = "frame " + std::to_string(position) + " to insert: ";
    const auto requireBlock = [&prefix](bool valid, Size actual, Size expected,
                                        const char* what) {
        if (!hasSize(actual, expected)) {
            throw std::invalid_argument(prefix + what + " is " + sizeText(actual) +
                                        ", the project needs " + sizeText(expected));
        }
        if (!valid) {
            throw std::invalid_argument(prefix + what +
                                        " has a pixel count that does not match its size");
        }
    };
    const auto requireCount = [&prefix](std::size_t actual, std::size_t expected,
                                        const char* what) {
        if (actual != expected) {
            throw std::invalid_argument(prefix + what + " has " + std::to_string(actual) +
                                        " values, the project needs " +
                                        std::to_string(expected));
        }
    };
    const auto requireAbsent = [&prefix](bool present, const char* what) {
        if (present) {
            throw std::invalid_argument(prefix + what +
                                        " is present, but the project has no extra plane");
        }
    };
    const bool extra = !geometry.discardsExtraPlane();
    const Size romSize = geometry.romFrameSize();
    const Size sdSize = geometry.planeSize(Resolution::Sd);
    const Size hdSize = geometry.planeSize(Resolution::Hd);
    const std::size_t rotationValues = kMaxColorRotations * kMaxColorRotationLength;
    const std::size_t dynamicColors = kMaxDynamicSetsPerFrame * colorCount;

    requireBlock(frame.originalSd.isValid(), frame.originalSd.size, romSize, "the original");
    requireBlock(frame.colorizedSd.isValid(), frame.colorizedSd.size, sdSize,
                 "the colorized frame");
    requireCount(frame.colorRotationsSd.size(), rotationValues, "the color rotations");
    if (extra) {
        if (frame.colorizedHd) {
            requireBlock(frame.colorizedHd->isValid(), frame.colorizedHd->size, hdSize,
                         "the extra-plane frame");
        }
        if (frame.inactiveHd) {
            if (frame.colorizedHd) {
                throw std::invalid_argument(prefix +
                                            "has both an extra-plane frame and inactive "
                                            "extra-plane pixels");
            }
            requireBlock(frame.inactiveHd->isValid(), frame.inactiveHd->size, hdSize,
                         "the inactive extra-plane frame");
        }
        requireCount(frame.colorRotationsHd.size(), rotationValues,
                     "the extra-plane color rotations");
    } else {
        requireAbsent(frame.colorizedHd.has_value(), "an extra-plane frame");
        requireAbsent(frame.inactiveHd.has_value(), "an inactive extra-plane frame");
        requireAbsent(!frame.colorRotationsHd.empty(), "extra-plane color rotations");
    }
    if (frame.dynamicData) {
        const FrameDynamicData& dynamic = *frame.dynamicData;
        requireBlock(dynamic.maskSd.isValid(), dynamic.maskSd.size, sdSize, "the dynamic mask");
        requireCount(dynamic.colorsSd.size(), dynamicColors, "the dynamic colors");
        if (!dynamic.shadowDirectionsSd.empty() || !dynamic.shadowColorsSd.empty()) {
            requireCount(dynamic.shadowDirectionsSd.size(), kMaxDynamicSetsPerFrame,
                         "the dynamic shadow directions");
            requireCount(dynamic.shadowColorsSd.size(), kMaxDynamicSetsPerFrame,
                         "the dynamic shadow colors");
        }
        if (extra) {
            requireBlock(dynamic.maskHd.isValid(), dynamic.maskHd.size, hdSize,
                         "the extra-plane dynamic mask");
            requireCount(dynamic.colorsHd.size(), dynamicColors,
                         "the extra-plane dynamic colors");
            if (!dynamic.shadowDirectionsHd.empty() || !dynamic.shadowColorsHd.empty()) {
                requireCount(dynamic.shadowDirectionsHd.size(), kMaxDynamicSetsPerFrame,
                             "the extra-plane dynamic shadow directions");
                requireCount(dynamic.shadowColorsHd.size(), kMaxDynamicSetsPerFrame,
                             "the extra-plane dynamic shadow colors");
            }
        } else {
            requireAbsent(!dynamic.maskHd.pixels.empty() || !dynamic.colorsHd.empty() ||
                              !dynamic.shadowDirectionsHd.empty() ||
                              !dynamic.shadowColorsHd.empty(),
                          "extra-plane dynamic data");
        }
    }
    if (frame.backgroundData) {
        requireBlock(frame.backgroundData->maskSd.isValid(), frame.backgroundData->maskSd.size,
                     sdSize, "the background mask");
        if (extra) {
            requireBlock(frame.backgroundData->maskHd.isValid(),
                         frame.backgroundData->maskHd.size, hdSize,
                         "the extra-plane background mask");
        } else {
            requireAbsent(!frame.backgroundData->maskHd.pixels.empty(),
                          "an extra-plane background mask");
        }
    }
    if (!frame.spriteReferences.empty()) {
        requireCount(frame.spriteReferences.size(), spritesPerFrame, "the sprite slots");
    }
}

// Defined with toSnapshot() below.
ProjectFrameSnapshot frameSnapshot(const FrameRecord& frame);

FrameRecord frameRecord(ProjectFrameSnapshot&& frameSnapshot)
{
    return FrameRecord{
        std::move(frameSnapshot.originalSd),
        std::move(frameSnapshot.colorizedSd),
        std::move(frameSnapshot.colorizedHd),
        frameSnapshot.shapeComparisonMode,
        frameSnapshot.comparisonMaskId,
        std::move(frameSnapshot.dynamicData),
        std::move(frameSnapshot.backgroundData),
        std::move(frameSnapshot.spriteReferences),
        frameSnapshot.triggerId,
        frameSnapshot.durationMs,
        normalizedColorRotations(std::move(frameSnapshot.colorRotationsSd)),
        normalizedColorRotations(std::move(frameSnapshot.colorRotationsHd)),
        std::move(frameSnapshot.inactiveHd),
        frameSnapshot.scene,
        std::move(frameSnapshot.tags),
        frameSnapshot.lights,
        frameSnapshot.originalOnlyDynamicSets,
        std::move(frameSnapshot.meta),
    };
}

// Gives a new ID to every component in the list that has none or the ID of a component before
// it, so the first of several with one ID keeps it. A component that gets a new ID is a new
// one: it has no revision and no author yet.
void giveUniqueIds(const std::vector<ComponentMeta*>& metas)
{
    std::unordered_set<ComponentId, ComponentIdHash> seen;
    seen.reserve(metas.size());
    for (ComponentMeta* meta : metas) {
        if (meta->id.isNull() || !seen.insert(meta->id).second) {
            *meta = ComponentMeta{};
            meta->id = ComponentId::generate();
            seen.insert(meta->id);
        }
    }
}

ComponentMeta& recordMeta(FrameRecord& record) { return record.meta; }
ComponentMeta& recordMeta(BackgroundRecord& record) { return record.meta; }
ComponentMeta& recordMeta(SpriteRecord& record) { return record.meta; }
ComponentMeta& recordMeta(PaletteRecord& record) { return record.meta; }
ComponentMeta& recordMeta(ComponentMeta& meta) { return meta; }

template <typename Record>
void ensureUniqueIds(std::vector<Record>& records)
{
    std::vector<ComponentMeta*> metas;
    metas.reserve(records.size());
    for (Record& record : records) {
        metas.push_back(&recordMeta(record));
    }
    giveUniqueIds(metas);
}

// The record at newIndex goes last, so that it is the one that gets a new ID when another
// record has its ID.
template <typename Record>
void ensureUniqueIdOf(std::vector<Record>& records, std::size_t newIndex)
{
    std::vector<ComponentMeta*> metas;
    metas.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index != newIndex) {
            metas.push_back(&recordMeta(records[index]));
        }
    }
    metas.push_back(&recordMeta(records[newIndex]));
    giveUniqueIds(metas);
}

}  // namespace

void ProjectDocument::ensureComponentIds()
{
    if (m_projectId.isNull()) {
        m_projectId = ComponentId::generate();
    }
    ensureUniqueIds(m_frames);
    m_comparisonMaskMeta.resize(m_comparisonMasks.size());
    ensureUniqueIds(m_comparisonMaskMeta);
    ensureUniqueIds(m_backgrounds);
    ensureUniqueIds(m_sprites);
    ensureUniqueIds(m_palettes);
}

const ComponentMeta& ProjectDocument::frameMeta(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).meta;
}

const ComponentMeta& ProjectDocument::comparisonMaskMeta(std::size_t maskIndex) const
{
    if (maskIndex >= m_comparisonMaskMeta.size()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    return m_comparisonMaskMeta[maskIndex];
}

const ComponentMeta& ProjectDocument::backgroundMeta(std::size_t backgroundIndex) const
{
    return background(backgroundIndex).meta;
}

const ComponentMeta& ProjectDocument::spriteMeta(std::size_t spriteIndex) const
{
    return sprite(spriteIndex).meta;
}

const ComponentMeta& ProjectDocument::paletteMeta(std::size_t paletteIndex) const
{
    return palette(paletteIndex).meta;
}

void ProjectDocument::setTeam(std::optional<TeamState> team)
{
    if (team == m_team) {
        return;
    }
    m_team = std::move(team);
    m_dirty = true;
}

void ProjectDocument::adoptComponentMeta(const std::vector<StampedMeta>& stamped)
{
    std::unordered_map<ComponentId, ComponentMeta*, ComponentIdHash> metas;
    const auto add = [&metas](ComponentMeta& meta) { metas.emplace(meta.id, &meta); };
    // IDs are unique within a kind only; the kinds are looked up one after another.
    for (const ComponentKind kind :
         {ComponentKind::Frame, ComponentKind::ComparisonMask, ComponentKind::Background,
          ComponentKind::Sprite, ComponentKind::Palette}) {
        metas.clear();
        switch (kind) {
        case ComponentKind::Frame:
            std::for_each(m_frames.begin(), m_frames.end(),
                          [&](FrameRecord& record) { add(record.meta); });
            break;
        case ComponentKind::ComparisonMask:
            std::for_each(m_comparisonMaskMeta.begin(), m_comparisonMaskMeta.end(), add);
            break;
        case ComponentKind::Background:
            std::for_each(m_backgrounds.begin(), m_backgrounds.end(),
                          [&](BackgroundRecord& record) { add(record.meta); });
            break;
        case ComponentKind::Sprite:
            std::for_each(m_sprites.begin(), m_sprites.end(),
                          [&](SpriteRecord& record) { add(record.meta); });
            break;
        case ComponentKind::Palette:
            std::for_each(m_palettes.begin(), m_palettes.end(),
                          [&](PaletteRecord& record) { add(record.meta); });
            break;
        }
        for (const StampedMeta& entry : stamped) {
            if (entry.kind != kind) {
                continue;
            }
            const auto found = metas.find(entry.meta.id);
            if (found != metas.end()) {
                *found->second = entry.meta;
            }
        }
    }
}

std::optional<std::size_t> ProjectDocument::frameIndexOf(const ComponentId& id) const
{
    const auto found = std::find_if(m_frames.begin(), m_frames.end(),
                                    [&id](const FrameRecord& frame) { return frame.meta.id == id; });
    if (found == m_frames.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - m_frames.begin());
}

ProjectDocument ProjectDocument::fromSnapshot(ProjectSnapshot snapshot)
{
    std::string errorMessage;
    if (!isValidProjectSnapshot(snapshot, &errorMessage)) {
        throw std::invalid_argument(errorMessage);
    }

    ProjectDocument document;
    if (snapshot.geometry.discardsExtraPlane()) {
        // Dropped by the load, and what this drops below.
        document.m_discardedExtraPlane = snapshot.discardedExtraPlane;
        document.m_discardedExtraPlane += extraPlanePictures(snapshot);
    }
    document.m_romName = std::move(snapshot.romName);
    document.m_projectId = snapshot.projectId;
    document.m_team = std::move(snapshot.team);
    // The sprite slots of a cROM before format version 1 filled up; a new slot is empty and
    // searches the whole frame, as one of a new frame.
    const Size romSize = ::colorizing::document::originalFrameSize(snapshot);
    FrameSpriteReference emptySlot;
    emptySlot.spriteId = kNoSpriteId;
    emptySlot.maxX = static_cast<uint16_t>(romSize.width - 1);
    emptySlot.maxY = static_cast<uint16_t>(romSize.height - 1);
    document.m_frames.reserve(snapshot.frames.size());
    for (ProjectFrameSnapshot& frameSnapshot : snapshot.frames) {
        if (!frameSnapshot.spriteReferences.empty()) {
            frameSnapshot.spriteReferences.resize(kSpritesPerFrame, emptySlot);
        }
        document.m_frames.push_back(frameRecord(std::move(frameSnapshot)));
    }
    document.m_comparisonMasks = std::move(snapshot.comparisonMasks);
    document.m_comparisonMaskMeta = std::move(snapshot.comparisonMaskMeta);
    document.m_backgrounds = std::move(snapshot.backgrounds);
    document.m_sprites = std::move(snapshot.sprites);
    for (SpriteRecord& sprite : document.m_sprites) {
        normalizeSpriteRecord(sprite, snapshot.colorCount);
    }
    document.m_palettes = std::move(snapshot.palettes);
    document.m_unsupportedPayloads = std::move(snapshot.unsupportedPayloads);
    document.m_editorMetadata = std::move(snapshot.editorMetadata);
    ensureSpriteTableEntries(document.m_editorMetadata, document.m_sprites.size());
    document.m_helperLines = std::move(snapshot.helperLines);
    document.m_imageZones = std::move(snapshot.imageZones);
    document.m_colorizationSettings = std::move(snapshot.colorizationSettings);
    document.m_speakerLights = std::move(snapshot.speakerLights);
    document.m_license = std::move(snapshot.license);
    document.m_colorCount = snapshot.colorCount;
    document.m_geometry = snapshot.geometry;
    if (document.m_geometry.discardsExtraPlane()) {
        for (FrameRecord& frame : document.m_frames) {
            dropDiscardedPlane(frame);
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
    document.ensureComponentIds();
    document.m_dirty = false;
    document.m_revision = nextRevision();
    document.m_identificationRevision = document.m_revision;
    document.shareIdenticalBlocks();
    return document;
}

std::size_t ProjectDocument::storageItemCount() const
{
    return m_frames.size() + m_comparisonMasks.size() + m_backgrounds.size() + m_sprites.size();
}

template <typename Visit>
void ProjectDocument::visitItemBlocks(std::size_t item, Visit&& visit)
{
    if (item < m_frames.size()) {
        FrameRecord& frame = m_frames[item];
        visit(frame.originalSd.pixels);
        visit(frame.colorizedSd.pixels);
        if (frame.colorizedHd) {
            visit(frame.colorizedHd->pixels);
        }
        if (frame.inactiveHd) {
            visit(frame.inactiveHd->pixels);
        }
        visit(frame.colorRotationsSd);
        visit(frame.colorRotationsHd);
        if (frame.dynamicData) {
            FrameDynamicData& dynamic = *frame.dynamicData;
            visit(dynamic.maskSd.pixels);
            visit(dynamic.maskHd.pixels);
            visit(dynamic.colorsSd);
            visit(dynamic.colorsHd);
            visit(dynamic.shadowDirectionsSd);
            visit(dynamic.shadowColorsSd);
            visit(dynamic.shadowDirectionsHd);
            visit(dynamic.shadowColorsHd);
        }
        if (frame.backgroundData) {
            visit(frame.backgroundData->maskSd.pixels);
            visit(frame.backgroundData->maskHd.pixels);
        }
        return;
    }
    item -= m_frames.size();
    if (item < m_comparisonMasks.size()) {
        visit(m_comparisonMasks[item].pixels);
        return;
    }
    item -= m_comparisonMasks.size();
    if (item < m_backgrounds.size()) {
        BackgroundRecord& background = m_backgrounds[item];
        visit(background.colorizedSd.pixels);
        if (background.colorizedHd) {
            visit(background.colorizedHd->pixels);
        }
        if (background.inactiveHd) {
            visit(background.inactiveHd->pixels);
        }
        return;
    }
    item -= m_backgrounds.size();
    if (item < m_sprites.size()) {
        SpriteRecord& sprite = m_sprites[item];
        visit(sprite.originalSd.pixels);
        visit(sprite.colorizedSd.pixels);
        visit(sprite.maskHd.pixels);
        visit(sprite.colorizedHd.pixels);
        visit(sprite.dynamicMaskSd.pixels);
        visit(sprite.dynamicMaskHd.pixels);
        visit(sprite.dynamicColorsSd);
        visit(sprite.dynamicColorsHd);
    }
}

std::size_t ProjectDocument::shareIdenticalBlocks()
{
    // Per value type and content: the arrays met first. A hash finds the candidates, the values
    // decide.
    std::unordered_map<uint64_t, std::vector<CompactArray<uint8_t>*>> bytes8;
    std::unordered_map<uint64_t, std::vector<CompactArray<uint16_t>*>> bytes16;
    std::size_t saved = 0;
    const auto share = [&saved](auto& array, auto& seen) {
        if (array.empty() || array.isSharedUniform()) {
            return;
        }
        const std::string_view content(reinterpret_cast<const char*>(array.data()),
                                       array.size() * sizeof(*array.data()));
        const uint64_t key = std::hash<std::string_view>{}(content) ^ (array.size() * 0x9e3779b97f4a7c15ull);
        auto& candidates = seen[key];
        for (auto* other : candidates) {
            if (other->storage() == array.storage()) {
                return;
            }
            if (*other == array) {
                saved += array.storageUseCount() == 1 ? array.storageBytes() : 0;
                array.shareStorage(*other);
                return;
            }
        }
        candidates.push_back(&array);
    };
    for (std::size_t item = 0; item < storageItemCount(); ++item) {
        visitItemBlocks(item, [&](auto& array) {
            using Value = std::decay_t<decltype(*array.data())>;
            if constexpr (sizeof(Value) == 1) {
                share(array, bytes8);
            } else {
                share(array, bytes16);
            }
        });
    }
    return saved;
}

ProjectDocument::PackProgress ProjectDocument::packBlocks(
    const std::vector<std::size_t>& keepFrames,
    std::chrono::steady_clock::duration budget)
{
    const auto start = std::chrono::steady_clock::now();
    PackProgress progress;
    if (m_packRevision != m_revision || m_packHolders.empty()) {
        // A pass starts over: who holds which buffer now.
        m_packHolders.clear();
        for (std::size_t item = 0; item < storageItemCount(); ++item) {
            visitItemBlocks(item, [this](auto& array) {
                if (!array.empty() && !array.isSharedUniform()) {
                    ++m_packHolders[array.storage()];
                }
            });
        }
        m_packCursor = 0;
        m_packRevision = m_revision;
    }
    const std::size_t items = storageItemCount();
    while (m_packCursor < items) {
        const std::size_t item = m_packCursor++;
        if (item < m_frames.size() &&
            std::find(keepFrames.begin(), keepFrames.end(), item) != keepFrames.end()) {
            continue;
        }
        visitItemBlocks(item, [this, &progress](auto& array) {
            if (array.empty() || array.isSharedUniform() || array.isPacked()) {
                return;
            }
            const auto holders = m_packHolders.find(array.storage());
            // Held by anything outside the document (a snapshot, a copy, an undo step), or
            // by an array of the document that is new since the pass began: left as it is.
            if (holders == m_packHolders.end() || holders->second != array.storageUseCount()) {
                return;
            }
            progress.bytesFreed += array.packStorage();
        });
        if (std::chrono::steady_clock::now() - start >= budget) {
            return progress;
        }
    }
    progress.done = true;
    m_packHolders.clear();
    return progress;
}

std::size_t ProjectDocument::heldBlockBytes() const
{
    std::unordered_map<const void*, std::size_t> buffers;
    auto& self = const_cast<ProjectDocument&>(*this);
    for (std::size_t item = 0; item < storageItemCount(); ++item) {
        self.visitItemBlocks(item, [&buffers](const auto& array) {
            if (!array.empty()) {
                buffers.emplace(array.storage(), array.storageBytes());
            }
        });
    }
    std::size_t bytes = 0;
    for (const auto& [buffer, size] : buffers) {
        bytes += size;
    }
    return bytes;
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

Resolution ProjectDocument::dynamicPlane() const
{
    return m_geometry.nativePlane();
}

std::size_t clearUnusedHdDynamicZones(ProjectDocument& document)
{
    if (document.geometry().layout() != render::PlaneLayout::Ordinary) {
        return 0;
    }
    std::size_t cleared = 0;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        if (document.clearUnusedHdDynamicZones(frame)) {
            ++cleared;
        }
    }
    return cleared;
}

std::size_t stripSceneFrames(ProjectDocument& document)
{
    std::size_t stripped = 0;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        if (document.stripSceneFrame(frame)) {
            ++stripped;
        }
    }
    return stripped;
}

bool ProjectDocument::clearUnusedHdDynamicZones(std::size_t frameIndex)
{
    if (m_geometry.layout() != render::PlaneLayout::Ordinary) {
        throw std::logic_error("only an ordinary project has unused 256x64 dynamic zones");
    }
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.dynamicData) {
        return false;
    }
    FrameDynamicData& data = *frame.dynamicData;
    // Read through const: a non-const access gives a packed array a buffer of its own.
    const CompactArray<uint8_t>& mask = data.maskHd.pixels;
    if (std::all_of(mask.begin(), mask.end(), [](uint8_t value) { return value == 255; })) {
        return false;
    }
    std::fill(data.maskHd.pixels.begin(), data.maskHd.pixels.end(), uint8_t{255});
    std::fill(data.colorsHd.begin(), data.colorsHd.end(), uint16_t{0});
    markChanged();
    return true;
}

void ProjectDocument::requireDynamicPlane(Resolution resolution) const
{
    requireEditablePlane(resolution);
    if (resolution != dynamicPlane()) {
        const Size size = m_geometry.planeSize(dynamicPlane());
        throw std::logic_error("dynamic zones are defined on the " + std::to_string(size.width) +
                               "x" + std::to_string(size.height) +
                               " plane; the runtime does not use those of the other plane");
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

bool ColorSetTables::operator==(const ColorSetTables& other) const
{
    return editColors == other.editColors && activeColorSets == other.activeColorSets &&
           colorSets == other.colorSets && colorSetNames == other.colorSetNames;
}

bool ColorSetTables::operator!=(const ColorSetTables& other) const
{
    return !(*this == other);
}

ColorSetTables ProjectDocument::colorSetTables() const
{
    return ColorSetTables{m_editorMetadata.editColors,
                          m_editorMetadata.activeColorSets,
                          m_editorMetadata.colorSets,
                          m_editorMetadata.colorSetNames};
}

void ProjectDocument::setColorSetTables(ColorSetTables tables)
{
    if (tables.editColors.size() != kEditColorCount ||
        tables.activeColorSets.size() != kMaxColorSets ||
        tables.colorSets.size() != kMaxColorSets * kColorSetColorCount ||
        tables.colorSetNames.size() != kMaxColorSets * kColorSetNameBytes) {
        throw std::invalid_argument("color set tables have the wrong sizes");
    }
    if (tables == colorSetTables()) {
        return;
    }
    m_editorMetadata.editColors = std::move(tables.editColors);
    m_editorMetadata.activeColorSets = std::move(tables.activeColorSets);
    m_editorMetadata.colorSets = std::move(tables.colorSets);
    m_editorMetadata.colorSetNames = std::move(tables.colorSetNames);
    markChanged();
}

uint16_t ProjectDocument::editColor(std::size_t slot) const
{
    if (slot >= m_editorMetadata.editColors.size()) {
        throw std::out_of_range("edit color slot out of range");
    }
    return m_editorMetadata.editColors[slot];
}

void ProjectDocument::setFirstDisplayedColorSet(uint8_t setIndex)
{
    if (setIndex >= kMaxColorSets) {
        throw std::out_of_range("color set index out of range");
    }
    m_editorMetadata.firstDisplayedColorSet = setIndex;
}

void ProjectDocument::setDrawColorMode(uint32_t mode)
{
    m_editorMetadata.drawColorMode = mode;
}

bool ProjectDocument::isDirty() const
{
    return m_dirty;
}

uint64_t ProjectDocument::revision() const
{
    return m_revision;
}

uint64_t ProjectDocument::identificationRevision() const
{
    return m_identificationRevision;
}

const std::vector<HelperLine>& ProjectDocument::helperLines() const
{
    return m_helperLines;
}

void ProjectDocument::setHelperLines(std::vector<HelperLine> lines)
{
    if (lines == m_helperLines) {
        return;
    }
    m_helperLines = std::move(lines);
    markChanged();
}

const std::vector<SavedImageZone>& ProjectDocument::imageZones() const
{
    return m_imageZones;
}

void ProjectDocument::setImageZones(std::vector<SavedImageZone> zones)
{
    if (zones == m_imageZones) {
        return;
    }
    m_imageZones = std::move(zones);
    markChanged();
}

const std::optional<ColorizationSettings>& ProjectDocument::colorizationSettings() const
{
    return m_colorizationSettings;
}

void ProjectDocument::setColorizationSettings(std::optional<ColorizationSettings> settings)
{
    if (settings == m_colorizationSettings) {
        return;
    }
    // Which frames are scene frames is part of identification.
    const bool scenesChanged =
        (settings ? settings->scenes : std::vector<SceneDefinition>{}) !=
        (m_colorizationSettings ? m_colorizationSettings->scenes
                                : std::vector<SceneDefinition>{});
    m_colorizationSettings = std::move(settings);
    markChanged();
    if (scenesChanged) {
        m_identificationRevision = m_revision;
    }
}

const std::optional<SceneFrameRef>& ProjectDocument::sceneFrame(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).scene;
}

void ProjectDocument::setSceneFrame(std::size_t frameIndex, std::optional<SceneFrameRef> scene)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.scene == scene) {
        return;
    }
    frame.scene = scene;
    markChanged();
}

const SceneDefinition* ProjectDocument::backgroundScene(std::size_t frameIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    const Size rom = m_geometry.romFrameSize();
    if (!m_colorizationSettings || frame.triggerId > 0xffff || rom.width != 128 ||
        rom.height != 32) {
        return nullptr;
    }
    const SceneDefinition* scene =
        m_colorizationSettings->scene(static_cast<uint16_t>(frame.triggerId));
    return scene && (scene->options & kSceneAsBackground) != 0 ? scene : nullptr;
}

std::optional<std::size_t> ProjectDocument::backgroundSceneFrame(std::size_t frameIndex,
                                                                 uint32_t sceneElapsedMs) const
{
    const SceneDefinition* scene = backgroundScene(frameIndex);
    if (!scene) {
        return std::nullopt;
    }
    if (!m_sceneCacheValid || m_sceneRevision != m_revision) {
        m_sceneFrames.clear();
        m_upscaledSceneFrames.clear();
        for (std::size_t index = 0; index < m_frames.size(); ++index) {
            const std::optional<SceneFrameRef>& ref = m_frames[index].scene;
            if (ref && ref->group == 1) {
                m_sceneFrames.emplace(static_cast<uint32_t>(ref->sceneId) << 16 | ref->frameIndex,
                                      index);
            }
        }
        m_sceneRevision = m_revision;
        m_sceneCacheValid = true;
    }
    const uint32_t step = sceneElapsedMs / std::max<uint32_t>(scene->durationMs, 1) %
                          std::max<uint32_t>(scene->frameCount, 1);
    const auto found = m_sceneFrames.find(static_cast<uint32_t>(scene->sceneId) << 16 | step);
    if (found == m_sceneFrames.end()) {
        return std::nullopt;
    }
    return found->second;
}

const Rgb565Frame* ProjectDocument::backgroundPicture(std::size_t frameIndex,
                                                      Resolution resolution,
                                                      uint32_t sceneElapsedMs) const
{
    if (const std::optional<std::size_t> sceneFrame =
            backgroundSceneFrame(frameIndex, sceneElapsedMs)) {
        const FrameRecord& scene = m_frames[*sceneFrame];
        if (resolution == Resolution::Sd) {
            return &scene.colorizedSd;
        }
        if (scene.colorizedHd) {
            return &*scene.colorizedHd;
        }
        auto upscaled = m_upscaledSceneFrames.find(*sceneFrame);
        if (upscaled == m_upscaledSceneFrames.end()) {
            const bool lineDoubling = m_colorizationSettings &&
                                      m_colorizationSettings->scalingAlgorithm ==
                                          kScalingLineDoubling;
            upscaled = m_upscaledSceneFrames
                           .emplace(*sceneFrame,
                                    upscale2x(scene.colorizedSd, lineDoubling
                                                                     ? UpscaleFilter::Nearest
                                                                     : UpscaleFilter::Scale2x))
                           .first;
        }
        return &upscaled->second;
    }
    const std::optional<FrameBackgroundData>& data = requireFrame(m_frames, frameIndex)
                                                         .backgroundData;
    if (!data || data->backgroundId >= m_backgrounds.size()) {
        return nullptr;
    }
    const BackgroundRecord& background = m_backgrounds[data->backgroundId];
    if (resolution == Resolution::Hd) {
        return background.colorizedHd ? &*background.colorizedHd : nullptr;
    }
    return &background.colorizedSd;
}

bool ProjectDocument::hasBackgroundSource(std::size_t frameIndex) const
{
    const std::optional<FrameBackgroundData>& data = requireFrame(m_frames, frameIndex)
                                                         .backgroundData;
    return data && (data->backgroundId < m_backgrounds.size() ||
                    backgroundSceneFrame(frameIndex).has_value());
}

void ProjectDocument::setSpeakerLights(SpeakerLightSettings settings)
{
    for (std::size_t i = 0; i < settings.effects.size(); ++i) {
        if (settings.effects[i].id == 0) {
            throw std::invalid_argument("a light effect needs an ID");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (settings.effects[j].id == settings.effects[i].id) {
                throw std::invalid_argument("two light effects have the ID " +
                                            std::to_string(settings.effects[i].id));
            }
        }
    }
    const auto missing = [&settings](const LightActions& actions) {
        for (const LightAction& action : actions) {
            if (action.kind == LightActionKind::Effect && !settings.effect(action.effectId)) {
                return true;
            }
        }
        return false;
    };
    if (missing(settings.defaults)) {
        throw std::invalid_argument("a default names a light effect the project does not have");
    }
    for (const FrameRecord& frame : m_frames) {
        if (missing(frame.lights)) {
            throw std::invalid_argument("a frame starts a light effect that would be gone");
        }
    }
    if (settings == m_speakerLights) {
        return;
    }
    m_speakerLights = std::move(settings);
    markChanged();
}

void ProjectDocument::setLicense(ProjectLicense license)
{
    if (static_cast<uint8_t>(license.kind) > kLastLicenseKind) {
        throw std::invalid_argument("unknown license kind");
    }
    if (license == m_license) {
        return;
    }
    m_license = std::move(license);
    markChanged();
}

const LightActions& ProjectDocument::frameLights(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).lights;
}

void ProjectDocument::setFrameLights(std::size_t frameIndex, const LightActions& lights)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    for (const LightAction& action : lights) {
        if (action.kind == LightActionKind::Effect && !m_speakerLights.effect(action.effectId)) {
            throw std::invalid_argument("the project has no light effect " +
                                        std::to_string(action.effectId));
        }
    }
    if (frame.lights == lights) {
        return;
    }
    frame.lights = lights;
    markChanged();
}

uint32_t ProjectDocument::originalOnlyDynamicSets(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).originalOnlyDynamicSets;
}

void ProjectDocument::setOriginalOnlyDynamicSets(std::size_t frameIndex, uint32_t sets)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.originalOnlyDynamicSets == sets) {
        return;
    }
    frame.originalOnlyDynamicSets = sets;
    markChanged();
}

bool ProjectDocument::originalOnlyDynamicSetsApply(std::size_t frameIndex) const
{
    if (rendersHdPlaneOnly() ||
        editsOriginalPlaneOnly() || !hasHdFrame(frameIndex)) {
        return false;
    }
    const std::optional<FrameBackgroundData>& data = backgroundData(frameIndex);
    if (!data || data->backgroundId == kNoBackgroundId) {
        return true;
    }
    return data->backgroundId < m_backgrounds.size() &&
           m_backgrounds[data->backgroundId].colorizedHd.has_value();
}

uint32_t ProjectDocument::effectiveOriginalOnlyDynamicSets(std::size_t frameIndex) const
{
    return originalOnlyDynamicSetsApply(frameIndex) ? originalOnlyDynamicSets(frameIndex) : 0;
}

bool ProjectDocument::frameUsesLightEffect(std::size_t frameIndex, uint16_t effectId) const
{
    for (const LightAction& action : requireFrame(m_frames, frameIndex).lights) {
        if (action.kind == LightActionKind::Effect && action.effectId == effectId) {
            return true;
        }
    }
    return false;
}

std::vector<std::size_t> ProjectDocument::framesUsingLightEffect(uint16_t effectId) const
{
    std::vector<std::size_t> frames;
    for (std::size_t frameIndex = 0; frameIndex < m_frames.size(); ++frameIndex) {
        if (frameUsesLightEffect(frameIndex, effectId)) {
            frames.push_back(frameIndex);
        }
    }
    return frames;
}

const std::vector<std::string>& ProjectDocument::frameTags(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).tags;
}

void ProjectDocument::setFrameTags(std::size_t frameIndex, std::vector<std::string> tags)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    for (std::string& tag : tags) {
        const std::size_t first = tag.find_first_not_of(" \t");
        const std::size_t last = tag.find_last_not_of(" \t");
        tag = first == std::string::npos ? std::string() : tag.substr(first, last - first + 1);
    }
    tags.erase(std::remove(tags.begin(), tags.end(), std::string()), tags.end());
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    if (frame.tags == tags) {
        return;
    }
    frame.tags = std::move(tags);
    markChanged();
}

std::vector<std::string> ProjectDocument::allFrameTags() const
{
    std::vector<std::string> tags;
    for (const FrameRecord& frame : m_frames) {
        tags.insert(tags.end(), frame.tags.begin(), frame.tags.end());
    }
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

void ProjectDocument::setOriginalFrame(std::size_t frameIndex, IndexedFrame original)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (original.size.width != frame.originalSd.size.width ||
        original.size.height != frame.originalSd.size.height || !original.isValid()) {
        throw std::invalid_argument("the original has not the size of the ROM frame");
    }
    frame.originalSd = std::move(original);
    markChanged();
    m_identificationRevision = m_revision;
}

void ProjectDocument::markChanged()
{
    m_dirty = true;
    m_revision = nextRevision();
}

void ProjectDocument::markUnsaved()
{
    m_dirty = true;
}

void ProjectDocument::markClean()
{
    m_dirty = false;
}

bool ProjectDocument::hasHdFrame(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).colorizedHd.has_value();
}

void ProjectDocument::createHdFrame(std::size_t frameIndex, UpscaleFilter filter)
{
    requireEditableExtraPlane();
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.colorizedHd) {
        return;
    }
    frame.colorizedHd = rendersHdPlaneOnly() ? plainRgb565Frame(frame.originalSd, m_colorCount)
                                           : upscale2x(frame.colorizedSd, filter);
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

void ProjectDocument::upscaleAssignedBackgroundToHd(std::size_t frameIndex, UpscaleFilter filter)
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
    background.colorizedHd = upscale2x(background.colorizedSd, filter);
    background.inactiveHd.reset();
    markChanged();
}

FrameHdState ProjectDocument::frameHdState(std::size_t frameIndex, FrameHdParts parts) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    FrameHdState state;
    state.colorizedHd = frame.colorizedHd;
    state.inactiveHd = frame.inactiveHd;
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

void ProjectDocument::requireGameFrame(const FrameRecord& frame, const char* what)
{
    if (frame.scene) {
        throw std::logic_error(std::string("a frame of a scene takes no ") + what);
    }
}

bool ProjectDocument::stripSceneFrame(std::size_t frameIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!frame.scene) {
        return false;
    }
    bool changed = false;
    for (FrameSpriteReference& reference : frame.spriteReferences) {
        if (reference.spriteId != kNoSpriteId) {
            reference = FrameSpriteReference{};
            changed = true;
        }
    }
    if (frame.dynamicData) {
        for (IndexedFrame* mask : {&frame.dynamicData->maskSd, &frame.dynamicData->maskHd}) {
            const CompactArray<uint8_t>& pixels = mask->pixels;
            if (std::any_of(pixels.begin(), pixels.end(),
                            [](uint8_t value) { return value != 255; })) {
                std::fill(mask->pixels.begin(), mask->pixels.end(), uint8_t{255});
                changed = true;
            }
        }
        if (changed && frameIndex < m_dynamicPixelStates.size()) {
            m_dynamicPixelStates[frameIndex] = 1;
        }
    }
    if (frame.comparisonMaskId != kNoComparisonMaskId || frame.shapeComparisonMode != 0) {
        frame.comparisonMaskId = kNoComparisonMaskId;
        frame.shapeComparisonMode = 0;
        changed = true;
    }
    if (frame.triggerId != kNoTriggerId) {
        frame.triggerId = kNoTriggerId;
        changed = true;
    }
    if (changed) {
        markChanged();
        m_identificationRevision = m_revision;
    }
    return changed;
}

void ProjectDocument::setShapeComparisonMode(std::size_t frameIndex, uint8_t mode)
{
    if (mode > 1) {
        throw std::invalid_argument("shape comparison mode must be 0 or 1");
    }
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (mode != 0) {
        requireGameFrame(frame, "shape mode");
    }
    if (frame.shapeComparisonMode == mode) {
        return;
    }
    frame.shapeComparisonMode = mode;
    markChanged();
    m_identificationRevision = m_revision;
}

void ProjectDocument::restoreShapeComparisonMode(std::size_t frameIndex, uint8_t mode)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.shapeComparisonMode == mode) {
        return;
    }
    frame.shapeComparisonMode = mode;
    markChanged();
    m_identificationRevision = m_revision;
}

uint8_t ProjectDocument::comparisonMaskId(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).comparisonMaskId;
}

void ProjectDocument::setComparisonMaskId(std::size_t frameIndex, uint8_t maskId)
{
    if (maskId != kNoComparisonMaskId && maskId >= m_comparisonMasks.size()) {
        throw std::out_of_range("comparison mask " + std::to_string(maskId) +
                                " does not exist; the project has " +
                                std::to_string(m_comparisonMasks.size()));
    }
    if (maskId != kNoComparisonMaskId) {
        requireGameFrame(requireFrame(m_frames, frameIndex), "comparison mask");
    }
    restoreComparisonMaskId(frameIndex, maskId);
}

void ProjectDocument::restoreComparisonMaskId(std::size_t frameIndex, uint8_t maskId)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (frame.comparisonMaskId == maskId) {
        return;
    }
    frame.comparisonMaskId = maskId;
    markChanged();
    m_identificationRevision = m_revision;
}

uint32_t ProjectDocument::triggerId(std::size_t frameIndex) const
{
    return requireFrame(m_frames, frameIndex).triggerId;
}

void ProjectDocument::setTriggerId(std::size_t frameIndex, uint32_t triggerId)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (triggerId != kNoTriggerId) {
        requireGameFrame(frame, "trigger");
    }
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

const CompactArray<uint16_t>& ProjectDocument::colorRotations(std::size_t frameIndex,
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
    const CompactArray<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationOffset(slotIndex)];
}

uint16_t ProjectDocument::colorRotationDelayMs(std::size_t frameIndex,
                                               Resolution resolution,
                                               std::size_t slotIndex) const
{
    requireEditablePlane(resolution);
    const CompactArray<uint16_t>& rotations = colorRotations(frameIndex, resolution);
    return rotations[colorRotationOffset(slotIndex) + 1];
}

uint16_t ProjectDocument::colorRotationColor(std::size_t frameIndex,
                                             Resolution resolution,
                                             std::size_t slotIndex,
                                             std::size_t colorIndex) const
{
    requireEditablePlane(resolution);
    const CompactArray<uint16_t>& rotations = colorRotations(frameIndex, resolution);
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
    CompactArray<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex);
    if (std::as_const(rotations)[offset] == length) {
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
    CompactArray<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex) + 1;
    if (std::as_const(rotations)[offset] == delayMs) {
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
    CompactArray<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationColorOffset(slotIndex, colorIndex);
    if (std::as_const(rotations)[offset] == rgb565) {
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
    CompactArray<uint16_t>& rotations =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t offset = colorRotationOffset(slotIndex);
    const uint16_t* values = std::as_const(rotations).data() + offset;
    if (std::all_of(values, values + kMaxColorRotationLength,
                    [](uint16_t value) { return value == 0; })) {
        return;
    }
    uint16_t* begin = rotations.data() + offset;
    std::fill(begin, begin + kMaxColorRotationLength, uint16_t{0});
    markChanged();
}

void ProjectDocument::restoreColorRotations(std::size_t frameIndex,
                                            Resolution resolution,
                                            CompactArray<uint16_t> rotations)
{
    requireEditablePlane(resolution);
    CompactArray<uint16_t>& current =
        requireColorRotations(requireFrame(m_frames, frameIndex), resolution);
    if (rotations.size() != current.size()) {
        throw std::invalid_argument("color rotations do not fit the frame");
    }
    if (rotations == current) {
        return;
    }
    current = std::move(rotations);
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
    if (std::as_const(mask.pixels)[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    markChanged();
    m_identificationRevision = m_revision;
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

const CompactArray<uint16_t>& requireDynamicColors(const FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->colorsHd : frame.dynamicData->colorsSd;
}

CompactArray<uint16_t>& requireDynamicColors(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->colorsHd : frame.dynamicData->colorsSd;
}

const CompactArray<uint8_t>& requireDynamicShadowDirections(const FrameRecord& frame,
                                                            Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow direction data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowDirectionsHd
                                        : frame.dynamicData->shadowDirectionsSd;
}

CompactArray<uint8_t>& requireDynamicShadowDirections(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow direction data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowDirectionsHd
                                        : frame.dynamicData->shadowDirectionsSd;
}

const CompactArray<uint16_t>& requireDynamicShadowColors(const FrameRecord& frame,
                                                        Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowColorsHd
                                        : frame.dynamicData->shadowColorsSd;
}

CompactArray<uint16_t>& requireDynamicShadowColors(FrameRecord& frame, Resolution resolution)
{
    if (!frame.dynamicData) {
        throw std::logic_error("dynamic shadow color data is not available");
    }
    return resolution == Resolution::Hd ? frame.dynamicData->shadowColorsHd
                                        : frame.dynamicData->shadowColorsSd;
}

template <typename T>
void ensureDynamicSetVectorSize(CompactArray<T>& values, T fill)
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

std::size_t checkedDynamicColorIndex(const CompactArray<uint16_t>& colors,
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
    requireDynamicPlane(resolution);
    if (value != 255) {
        requireGameFrame(requireFrame(m_frames, frameIndex), "dynamic zone");
    }
    IndexedFrame& mask = requireDynamicMask(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t pixelIndex = checkedIndexedPixelIndex(mask, x, y);
    if (std::as_const(mask.pixels)[pixelIndex] == value) {
        return;
    }
    mask.pixels[pixelIndex] = value;
    if (frameIndex < m_dynamicPixelStates.size()) {
        // A new dynamic pixel decides it; a removed one may have been the last.
        m_dynamicPixelStates[frameIndex] = value != 255 ? 2 : 0;
    }
    markChanged();
}

bool ProjectDocument::hasDynamicPixels(std::size_t frameIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (m_dynamicPixelStates.size() != m_frames.size()) {
        m_dynamicPixelStates.assign(m_frames.size(), 0);
    }
    uint8_t& state = m_dynamicPixelStates[frameIndex];
    if (state == 0) {
        bool found = false;
        if (frame.dynamicData) {
            const IndexedFrame& mask = dynamicPlane() == Resolution::Hd ? frame.dynamicData->maskHd
                                                                        : frame.dynamicData->maskSd;
            found = std::any_of(mask.pixels.begin(), mask.pixels.end(),
                                [](uint8_t value) { return value != 255; });
        }
        state = found ? 2 : 1;
    }
    return state == 2;
}

uint16_t ProjectDocument::dynamicColor(std::size_t frameIndex,
                                       Resolution resolution,
                                       std::size_t setIndex,
                                       std::size_t originalColorIndex) const
{
    requireEditablePlane(resolution);
    const CompactArray<uint16_t>& colors =
        requireDynamicColors(requireFrame(m_frames, frameIndex), resolution);
    return colors[checkedDynamicColorIndex(colors, setIndex, originalColorIndex)];
}

void ProjectDocument::setDynamicColor(std::size_t frameIndex,
                                      Resolution resolution,
                                      std::size_t setIndex,
                                      std::size_t originalColorIndex,
                                      uint16_t rgb565)
{
    requireDynamicPlane(resolution);
    CompactArray<uint16_t>& colors =
        requireDynamicColors(requireFrame(m_frames, frameIndex), resolution);
    const std::size_t colorIndex =
        checkedDynamicColorIndex(colors, setIndex, originalColorIndex);
    if (std::as_const(colors)[colorIndex] == rgb565) {
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
    const CompactArray<uint8_t>& directions =
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
    const CompactArray<uint16_t>& colors =
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
    requireDynamicPlane(resolution);
    requireDynamicSetIndex(setIndex);
    CompactArray<uint8_t>& directions =
        requireDynamicShadowDirections(requireFrame(m_frames, frameIndex), resolution);
    ensureDynamicSetVectorSize<uint8_t>(directions, 0);
    if (std::as_const(directions)[setIndex] == direction) {
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
    requireDynamicPlane(resolution);
    requireDynamicSetIndex(setIndex);
    CompactArray<uint16_t>& colors =
        requireDynamicShadowColors(requireFrame(m_frames, frameIndex), resolution);
    ensureDynamicSetVectorSize<uint16_t>(colors, 0);
    if (std::as_const(colors)[setIndex] == rgb565) {
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
    if (std::as_const(frame.pixels)[pixelIndex] == rgb565) {
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
    if (backgroundId != kNoBackgroundId && backgroundId >= m_backgrounds.size()) {
        throw std::out_of_range("background " + std::to_string(backgroundId) +
                                " does not exist; the project has " +
                                std::to_string(m_backgrounds.size()));
    }
    restoreBackgroundId(frameIndex, backgroundId);
}

void ProjectDocument::restoreBackgroundId(std::size_t frameIndex, uint16_t backgroundId)
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
    if (std::as_const(mask.pixels)[pixelIndex] == value) {
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

void ProjectDocument::createHdSprite(std::size_t spriteIndex, UpscaleFilter filter)
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
        sprite.colorizedHd = upscaleSpriteRgb565Frame2x(sprite.colorizedSd, filter);
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

void ProjectDocument::restoreSprite(std::size_t spriteIndex, SpriteRecord sprite)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    const SpriteRecord& current = m_sprites[spriteIndex];
    if (sprite.originalSd.size.width != current.originalSd.size.width ||
        sprite.originalSd.size.height != current.originalSd.size.height ||
        sprite.detectionAreas.size() != current.detectionAreas.size()) {
        throw std::invalid_argument("sprite record does not fit the project");
    }
    // The sprite stays the same component.
    sprite.meta = current.meta;
    m_sprites[spriteIndex] = std::move(sprite);
    markChanged();
}

uint32_t ProjectDocument::spriteSourceFrame(std::size_t spriteIndex) const
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    return m_editorMetadata.spriteSourceFrames[spriteIndex];
}

void ProjectDocument::setSpriteSourceFrame(std::size_t spriteIndex, uint32_t frameIndex)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    uint32_t& source = m_editorMetadata.spriteSourceFrames[spriteIndex];
    if (source == frameIndex) {
        return;
    }
    source = frameIndex;
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
    sprite.colorizedSd.pixels.assign(sprite.colorizedSd.pixels.size(), uint16_t{0});
    sprite.dynamicMaskSd.pixels.assign(sprite.dynamicMaskSd.pixels.size(), uint8_t{255});
    sprite.dynamicColorsSd.assign(sprite.dynamicColorsSd.size(), uint16_t{0});
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

void ProjectDocument::replaceSpritePlane(std::size_t spriteIndex,
                                         Resolution plane,
                                         IndexedFrame shades,
                                         Rgb565Frame colors)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    const Size spriteSize{static_cast<int>(kMaxSpriteWidth), static_cast<int>(kMaxSpriteHeight)};
    if (!hasSize(shades.size, spriteSize) || !hasSize(colors.size, spriteSize) ||
        !shades.isValid() || !colors.isValid()) {
        throw std::invalid_argument("sprite pictures have the size of a sprite");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    if (rendersHdPlaneOnly()) {
        if (plane != Resolution::Hd) {
            throw std::logic_error("the sprite has its picture on the native plane only");
        }
        replaceCapturedHdOnlySprite(sprite, std::move(shades), std::move(colors));
        return;
    }
    if (plane == Resolution::Hd) {
        requireEditableExtraPlane();
        if (!sprite.hasHd) {
            throw std::logic_error("the sprite has no HD data");
        }
        if (sprite.maskHd.pixels == shades.pixels && sprite.colorizedHd.pixels == colors.pixels) {
            return;
        }
        sprite.maskHd = std::move(shades);
        sprite.colorizedHd = std::move(colors);
        markChanged();
        return;
    }
    if (sprite.originalSd.pixels == shades.pixels && sprite.colorizedSd.pixels == colors.pixels) {
        return;
    }
    sprite.originalSd = std::move(shades);
    sprite.colorizedSd = std::move(colors);
    if (sprite.hasHd) {
        sprite.maskHd = upscaleSpriteIndexedFrame2x(sprite.originalSd);
        sprite.colorizedHd = upscaleSpriteRgb565Frame2x(sprite.colorizedSd);
    }
    recalculateSpriteDetectionDwords(sprite);
    markChanged();
}

void ProjectDocument::setSpriteShapeMode(std::size_t spriteIndex, bool shapeMode)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    const uint8_t value = shapeMode ? 1 : 0;
    if (m_sprites[spriteIndex].shapeMode == value) {
        return;
    }
    m_sprites[spriteIndex].shapeMode = value;
    markChanged();
}

std::size_t ProjectDocument::setSpriteDynamicMaskPixels(std::size_t spriteIndex,
                                                        const std::vector<render::Point>& points,
                                                        uint8_t value)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    if (value != 255 && value >= kMaxDynamicSetsPerSprite) {
        throw std::out_of_range("sprite dynamic set out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    const bool extra = dynamicPlane() == Resolution::Hd;
    const IndexedFrame& opacity = extra ? sprite.maskHd : sprite.originalSd;
    IndexedFrame& mask = extra ? sprite.dynamicMaskHd : sprite.dynamicMaskSd;
    std::size_t changed = 0;
    for (const render::Point& point : points) {
        if (point.x < 0 || point.y < 0 || point.x >= static_cast<int>(kMaxSpriteWidth) ||
            point.y >= static_cast<int>(kMaxSpriteHeight)) {
            continue;
        }
        const std::size_t index = spritePixelIndex(point.x, point.y);
        if (opacity.pixels[index] == 255 || std::as_const(mask.pixels)[index] == value) {
            continue;
        }
        mask.pixels[index] = value;
        ++changed;
    }
    if (changed > 0) {
        markChanged();
    }
    return changed;
}

uint16_t ProjectDocument::spriteDynamicColor(std::size_t spriteIndex,
                                             std::size_t setIndex,
                                             std::size_t originalColorIndex) const
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    if (setIndex >= kMaxDynamicSetsPerSprite || originalColorIndex >= m_colorCount) {
        throw std::out_of_range("sprite dynamic color out of range");
    }
    const SpriteRecord& sprite = m_sprites[spriteIndex];
    const CompactArray<uint16_t>& colors =
        dynamicPlane() == Resolution::Hd ? sprite.dynamicColorsHd : sprite.dynamicColorsSd;
    const std::size_t index = setIndex * m_colorCount + originalColorIndex;
    return index < colors.size() ? colors[index] : uint16_t{0};
}

void ProjectDocument::setSpriteDynamicColor(std::size_t spriteIndex,
                                            std::size_t setIndex,
                                            std::size_t originalColorIndex,
                                            uint16_t rgb565)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    if (setIndex >= kMaxDynamicSetsPerSprite || originalColorIndex >= m_colorCount) {
        throw std::out_of_range("sprite dynamic color out of range");
    }
    SpriteRecord& sprite = m_sprites[spriteIndex];
    CompactArray<uint16_t>& colors =
        dynamicPlane() == Resolution::Hd ? sprite.dynamicColorsHd : sprite.dynamicColorsSd;
    const std::size_t index = setIndex * m_colorCount + originalColorIndex;
    if (index >= colors.size() || std::as_const(colors)[index] == rgb565) {
        return;
    }
    colors[index] = rgb565;
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
    if (std::as_const(sprite.originalSd.pixels)[pixelIndex] == value) {
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
    if (std::as_const(frame.pixels)[pixelIndex] == rgb565) {
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

namespace {

// The first empty slot of a sprite list; the list's size when it is full.
std::size_t firstEmptySpriteSlot(const std::vector<FrameSpriteReference>& references)
{
    std::size_t slot = 0;
    while (slot < references.size() && references[slot].spriteId != kNoSpriteId) {
        ++slot;
    }
    return slot;
}

}  // namespace

void ProjectDocument::setFrameSpriteReference(std::size_t frameIndex,
                                              std::size_t slotIndex,
                                              FrameSpriteReference reference)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (slotIndex >= kSpritesPerFrame ||
        (!frame.spriteReferences.empty() &&
         frame.spriteReferences.size() != kSpritesPerFrame)) {
        throw std::out_of_range("sprite slot index out of range");
    }
    if (reference.spriteId != kNoSpriteId && reference.spriteId >= m_sprites.size()) {
        throw std::out_of_range("sprite reference index out of range");
    }
    if (reference.spriteId != kNoSpriteId) {
        requireGameFrame(frame, "sprite");
    }
    const FrameSpriteReference current = frame.spriteReferences.empty()
                                             ? FrameSpriteReference{}
                                             : frame.spriteReferences[slotIndex];
    if (current.spriteId != kNoSpriteId && reference.spriteId == kNoSpriteId) {
        clearFrameSpriteReference(frameIndex, slotIndex);
        return;
    }
    if (current.spriteId == kNoSpriteId && reference.spriteId != kNoSpriteId) {
        const std::size_t firstEmpty = frame.spriteReferences.empty()
                                           ? 0
                                           : firstEmptySpriteSlot(frame.spriteReferences);
        if (slotIndex > firstEmpty) {
            throw std::invalid_argument(
                "sprite slot " + std::to_string(slotIndex) + " cannot be used while slot " +
                std::to_string(firstEmpty) +
                " is empty: the runtime stops looking for sprites at the first empty slot");
        }
    }
    if (current.spriteId == reference.spriteId && current.minX == reference.minX &&
        current.minY == reference.minY && current.maxX == reference.maxX &&
        current.maxY == reference.maxY) {
        return;
    }
    if (frame.spriteReferences.empty()) {
        frame.spriteReferences.assign(kSpritesPerFrame, FrameSpriteReference{});
    }
    frame.spriteReferences[slotIndex] = reference;
    markChanged();
}

void ProjectDocument::clearFrameSpriteReference(std::size_t frameIndex, std::size_t slotIndex)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    std::vector<FrameSpriteReference>& references = frame.spriteReferences;
    if (slotIndex >= kSpritesPerFrame ||
        (!references.empty() && references.size() != kSpritesPerFrame)) {
        throw std::out_of_range("sprite slot index out of range");
    }
    if (references.empty() || references[slotIndex].spriteId == kNoSpriteId) {
        return;
    }
    // IDC_DELSPRITE2 of the Win32 editor: the slots behind move up with their bounds, and the
    // last slot becomes empty and keeps its bounds.
    for (std::size_t slot = slotIndex; slot + 1 < references.size(); ++slot) {
        references[slot] = references[slot + 1];
    }
    references.back().spriteId = kNoSpriteId;
    markChanged();
}

void ProjectDocument::restoreFrameSpriteReferences(std::size_t frameIndex,
                                                   std::vector<FrameSpriteReference> references)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (!references.empty() && references.size() != kSpritesPerFrame) {
        throw std::invalid_argument("sprite slots do not fit the frame");
    }
    for (const FrameSpriteReference& reference : references) {
        if (reference.spriteId != kNoSpriteId && reference.spriteId >= m_sprites.size()) {
            throw std::out_of_range("sprite reference index out of range");
        }
    }
    frame.spriteReferences = std::move(references);
    markChanged();
}

std::size_t ProjectDocument::spriteSlotForAssignment(std::size_t frameIndex,
                                                     std::size_t slotIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    if (slotIndex >= kSpritesPerFrame) {
        throw std::out_of_range("sprite slot index out of range");
    }
    if (frame.spriteReferences.empty()) {
        return 0;
    }
    if (frame.spriteReferences.at(slotIndex).spriteId != kNoSpriteId) {
        return slotIndex;
    }
    return std::min(slotIndex, firstEmptySpriteSlot(frame.spriteReferences));
}

bool ProjectDocument::assignmentPassesSpriteWarning(std::size_t frameIndex,
                                                    std::size_t slotIndex) const
{
    const std::size_t slot = spriteSlotForAssignment(frameIndex, slotIndex);
    const std::vector<FrameSpriteReference>& references =
        requireFrame(m_frames, frameIndex).spriteReferences;
    // Slots are filled from the first on, so a new sprite in slot 32 is the 33rd.
    return slot == kSpritesPerFrameWarning && slot < references.size() &&
           references[slot].spriteId == kNoSpriteId;
}

std::optional<std::size_t> ProjectDocument::firstSpriteSlotGap(std::size_t frameIndex) const
{
    const std::vector<FrameSpriteReference>& references =
        requireFrame(m_frames, frameIndex).spriteReferences;
    const std::size_t firstEmpty = firstEmptySpriteSlot(references);
    for (std::size_t slot = firstEmpty; slot < references.size(); ++slot) {
        if (references[slot].spriteId != kNoSpriteId) {
            return firstEmpty;
        }
    }
    return std::nullopt;
}

FrameReferenceProblems ProjectDocument::frameReferenceProblems(std::size_t frameIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    FrameReferenceProblems problems;
    problems.comparisonMask = frame.comparisonMaskId != kNoComparisonMaskId &&
                              frame.comparisonMaskId >= m_comparisonMasks.size();
    problems.background = frame.backgroundData &&
                          frame.backgroundData->backgroundId != kNoBackgroundId &&
                          frame.backgroundData->backgroundId >= m_backgrounds.size();
    for (const FrameSpriteReference& reference : frame.spriteReferences) {
        if (reference.spriteId != kNoSpriteId && reference.spriteId >= m_sprites.size()) {
            problems.sprite = true;
        }
    }
    problems.spriteSlotGap = firstSpriteSlotGap(frameIndex);
    return problems;
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
    if (std::as_const(frame.pixels)[pixelIndex] == rgb565) {
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

namespace {

// Moves the entries behind entryIndex of a table of fixed-size entries down by one and
// clears the last of the first entryCount entries.
template <typename T>
void removeTableEntry(std::vector<T>& table,
                      std::size_t entryIndex,
                      std::size_t entryCount,
                      std::size_t entrySize)
{
    if (entryIndex >= entryCount || entryCount * entrySize > table.size()) {
        return;
    }
    const auto begin = table.begin();
    std::move(begin + static_cast<std::ptrdiff_t>((entryIndex + 1) * entrySize),
              begin + static_cast<std::ptrdiff_t>(entryCount * entrySize),
              begin + static_cast<std::ptrdiff_t>(entryIndex * entrySize));
    std::fill(begin + static_cast<std::ptrdiff_t>((entryCount - 1) * entrySize),
              begin + static_cast<std::ptrdiff_t>(entryCount * entrySize),
              T{});
}

std::vector<std::size_t> sortedUnique(std::vector<std::size_t> values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

bool isAscending(const std::vector<std::size_t>& values)
{
    return std::adjacent_find(values.begin(), values.end(),
                              [](std::size_t a, std::size_t b) { return a >= b; }) ==
           values.end();
}

}  // namespace

RecordTables ProjectDocument::recordTables() const
{
    RecordTables tables;
    tables.maskNames = m_editorMetadata.maskNames;
    tables.sectionCount = m_editorMetadata.sectionCount;
    tables.sectionFirstFrames = m_editorMetadata.sectionFirstFrames;
    tables.sectionNames = m_editorMetadata.sectionNames;
    tables.spriteNames = m_editorMetadata.spriteNames;
    tables.spriteSourceFrames = m_editorMetadata.spriteSourceFrames;
    tables.spriteSourceRects = m_editorMetadata.spriteSourceRects;
    tables.spriteSourceMirrors = m_editorMetadata.spriteSourceMirrors;
    return tables;
}

void ProjectDocument::requireFittingRecordTables(const RecordTables& tables) const
{
    if (tables.maskNames.size() != m_editorMetadata.maskNames.size() ||
        tables.sectionFirstFrames.size() != m_editorMetadata.sectionFirstFrames.size() ||
        tables.sectionNames.size() != m_editorMetadata.sectionNames.size() ||
        // The sprite tables grow with the sprites: any size, all four alike.
        tables.spriteSourceFrames.size() < kCrpSpriteEntries ||
        tables.spriteNames.size() != tables.spriteSourceFrames.size() * kSpriteNameBytes ||
        tables.spriteSourceRects.size() != tables.spriteSourceFrames.size() * 4 ||
        tables.spriteSourceMirrors.size() != tables.spriteSourceFrames.size() * 2 ||
        tables.sectionCount > kMaxSections) {
        throw std::invalid_argument("record tables have invalid sizes");
    }
}

void ProjectDocument::restoreRecordTables(RecordTables tables)
{
    requireFittingRecordTables(tables);
    m_editorMetadata.maskNames = std::move(tables.maskNames);
    m_editorMetadata.sectionCount = tables.sectionCount;
    m_editorMetadata.sectionFirstFrames = std::move(tables.sectionFirstFrames);
    m_editorMetadata.sectionNames = std::move(tables.sectionNames);
    m_editorMetadata.spriteNames = std::move(tables.spriteNames);
    m_editorMetadata.spriteSourceFrames = std::move(tables.spriteSourceFrames);
    m_editorMetadata.spriteSourceRects = std::move(tables.spriteSourceRects);
    m_editorMetadata.spriteSourceMirrors = std::move(tables.spriteSourceMirrors);
    ensureSpriteTableEntries(m_editorMetadata, m_sprites.size());
    markChanged();
}

RemovedFrames ProjectDocument::removeFrames(const std::vector<std::size_t>& frameIndices)
{
    RemovedFrames removed;
    removed.indices = sortedUnique(frameIndices);
    if (removed.indices.empty()) {
        return removed;
    }
    const std::size_t oldCount = m_frames.size();
    if (removed.indices.back() >= oldCount) {
        throw std::out_of_range("frame index out of range");
    }
    if (removed.indices.size() >= oldCount) {
        throw std::logic_error("at least one frame has to remain");
    }
    removed.tables = recordTables();

    const std::vector<std::size_t>& gone = removed.indices;
    const auto isGone = [&gone](std::size_t frameIndex) {
        return std::binary_search(gone.begin(), gone.end(), frameIndex);
    };
    // The new number of a remaining frame, and of the place a removed frame left.
    const auto goneBefore = [&gone](std::size_t frameIndex) {
        return static_cast<std::size_t>(
            std::lower_bound(gone.begin(), gone.end(), frameIndex) - gone.begin());
    };
    const auto nextRemaining = [&](std::size_t frameIndex) {
        while (frameIndex < oldCount && isGone(frameIndex)) {
            ++frameIndex;
        }
        return frameIndex;
    };

    removed.frames.reserve(gone.size());
    std::size_t next = 0;
    std::size_t write = gone.front();
    for (std::size_t read = gone.front(); read < oldCount; ++read) {
        if (next < gone.size() && gone[next] == read) {
            removed.frames.push_back(std::move(m_frames[read]));
            ++next;
        } else {
            m_frames[write++] = std::move(m_frames[read]);
        }
    }
    m_frames.resize(write);
    m_dynamicPixelStates.clear();

    std::vector<uint8_t>& palettes = m_editorMetadata.importedPalettes;
    const std::size_t paletteCount = palettes.size() / kImportedPaletteBytes;
    if (gone.front() < paletteCount) {
        std::vector<uint8_t> kept;
        kept.reserve(palettes.size());
        for (std::size_t frameIndex = 0; frameIndex < paletteCount; ++frameIndex) {
            const auto begin =
                palettes.begin() + static_cast<std::ptrdiff_t>(frameIndex * kImportedPaletteBytes);
            const auto end = begin + static_cast<std::ptrdiff_t>(kImportedPaletteBytes);
            std::vector<uint8_t>& target = isGone(frameIndex) ? removed.importedPalettes : kept;
            target.insert(target.end(), begin, end);
        }
        palettes = std::move(kept);
    }

    EditorMetadata& metadata = m_editorMetadata;
    const std::size_t sectionCount = std::min<std::size_t>(metadata.sectionCount, kMaxSections);
    std::vector<uint32_t> firstFrames;
    std::vector<uint8_t> names;
    for (std::size_t section = 0; section < sectionCount; ++section) {
        const std::size_t first = metadata.sectionFirstFrames[section];
        std::size_t newFirst = first - goneBefore(std::min(first, oldCount));
        if (first < oldCount && isGone(first)) {
            const std::size_t target = nextRemaining(first);
            bool otherSectionStartsBetween = false;
            for (std::size_t other = 0; other < sectionCount; ++other) {
                const std::size_t otherFirst = metadata.sectionFirstFrames[other];
                otherSectionStartsBetween = otherSectionStartsBetween ||
                                            (otherFirst > first && otherFirst <= target);
            }
            if (target >= oldCount || otherSectionStartsBetween) {
                continue;
            }
            newFirst = target - goneBefore(target);
        }
        firstFrames.push_back(static_cast<uint32_t>(newFirst));
        const auto nameBegin = metadata.sectionNames.begin() +
                               static_cast<std::ptrdiff_t>(section * kSectionNameBytes);
        names.insert(names.end(), nameBegin,
                     nameBegin + static_cast<std::ptrdiff_t>(kSectionNameBytes));
    }
    std::fill_n(metadata.sectionFirstFrames.begin(), sectionCount, uint32_t{0});
    std::fill_n(metadata.sectionNames.begin(), sectionCount * kSectionNameBytes, uint8_t{0});
    std::copy(firstFrames.begin(), firstFrames.end(), metadata.sectionFirstFrames.begin());
    std::copy(names.begin(), names.end(), metadata.sectionNames.begin());
    metadata.sectionCount = static_cast<uint32_t>(firstFrames.size());

    for (std::size_t sprite = 0; sprite < m_sprites.size(); ++sprite) {
        const std::size_t source = metadata.spriteSourceFrames[sprite];
        if (source < oldCount && isGone(source)) {
            metadata.spriteSourceRects[sprite * 4] = kNoSpriteSourceRect;
        }
        metadata.spriteSourceFrames[sprite] =
            static_cast<uint32_t>(source - goneBefore(std::min(source, oldCount)));
    }

    markChanged();
    m_identificationRevision = m_revision;
    return removed;
}

void ProjectDocument::reorderFrames(const std::vector<std::size_t>& newOrder)
{
    const std::size_t count = m_frames.size();
    std::vector<std::size_t> newIndex(count, count);
    if (newOrder.size() != count) {
        throw std::invalid_argument("the new frame order does not name every frame");
    }
    for (std::size_t position = 0; position < count; ++position) {
        if (newOrder[position] >= count || newIndex[newOrder[position]] != count) {
            throw std::invalid_argument("the new frame order does not name every frame once");
        }
        newIndex[newOrder[position]] = position;
    }

    std::vector<uint8_t>& palettes = m_editorMetadata.importedPalettes;
    const std::size_t paletteCount = std::min(palettes.size() / kImportedPaletteBytes, count);
    for (std::size_t position = 0; position < paletteCount; ++position) {
        if (newOrder[position] >= paletteCount) {
            throw std::logic_error(
                "the frames with an imported palette have to stay the first frames");
        }
    }

    std::vector<FrameRecord> frames;
    frames.reserve(count);
    for (std::size_t oldIndex : newOrder) {
        frames.push_back(std::move(m_frames[oldIndex]));
    }
    m_frames = std::move(frames);
    m_dynamicPixelStates.clear();

    if (paletteCount > 0) {
        std::vector<uint8_t> reordered(palettes.begin(),
                                       palettes.begin() + static_cast<std::ptrdiff_t>(
                                                              paletteCount * kImportedPaletteBytes));
        for (std::size_t position = 0; position < paletteCount; ++position) {
            std::copy_n(palettes.begin() +
                            static_cast<std::ptrdiff_t>(newOrder[position] * kImportedPaletteBytes),
                        kImportedPaletteBytes,
                        reordered.begin() +
                            static_cast<std::ptrdiff_t>(position * kImportedPaletteBytes));
        }
        std::copy(reordered.begin(), reordered.end(), palettes.begin());
    }

    EditorMetadata& metadata = m_editorMetadata;
    const std::size_t sectionCount = std::min<std::size_t>(metadata.sectionCount, kMaxSections);
    for (std::size_t section = 0; section < sectionCount; ++section) {
        uint32_t& first = metadata.sectionFirstFrames[section];
        if (first < count) {
            first = static_cast<uint32_t>(newIndex[first]);
        }
    }
    for (std::size_t sprite = 0; sprite < m_sprites.size(); ++sprite) {
        uint32_t& source = metadata.spriteSourceFrames[sprite];
        if (source < count) {
            source = static_cast<uint32_t>(newIndex[source]);
        }
    }

    markChanged();
    m_identificationRevision = m_revision;
}

FrameRecord ProjectDocument::frameRecordFrom(ProjectFrameSnapshot frame)
{
    return frameRecord(std::move(frame));
}

ProjectFrameSnapshot ProjectDocument::frameSnapshotOf(std::size_t frameIndex) const
{
    return frameSnapshot(requireFrame(m_frames, frameIndex));
}

void ProjectDocument::replaceFrame(std::size_t frameIndex, FrameRecord record)
{
    FrameRecord& frame = requireFrame(m_frames, frameIndex);
    requireFittingFrameRecord(record, frameIndex, m_geometry, m_colorCount, kSpritesPerFrame);
    record.meta = frame.meta;
    frame = std::move(record);
    m_dynamicPixelStates.clear();
    markChanged();
    m_identificationRevision = m_revision;
}

void ProjectDocument::replaceComparisonMask(std::size_t maskIndex, IndexedFrame mask)
{
    if (maskIndex >= m_comparisonMasks.size()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    if (!mask.isValid() || !hasSize(mask.size, originalFrameSize())) {
        throw std::invalid_argument("comparison mask does not fit the project");
    }
    m_comparisonMasks[maskIndex] = std::move(mask);
    markChanged();
    m_identificationRevision = m_revision;
}

void ProjectDocument::replaceBackground(std::size_t backgroundIndex, BackgroundRecord background)
{
    if (backgroundIndex >= m_backgrounds.size()) {
        throw std::out_of_range("background index out of range");
    }
    if (!background.colorizedSd.isValid() ||
        !hasSize(background.colorizedSd.size, m_geometry.planeSize(Resolution::Sd)) ||
        (background.colorizedHd &&
         (editsOriginalPlaneOnly() || !background.colorizedHd->isValid() ||
          !hasSize(background.colorizedHd->size, m_geometry.planeSize(Resolution::Hd)))) ||
        background.inactiveHd) {
        throw std::invalid_argument("background record does not fit the project");
    }
    background.meta = m_backgrounds[backgroundIndex].meta;
    m_backgrounds[backgroundIndex] = std::move(background);
    markChanged();
}

void ProjectDocument::restoreState(const ProjectDocument& state)
{
    *this = state;
    markChanged();
    m_identificationRevision = m_revision;
}

void ProjectDocument::insertFrames(RemovedFrames removed)
{
    const std::size_t insertCount = removed.indices.size();
    if (insertCount == 0) {
        return;
    }
    const std::size_t total = m_frames.size() + insertCount;
    const std::size_t paletteInsertCount = removed.importedPalettes.size() / kImportedPaletteBytes;
    if (removed.frames.size() != insertCount || !isAscending(removed.indices) ||
        removed.indices.back() >= total ||
        removed.importedPalettes.size() % kImportedPaletteBytes != 0 ||
        paletteInsertCount > insertCount) {
        throw std::invalid_argument("removed frames do not fit the document");
    }
    for (std::size_t position = 0; position < insertCount; ++position) {
        requireFittingFrameRecord(removed.frames[position], removed.indices[position], m_geometry,
                                  m_colorCount, kSpritesPerFrame);
    }
    requireFittingRecordTables(removed.tables);

    std::vector<FrameRecord> merged;
    merged.reserve(total);
    std::size_t next = 0;
    std::size_t kept = 0;
    for (std::size_t frameIndex = 0; frameIndex < total; ++frameIndex) {
        if (next < insertCount && removed.indices[next] == frameIndex) {
            merged.push_back(std::move(removed.frames[next++]));
        } else {
            merged.push_back(std::move(m_frames[kept++]));
        }
    }
    m_frames = std::move(merged);
    m_dynamicPixelStates.clear();
    // The frames that were there keep their IDs; an inserted one that has none or one of them
    // gets a new one.
    std::vector<ComponentMeta*> metas;
    metas.reserve(total);
    for (std::size_t frameIndex = 0, position = 0; frameIndex < total; ++frameIndex) {
        if (position < insertCount && removed.indices[position] == frameIndex) {
            ++position;
        } else {
            metas.push_back(&m_frames[frameIndex].meta);
        }
    }
    for (std::size_t frameIndex : removed.indices) {
        metas.push_back(&m_frames[frameIndex].meta);
    }
    giveUniqueIds(metas);

    if (paletteInsertCount > 0) {
        const std::vector<uint8_t>& current = m_editorMetadata.importedPalettes;
        const std::size_t paletteTotal =
            current.size() / kImportedPaletteBytes + paletteInsertCount;
        std::vector<uint8_t> palettes;
        palettes.reserve(paletteTotal * kImportedPaletteBytes);
        std::size_t nextInserted = 0;
        std::size_t nextKept = 0;
        for (std::size_t frameIndex = 0; frameIndex < paletteTotal; ++frameIndex) {
            const bool inserted = nextInserted < paletteInsertCount &&
                                  removed.indices[nextInserted] == frameIndex;
            const std::vector<uint8_t>& source = inserted ? removed.importedPalettes : current;
            std::size_t& position = inserted ? nextInserted : nextKept;
            const auto begin =
                source.begin() + static_cast<std::ptrdiff_t>(position * kImportedPaletteBytes);
            palettes.insert(palettes.end(), begin,
                            begin + static_cast<std::ptrdiff_t>(kImportedPaletteBytes));
            ++position;
        }
        m_editorMetadata.importedPalettes = std::move(palettes);
    }

    restoreRecordTables(std::move(removed.tables));
    m_identificationRevision = m_revision;
}

FrameRecord ProjectDocument::makeUncolorizedFrame(IndexedFrame original,
                                                  uint32_t durationMs) const
{
    for (uint8_t shade : original.pixels) {
        if (shade >= m_colorCount) {
            throw std::invalid_argument("the original has a shade the project has no color for");
        }
    }
    FrameRecord frame = frameRecord(
        makeUncolorizedFrameSnapshot(m_geometry, m_colorCount, std::move(original), durationMs));
    if (m_geometry.discardsExtraPlane()) {
        dropDiscardedPlane(frame);
    }
    return frame;
}

SpriteRecord ProjectDocument::makeBlankSprite(std::string name) const
{
    const Size spriteSize{static_cast<int>(kMaxSpriteWidth), static_cast<int>(kMaxSpriteHeight)};
    SpriteRecord sprite;
    sprite.originalSd = makeIndexedFrame(spriteSize, 255);
    sprite.colorizedSd = makeRgb565Frame(spriteSize, 0);
    sprite.detectionDwords.assign(kMaxSpriteDetectAreas, 0xffffffff);
    sprite.detectionDwordPositions.assign(kMaxSpriteDetectAreas, 0xffff);
    sprite.detectionAreas.assign(kMaxSpriteDetectAreas, SpriteDetectionArea{});
    sprite.name = std::move(name);
    if (editsOriginalPlaneOnly()) {
        // No extra-plane data is held, see fromSnapshot().
        sprite.dynamicMaskSd = makeIndexedFrame(spriteSize, 255);
        sprite.dynamicColorsSd.assign(kMaxDynamicSetsPerSprite * m_colorCount, 0);
        return sprite;
    }
    sprite.maskHd = makeIndexedFrame(spriteSize, 255);
    sprite.colorizedHd = makeRgb565Frame(spriteSize, 0);
    sprite.hasHd = rendersHdPlaneOnly();
    normalizeSpriteRecord(sprite, m_colorCount);
    return sprite;
}

bool ProjectDocument::isSpriteNameUsed(const std::string& name) const
{
    return std::any_of(m_sprites.begin(), m_sprites.end(),
                       [&name](const SpriteRecord& sprite) { return sprite.name == name; });
}

std::size_t ProjectDocument::appendSprite(SpriteRecord sprite, std::size_t sourceFrameIndex)
{
    if (m_sprites.size() >= kMaxSprites) {
        throw std::length_error("a project holds at most " + std::to_string(kMaxSprites) +
                                " sprites");
    }
    const Size spriteSize{static_cast<int>(kMaxSpriteWidth), static_cast<int>(kMaxSpriteHeight)};
    const bool hasExtra = !editsOriginalPlaneOnly();
    if (!sprite.originalSd.isValid() || !hasSize(sprite.originalSd.size, spriteSize) ||
        !sprite.colorizedSd.isValid() || !hasSize(sprite.colorizedSd.size, spriteSize) ||
        (hasExtra && (!sprite.maskHd.isValid() || !hasSize(sprite.maskHd.size, spriteSize) ||
                      !sprite.colorizedHd.isValid() ||
                      !hasSize(sprite.colorizedHd.size, spriteSize))) ||
        (!hasExtra && sprite.hasHd) ||
        sprite.detectionDwords.size() != kMaxSpriteDetectAreas ||
        sprite.detectionDwordPositions.size() != kMaxSpriteDetectAreas ||
        sprite.detectionAreas.size() != kMaxSpriteDetectAreas) {
        throw std::invalid_argument("sprite record does not fit the project");
    }
    const std::size_t spriteIndex = m_sprites.size();
    EditorMetadata& metadata = m_editorMetadata;
    ensureSpriteTableEntries(metadata, spriteIndex + 1);
    setFixedName(metadata.spriteNames, spriteIndex, kSpriteNameBytes, sprite.name);
    metadata.spriteSourceFrames[spriteIndex] = static_cast<uint32_t>(sourceFrameIndex);
    metadata.spriteSourceRects[spriteIndex * 4] = kNoSpriteSourceRect;
    std::fill_n(metadata.spriteSourceRects.begin() +
                    static_cast<std::ptrdiff_t>(spriteIndex * 4 + 1),
                3,
                uint16_t{0});
    std::fill_n(metadata.spriteSourceMirrors.begin() +
                    static_cast<std::ptrdiff_t>(spriteIndex * 2),
                2,
                uint32_t{0});
    m_sprites.push_back(std::move(sprite));
    ensureUniqueIdOf(m_sprites, m_sprites.size() - 1);
    markChanged();
    return spriteIndex;
}

RemovedSprite ProjectDocument::removeSprite(std::size_t spriteIndex)
{
    if (spriteIndex >= m_sprites.size()) {
        throw std::out_of_range("sprite index out of range");
    }
    RemovedSprite removed;
    removed.index = spriteIndex;
    removed.tables = recordTables();
    removed.sprite = std::move(m_sprites[spriteIndex]);
    const std::size_t oldCount = m_sprites.size();
    m_sprites.erase(m_sprites.begin() + static_cast<std::ptrdiff_t>(spriteIndex));

    for (std::size_t frameIndex = 0; frameIndex < m_frames.size(); ++frameIndex) {
        std::vector<FrameSpriteReference>& references = m_frames[frameIndex].spriteReferences;
        if (references.empty()) {
            continue;
        }
        // An emptied slot at the end keeps the bounds of the last slot, as the Win32
        // editor's Delete_Sprite leaves it.
        FrameSpriteReference emptySlot = references.back();
        emptySlot.spriteId = kNoSpriteId;
        std::size_t write = 0;
        for (std::size_t slot = 0; slot < references.size(); ++slot) {
            if (references[slot].spriteId == spriteIndex) {
                removed.references.push_back({frameIndex, slot, references[slot]});
            } else {
                references[write++] = references[slot];
            }
        }
        std::fill(references.begin() + static_cast<std::ptrdiff_t>(write), references.end(),
                  emptySlot);
        for (FrameSpriteReference& reference : references) {
            if (reference.spriteId != kNoSpriteId && reference.spriteId > spriteIndex) {
                --reference.spriteId;
            }
        }
    }

    EditorMetadata& metadata = m_editorMetadata;
    removeTableEntry(metadata.spriteNames, spriteIndex, oldCount, kSpriteNameBytes);
    removeTableEntry(metadata.spriteSourceFrames, spriteIndex, oldCount, 1);
    removeTableEntry(metadata.spriteSourceRects, spriteIndex, oldCount, 4);
    removeTableEntry(metadata.spriteSourceMirrors, spriteIndex, oldCount, 2);
    markChanged();
    return removed;
}

void ProjectDocument::insertSprite(RemovedSprite removed)
{
    if (removed.index > m_sprites.size() || m_sprites.size() >= kMaxSprites) {
        throw std::invalid_argument("removed sprite does not fit the document");
    }
    for (const RemovedSpriteReference& reference : removed.references) {
        if (reference.frameIndex >= m_frames.size() ||
            reference.slotIndex >= kSpritesPerFrame) {
            throw std::invalid_argument("removed sprite does not fit the document");
        }
    }
    requireFittingRecordTables(removed.tables);
    for (FrameRecord& frame : m_frames) {
        for (FrameSpriteReference& reference : frame.spriteReferences) {
            // The last ID would become "no sprite".
            if (reference.spriteId >= removed.index && reference.spriteId < kNoSpriteId - 1) {
                ++reference.spriteId;
            }
        }
    }
    for (const RemovedSpriteReference& reference : removed.references) {
        std::vector<FrameSpriteReference>& references =
            m_frames[reference.frameIndex].spriteReferences;
        if (references.size() != kSpritesPerFrame) {
            references.assign(kSpritesPerFrame, FrameSpriteReference{});
        }
        references.insert(references.begin() + static_cast<std::ptrdiff_t>(reference.slotIndex),
                          reference.reference);
        references.pop_back();
    }
    m_sprites.insert(m_sprites.begin() + static_cast<std::ptrdiff_t>(removed.index),
                     std::move(removed.sprite));
    ensureUniqueIdOf(m_sprites, removed.index);
    restoreRecordTables(std::move(removed.tables));
}

BackgroundRecord ProjectDocument::makeBackgroundFromFrame(std::size_t frameIndex) const
{
    const FrameRecord& frame = requireFrame(m_frames, frameIndex);
    BackgroundRecord background;
    background.colorizedSd = frame.colorizedSd;
    if (rendersHdPlaneOnly()) {
        if (!frame.colorizedHd) {
            throwMissingExtra(m_geometry, "frame");
        }
        background.colorizedHd = frame.colorizedHd;
    }
    return background;
}

std::size_t ProjectDocument::appendBackground(BackgroundRecord background)
{
    if (m_backgrounds.size() >= kMaxBackgrounds) {
        throw std::length_error("a project holds at most " + std::to_string(kMaxBackgrounds) +
                                " backgrounds");
    }
    if (!background.colorizedSd.isValid() ||
        !hasSize(background.colorizedSd.size, m_geometry.planeSize(Resolution::Sd)) ||
        (background.colorizedHd &&
         (editsOriginalPlaneOnly() || !background.colorizedHd->isValid() ||
          !hasSize(background.colorizedHd->size, m_geometry.planeSize(Resolution::Hd)))) ||
        background.inactiveHd) {
        throw std::invalid_argument("background record does not fit the project");
    }
    m_backgrounds.push_back(std::move(background));
    ensureUniqueIdOf(m_backgrounds, m_backgrounds.size() - 1);
    markChanged();
    return m_backgrounds.size() - 1;
}

RemovedBackground ProjectDocument::removeBackground(std::size_t backgroundIndex)
{
    if (backgroundIndex >= m_backgrounds.size()) {
        throw std::out_of_range("background index out of range");
    }
    RemovedBackground removed;
    removed.index = backgroundIndex;
    removed.background = std::move(m_backgrounds[backgroundIndex]);
    m_backgrounds.erase(m_backgrounds.begin() + static_cast<std::ptrdiff_t>(backgroundIndex));
    for (std::size_t frameIndex = 0; frameIndex < m_frames.size(); ++frameIndex) {
        std::optional<FrameBackgroundData>& data = m_frames[frameIndex].backgroundData;
        if (!data || data->backgroundId == kNoBackgroundId) {
            continue;
        }
        if (data->backgroundId == backgroundIndex) {
            data->backgroundId = kNoBackgroundId;
            removed.frames.push_back(frameIndex);
        } else if (data->backgroundId > backgroundIndex) {
            --data->backgroundId;
        }
    }
    markChanged();
    return removed;
}

void ProjectDocument::insertBackground(RemovedBackground removed)
{
    if (removed.index > m_backgrounds.size() || m_backgrounds.size() >= kMaxBackgrounds ||
        (!removed.frames.empty() && removed.frames.back() >= m_frames.size())) {
        throw std::invalid_argument("removed background does not fit the document");
    }
    for (FrameRecord& frame : m_frames) {
        if (frame.backgroundData && frame.backgroundData->backgroundId >= removed.index &&
            frame.backgroundData->backgroundId < kNoBackgroundId - 1) {
            ++frame.backgroundData->backgroundId;
        }
    }
    for (std::size_t frameIndex : removed.frames) {
        if (m_frames[frameIndex].backgroundData) {
            m_frames[frameIndex].backgroundData->backgroundId =
                static_cast<uint16_t>(removed.index);
        }
    }
    m_backgrounds.insert(m_backgrounds.begin() + static_cast<std::ptrdiff_t>(removed.index),
                         std::move(removed.background));
    ensureUniqueIdOf(m_backgrounds, removed.index);
    markChanged();
}

std::size_t ProjectDocument::appendComparisonMask()
{
    if (m_comparisonMasks.size() >= kMaxComparisonMasks) {
        throw std::length_error("a project holds at most 64 comparison masks");
    }
    m_comparisonMasks.push_back(makeIndexedFrame(originalFrameSize(), 0));
    m_comparisonMaskMeta.push_back(ComponentMeta{});
    ensureUniqueIdOf(m_comparisonMaskMeta, m_comparisonMaskMeta.size() - 1);
    markChanged();
    m_identificationRevision = m_revision;
    return m_comparisonMasks.size() - 1;
}

RemovedComparisonMask ProjectDocument::removeComparisonMask(std::size_t maskIndex)
{
    if (maskIndex >= m_comparisonMasks.size()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    RemovedComparisonMask removed;
    removed.index = maskIndex;
    removed.tables = recordTables();
    removed.mask = std::move(m_comparisonMasks[maskIndex]);
    removed.meta = std::move(m_comparisonMaskMeta[maskIndex]);
    m_comparisonMasks.erase(m_comparisonMasks.begin() + static_cast<std::ptrdiff_t>(maskIndex));
    m_comparisonMaskMeta.erase(m_comparisonMaskMeta.begin() +
                               static_cast<std::ptrdiff_t>(maskIndex));
    for (std::size_t frameIndex = 0; frameIndex < m_frames.size(); ++frameIndex) {
        uint8_t& maskId = m_frames[frameIndex].comparisonMaskId;
        if (maskId == kNoComparisonMaskId) {
            continue;
        }
        if (maskId == maskIndex) {
            maskId = kNoComparisonMaskId;
            removed.frames.push_back(frameIndex);
        } else if (maskId > maskIndex) {
            --maskId;
        }
    }
    // The Win32 editor has a name for each of the 64 mask slots, used or not.
    removeTableEntry(m_editorMetadata.maskNames, maskIndex, kMaxComparisonMasks, kMaskNameBytes);
    markChanged();
    m_identificationRevision = m_revision;
    return removed;
}

void ProjectDocument::insertComparisonMask(RemovedComparisonMask removed)
{
    if (removed.index > m_comparisonMasks.size() ||
        m_comparisonMasks.size() >= kMaxComparisonMasks ||
        !removed.mask.isValid() || !hasSize(removed.mask.size, originalFrameSize()) ||
        (!removed.frames.empty() && removed.frames.back() >= m_frames.size())) {
        throw std::invalid_argument("removed comparison mask does not fit the document");
    }
    requireFittingRecordTables(removed.tables);
    for (FrameRecord& frame : m_frames) {
        if (frame.comparisonMaskId >= removed.index &&
            frame.comparisonMaskId < kNoComparisonMaskId - 1) {
            ++frame.comparisonMaskId;
        }
    }
    for (std::size_t frameIndex : removed.frames) {
        m_frames[frameIndex].comparisonMaskId = static_cast<uint8_t>(removed.index);
    }
    m_comparisonMasks.insert(
        m_comparisonMasks.begin() + static_cast<std::ptrdiff_t>(removed.index),
        std::move(removed.mask));
    m_comparisonMaskMeta.insert(
        m_comparisonMaskMeta.begin() + static_cast<std::ptrdiff_t>(removed.index),
        std::move(removed.meta));
    ensureUniqueIdOf(m_comparisonMaskMeta, removed.index);
    restoreRecordTables(std::move(removed.tables));
    m_identificationRevision = m_revision;
}

std::size_t ProjectDocument::comparisonMaskUseCount(std::size_t maskIndex) const
{
    return static_cast<std::size_t>(
        std::count_if(m_frames.begin(), m_frames.end(), [maskIndex](const FrameRecord& frame) {
            return frame.comparisonMaskId != kNoComparisonMaskId &&
                   frame.comparisonMaskId == maskIndex;
        }));
}

std::size_t ProjectDocument::backgroundUseCount(std::size_t backgroundIndex) const
{
    return static_cast<std::size_t>(
        std::count_if(m_frames.begin(), m_frames.end(),
                      [backgroundIndex](const FrameRecord& frame) {
                          return frame.backgroundData &&
                                 frame.backgroundData->backgroundId != kNoBackgroundId &&
                                 frame.backgroundData->backgroundId == backgroundIndex;
                      }));
}

std::size_t ProjectDocument::spriteUseCount(std::size_t spriteIndex) const
{
    return static_cast<std::size_t>(
        std::count_if(m_frames.begin(), m_frames.end(), [spriteIndex](const FrameRecord& frame) {
            return std::any_of(frame.spriteReferences.begin(), frame.spriteReferences.end(),
                               [spriteIndex](const FrameSpriteReference& reference) {
                                   return reference.spriteId != kNoSpriteId &&
                                          reference.spriteId == spriteIndex;
                               });
        }));
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
        frame.scene,
        frame.tags,
        frame.lights,
        frame.originalOnlyDynamicSets,
        frame.meta,
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
    snapshot.geometry = m_geometry;
    snapshot.colorizationSettings = m_colorizationSettings;
    snapshot.speakerLights = m_speakerLights;
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
                    ? static_cast<uint16_t>(subsetId(ids.sprites, reference.spriteId))
                    : kNoSpriteId;
        }
        snapshot.frames.push_back(std::move(frame));
        ids.frames.push_back(frameIndex);
    }
    for (std::size_t maskIndex : ids.comparisonMasks) {
        snapshot.comparisonMasks.push_back(m_comparisonMasks[maskIndex]);
        snapshot.comparisonMaskMeta.push_back(m_comparisonMaskMeta[maskIndex]);
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
    snapshot.comparisonMaskMeta = m_comparisonMaskMeta;
    snapshot.projectId = m_projectId;
    snapshot.team = m_team;
    snapshot.backgrounds = m_backgrounds;
    snapshot.sprites = m_sprites;
    snapshot.palettes = m_palettes;
    snapshot.unsupportedPayloads = m_unsupportedPayloads;
    snapshot.colorCount = m_colorCount;
    snapshot.geometry = m_geometry;
    snapshot.editorMetadata = m_editorMetadata;
    snapshot.helperLines = m_helperLines;
    snapshot.imageZones = m_imageZones;
    snapshot.colorizationSettings = m_colorizationSettings;
    snapshot.speakerLights = m_speakerLights;
    snapshot.license = m_license;
    snapshot.discardedExtraPlane = m_discardedExtraPlane;
    if (m_geometry.discardsExtraPlane()) {
        discardExtraPlane(snapshot);
    }
    return snapshot;
}

}  // namespace colorizing::document
