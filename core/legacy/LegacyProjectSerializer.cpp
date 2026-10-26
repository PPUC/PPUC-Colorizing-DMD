#include "legacy/LegacyProjectSerializer.h"

#include <algorithm>
#include <array>

#include "legacy/LegacyByteWriter.h"
#include "legacy/LegacyProjectIo.h"
#include "render/CanvasLayout.h"

namespace colorizing::legacy {

namespace {

// The header revision written: 21, with the format version and the sprite slots of a frame
// (libserum 2.9 on).
constexpr uint32_t kWrittenCromRevision = 21;

using detail::kRomNameBytes;
using detail::Writer;
using detail::WriteCancelled;

std::array<uint32_t, 256> buildCrc32Table()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t ch = i;
        uint32_t crc = 0;
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t b = (ch ^ crc) & 1;
            crc >>= 1;
            if (b) {
                crc ^= 0xEDB88320;
            }
            ch >>= 1;
        }
        table[i] = crc;
    }
    return table;
}

std::size_t pixelCount(render::Size size)
{
    return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

// The Win32 editor blanks colorized pixels that are covered by dynamic content when saving.
void writeColorizedUnderDynamicMask(Writer& writer,
                                    const document::Rgb565Frame& frame,
                                    const document::IndexedFrame* dynamicMask)
{
    if (!dynamicMask || dynamicMask->pixels.size() != frame.pixels.size()) {
        writer.u16s(frame.pixels);
        return;
    }
    // One pointer per block: a read through operator[] checks every time whether the block
    // is packed.
    const uint8_t* mask = dynamicMask->pixels.data();
    const uint16_t* pixels = frame.pixels.data();
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        writer.u16(mask[i] < 255 ? uint16_t{0} : pixels[i]);
    }
}

const document::IndexedFrame* comparisonMaskForFrame(const document::ProjectSnapshot& snapshot,
                                                     const document::ProjectFrameSnapshot& frame)
{
    if (frame.comparisonMaskId == 255 ||
        frame.comparisonMaskId >= snapshot.comparisonMasks.size()) {
        return nullptr;
    }
    return &snapshot.comparisonMasks[frame.comparisonMaskId];
}

}  // namespace

bool isLegacyExtraPlaneSection(const std::string& name)
{
    return name.rfind("HD ", 0) == 0 || (name.rfind("extra ", 0) == 0 &&
                                         name.find(" flags") != std::string::npos);
}

uint32_t legacyFrameHash(const document::IndexedFrame& original,
                         const document::IndexedFrame* comparisonMask,
                         bool shapeMode)
{
    static const std::array<uint32_t, 256> table = buildCrc32Table();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* pixels = original.pixels.data();
    const uint8_t* mask = comparisonMask ? comparisonMask->pixels.data() : nullptr;
    const std::size_t maskSize = comparisonMask ? comparisonMask->pixels.size() : 0;
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        if (i < maskSize && mask[i] != 0) {
            continue;
        }
        uint8_t value = pixels[i];
        if (shapeMode && value > 1) {
            value = 1;
        }
        crc = (crc >> 8) ^ table[(value ^ crc) & 0xFF];
    }
    return ~crc;
}

std::vector<uint16_t> legacyOriginalColors(uint32_t colorCount)
{
    return document::plainOriginalColors(colorCount);
}

bool isLegacyFrameActive(uint32_t durationMs,
                         const document::IndexedFrame& original,
                         const document::Rgb565Frame* nativeColorized,
                         const document::IndexedFrame* nativeDynamicMask,
                         const std::vector<uint16_t>& originalColors)
{
    if (durationMs >= kSkipFrameDurationMs) {
        return true;
    }
    if (!nativeColorized) {
        return false;
    }
    const uint8_t* shades = original.pixels.data();
    const std::size_t shadeCount = original.pixels.size();
    const uint16_t* colors = nativeColorized->pixels.data();
    const uint8_t* dynamicMask = nativeDynamicMask ? nativeDynamicMask->pixels.data() : nullptr;
    const std::size_t dynamicCount = nativeDynamicMask ? nativeDynamicMask->pixels.size() : 0;
    for (std::size_t index = 0; index < nativeColorized->pixels.size(); ++index) {
        const uint8_t shade = index < shadeCount ? shades[index] : uint8_t{0};
        const uint16_t plain = shade < originalColors.size() ? originalColors[shade] : uint16_t{0};
        // The saved frame has its dynamic pixels blanked; any dynamic pixel counts.
        const bool dynamic = index < dynamicCount && dynamicMask[index] != 255;
        if (dynamic || colors[index] != plain) {
            return true;
        }
    }
    return false;
}

bool isLegacyFrameActive(const document::ProjectFrameSnapshot& frame,
                         const std::vector<uint16_t>& originalColors,
                         render::Resolution nativePlane)
{
    // Only the native plane lines up with the original. A 256x64 project renders its HD frame
    // alone; its SD frame is never shown and does not count.
    const bool extraIsNative = nativePlane == render::Resolution::Hd;
    const document::Rgb565Frame* colorized =
        !extraIsNative ? &frame.colorizedSd
                       : frame.colorizedHd ? &*frame.colorizedHd : nullptr;
    const document::IndexedFrame* dynamicMask =
        !frame.dynamicData ? nullptr
                           : extraIsNative ? &frame.dynamicData->maskHd
                                           : &frame.dynamicData->maskSd;
    return isLegacyFrameActive(frame.durationMs, frame.originalSd, colorized, dynamicMask,
                               originalColors);
}

bool isFrameSkippedAtRuntime(const document::ProjectDocument& document, std::size_t frameIndex)
{
    // Checked first: it is true for nearly every frame and needs no pixel.
    if (document.frameDurationMs(frameIndex) >= kSkipFrameDurationMs) {
        return false;
    }
    const render::Resolution nativePlane = document.geometry().nativePlane();
    const bool extraIsNative = nativePlane == render::Resolution::Hd;
    const std::optional<document::FrameDynamicData>& dynamicData =
        document.dynamicData(frameIndex);
    const document::Rgb565Frame* colorized =
        extraIsNative && !document.hasHdFrame(frameIndex)
            ? nullptr
            : &document.colorizedFrame(frameIndex, nativePlane);
    const document::IndexedFrame* dynamicMask =
        !dynamicData ? nullptr : extraIsNative ? &dynamicData->maskHd : &dynamicData->maskSd;
    if (isLegacyFrameActive(document.frameDurationMs(frameIndex),
                            document.originalFrame(frameIndex),
                            colorized,
                            dynamicMask,
                            legacyOriginalColors(document.colorCount()))) {
        return false;
    }
    // libserum's FrameHasRenderableContent(): an inactive frame is still rendered if it has a
    // background or a dynamic zone, on the original plane or, for a frame with extra-plane
    // content, on the extra plane.
    const std::optional<document::FrameBackgroundData>& backgroundData =
        document.backgroundData(frameIndex);
    if (backgroundData && backgroundData->backgroundId < document.backgroundCount()) {
        return false;
    }
    const auto hasZone = [](const document::IndexedFrame& mask) {
        return std::any_of(mask.pixels.begin(), mask.pixels.end(),
                           [](uint8_t set) { return set != 255; });
    };
    if (dynamicData && document.geometry().isEditable(render::Resolution::Hd) &&
        document.hasHdFrame(frameIndex) && hasZone(dynamicData->maskHd)) {
        return false;
    }
    return !(dynamicData && hasZone(dynamicData->maskSd));
}

namespace {

// What every element of the cROM blocks is written from.
struct CromContext {
    const document::ProjectSnapshot* snapshot = nullptr;
    std::size_t sdPixels = 0;
    std::size_t hdPixels = 0;
    std::size_t spritePixels = 0;
    std::size_t dynamicColorCount = 0;
    std::size_t spriteDynamicColorCount = 0;
    std::size_t rotationValueCount = 0;
    std::size_t shadowCount = 0;
    std::size_t slots = 0;
    // Sprite bounds are original frame pixels.
    render::Size originalSize;
    render::Resolution nativePlane = render::Resolution::Sd;
    std::vector<uint16_t> originalColors;
    document::CompactArray<uint8_t> noDirections;
    document::CompactArray<uint16_t> noColors;
};

CromContext makeCromContext(const document::ProjectSnapshot& snapshot)
{
    CromContext context;
    context.snapshot = &snapshot;
    context.sdPixels = pixelCount(snapshot.geometry.planeSize(render::Resolution::Sd));
    context.hdPixels = pixelCount(snapshot.geometry.planeSize(render::Resolution::Hd));
    context.spritePixels = document::kMaxSpriteWidth * document::kMaxSpriteHeight;
    context.dynamicColorCount =
        document::kMaxDynamicSetsPerFrame * static_cast<std::size_t>(snapshot.colorCount);
    context.spriteDynamicColorCount =
        document::kMaxDynamicSetsPerSprite * static_cast<std::size_t>(snapshot.colorCount);
    context.rotationValueCount = document::kMaxColorRotations * document::kMaxColorRotationLength;
    context.shadowCount = document::kMaxDynamicSetsPerFrame;
    context.slots = document::kSpritesPerFrame;
    context.originalSize = document::originalFrameSize(snapshot);
    context.nativePlane = snapshot.geometry.nativePlane();
    context.originalColors = legacyOriginalColors(snapshot.colorCount);
    return context;
}

// One element of a block: a frame, comparison mask, sprite or background.
void writeCromElement(Writer& writer, const CromContext& c, CromBlock block, std::size_t index)
{
    const document::ProjectSnapshot& snapshot = *c.snapshot;
    switch (block) {
        case CromBlock::FrameHash: {
            const auto& frame = snapshot.frames[index];
            // The Win32 editor only treats the exact value 1 as shape mode when hashing.
            writer.u32(legacyFrameHash(frame.originalSd,
                                       comparisonMaskForFrame(snapshot, frame),
                                       frame.shapeComparisonMode == 1));
            return;
        }
        case CromBlock::FrameShapeMode:
            writer.u8(snapshot.frames[index].shapeComparisonMode);
            return;
        case CromBlock::FrameMaskId:
            writer.u8(snapshot.frames[index].comparisonMaskId);
            return;
        case CromBlock::ComparisonMask:
            writer.bytes(snapshot.comparisonMasks[index].pixels);
            return;
        case CromBlock::FrameHasExtra:
            writer.u8(snapshot.frames[index].colorizedHd ? 1 : 0);
            return;
        case CromBlock::FrameColors: {
            const auto& frame = snapshot.frames[index];
            writeColorizedUnderDynamicMask(writer,
                                           frame.colorizedSd,
                                           frame.dynamicData ? &frame.dynamicData->maskSd
                                                             : nullptr);
            return;
        }
        case CromBlock::FrameColorsExtra: {
            const auto& frame = snapshot.frames[index];
            if (frame.colorizedHd) {
                // The HD dynamic mask counts only where HD is the ROM frame's plane. On the HD
                // plane of an ordinary project libserum renders the SD zones and never reads
                // the HD mask: a mask stored there would black out the HD picture under it,
                // which no zone then covers. That plane is written as it is.
                writeColorizedUnderDynamicMask(
                    writer,
                    *frame.colorizedHd,
                    frame.dynamicData && c.nativePlane == render::Resolution::Hd
                        ? &frame.dynamicData->maskHd
                        : nullptr);
            } else if (frame.inactiveHd) {
                writer.u16s(frame.inactiveHd->pixels);
            } else {
                writer.repeatU16(0, c.hdPixels);
            }
            return;
        }
        case CromBlock::FrameDynamicMask: {
            const auto& frame = snapshot.frames[index];
            if (frame.dynamicData) {
                writer.bytes(frame.dynamicData->maskSd.pixels);
            } else {
                writer.repeatU8(255, c.sdPixels);
            }
            return;
        }
        case CromBlock::FrameDynamicMaskExtra: {
            const auto& frame = snapshot.frames[index];
            if (frame.dynamicData) {
                writer.bytes(frame.dynamicData->maskHd.pixels);
            } else {
                writer.repeatU8(255, c.hdPixels);
            }
            return;
        }
        case CromBlock::FrameDynamicColors: {
            const auto& frame = snapshot.frames[index];
            writer.u16sOr(frame.dynamicData ? frame.dynamicData->colorsSd : c.noColors,
                          c.dynamicColorCount,
                          0);
            return;
        }
        case CromBlock::FrameDynamicColorsExtra: {
            const auto& frame = snapshot.frames[index];
            writer.u16sOr(frame.dynamicData ? frame.dynamicData->colorsHd : c.noColors,
                          c.dynamicColorCount,
                          0);
            return;
        }
        case CromBlock::SpriteHasExtra:
            writer.u8(snapshot.sprites[index].hasHd ? 1 : 0);
            return;
        case CromBlock::FrameSprites: {
            const auto& frame = snapshot.frames[index];
            for (std::size_t slot = 0; slot < c.slots; ++slot) {
                const uint16_t spriteId = slot < frame.spriteReferences.size()
                                              ? frame.spriteReferences[slot].spriteId
                                              : document::kNoSpriteId;
                writer.u16(spriteId);
            }
            return;
        }
        case CromBlock::SpriteOriginal:
            writer.bytes(snapshot.sprites[index].originalSd.pixels);
            return;
        case CromBlock::SpriteColors:
            writer.u16s(snapshot.sprites[index].colorizedSd.pixels);
            return;
        case CromBlock::SpriteMaskExtra:
            writer.bytes(snapshot.sprites[index].maskHd.pixels);
            return;
        case CromBlock::SpriteColorsExtra:
            writer.u16s(snapshot.sprites[index].colorizedHd.pixels);
            return;
        case CromBlock::FrameActive:
            writer.u8(isLegacyFrameActive(snapshot.frames[index], c.originalColors,
                                          c.nativePlane)
                          ? 1
                          : 0);
            return;
        case CromBlock::FrameRotations:
            writer.u16sOr(snapshot.frames[index].colorRotationsSd, c.rotationValueCount, 0);
            return;
        case CromBlock::FrameRotationsExtra:
            writer.u16sOr(snapshot.frames[index].colorRotationsHd, c.rotationValueCount, 0);
            return;
        case CromBlock::SpriteDetectionDwords:
            writer.u32s(snapshot.sprites[index].detectionDwords);
            return;
        case CromBlock::SpriteDetectionPositions:
            writer.u16s(snapshot.sprites[index].detectionDwordPositions);
            return;
        case CromBlock::SpriteDetectionAreas:
            for (const auto& area : snapshot.sprites[index].detectionAreas) {
                writer.u16(area.x);
                writer.u16(area.y);
                writer.u16(area.width);
                writer.u16(area.height);
            }
            return;
        case CromBlock::FrameTrigger:
            writer.u32(snapshot.frames[index].triggerId);
            return;
        case CromBlock::FrameSpriteBounds: {
            const auto& frame = snapshot.frames[index];
            for (std::size_t slot = 0; slot < c.slots; ++slot) {
                if (slot < frame.spriteReferences.size()) {
                    const auto& reference = frame.spriteReferences[slot];
                    writer.u16(reference.minX);
                    writer.u16(reference.minY);
                    writer.u16(reference.maxX);
                    writer.u16(reference.maxY);
                } else {
                    writer.u16(0);
                    writer.u16(0);
                    writer.u16(static_cast<uint16_t>(c.originalSize.width - 1));
                    writer.u16(static_cast<uint16_t>(c.originalSize.height - 1));
                }
            }
            return;
        }
        case CromBlock::BackgroundHasExtra:
            writer.u8(snapshot.backgrounds[index].colorizedHd ? 1 : 0);
            return;
        case CromBlock::BackgroundColors:
            writer.u16s(snapshot.backgrounds[index].colorizedSd.pixels);
            return;
        case CromBlock::BackgroundColorsExtra: {
            const auto& background = snapshot.backgrounds[index];
            if (background.colorizedHd) {
                writer.u16s(background.colorizedHd->pixels);
            } else if (background.inactiveHd) {
                writer.u16s(background.inactiveHd->pixels);
            } else {
                writer.repeatU16(0, c.hdPixels);
            }
            return;
        }
        case CromBlock::FrameBackgroundId: {
            const auto& frame = snapshot.frames[index];
            writer.u16(frame.backgroundData ? frame.backgroundData->backgroundId
                                            : uint16_t{0xffff});
            return;
        }
        // A frame that never had background data gets the editor's "whole frame" default
        // mask.
        case CromBlock::FrameBackgroundMask: {
            const auto& frame = snapshot.frames[index];
            if (frame.backgroundData) {
                writer.bytes(frame.backgroundData->maskSd.pixels);
            } else {
                writer.repeatU8(1, c.sdPixels);
            }
            return;
        }
        case CromBlock::FrameBackgroundMaskExtra: {
            const auto& frame = snapshot.frames[index];
            if (frame.backgroundData) {
                writer.bytes(frame.backgroundData->maskHd.pixels);
            } else {
                writer.repeatU8(1, c.hdPixels);
            }
            return;
        }
        case CromBlock::FrameShadowDirections: {
            const auto& frame = snapshot.frames[index];
            writer.bytesOr(frame.dynamicData ? frame.dynamicData->shadowDirectionsSd
                                             : c.noDirections,
                           c.shadowCount,
                           0);
            return;
        }
        case CromBlock::FrameShadowColors: {
            const auto& frame = snapshot.frames[index];
            writer.u16sOr(frame.dynamicData ? frame.dynamicData->shadowColorsSd : c.noColors,
                          c.shadowCount,
                          0);
            return;
        }
        case CromBlock::FrameShadowDirectionsExtra: {
            const auto& frame = snapshot.frames[index];
            writer.bytesOr(frame.dynamicData ? frame.dynamicData->shadowDirectionsHd
                                             : c.noDirections,
                           c.shadowCount,
                           0);
            return;
        }
        case CromBlock::FrameShadowColorsExtra: {
            const auto& frame = snapshot.frames[index];
            writer.u16sOr(frame.dynamicData ? frame.dynamicData->shadowColorsHd : c.noColors,
                          c.shadowCount,
                          0);
            return;
        }
        case CromBlock::SpriteDynamicColors:
            writer.u16sOr(snapshot.sprites[index].dynamicColorsSd, c.spriteDynamicColorCount, 0);
            return;
        case CromBlock::SpriteDynamicColorsExtra:
            writer.u16sOr(snapshot.sprites[index].dynamicColorsHd, c.spriteDynamicColorCount, 0);
            return;
        case CromBlock::SpriteDynamicMask:
            writer.bytesOr(snapshot.sprites[index].dynamicMaskSd.pixels, c.spritePixels, 255);
            return;
        case CromBlock::SpriteDynamicMaskExtra:
            writer.bytesOr(snapshot.sprites[index].dynamicMaskHd.pixels, c.spritePixels, 255);
            return;
        case CromBlock::SpriteShapeMode:
            writer.u8(snapshot.sprites[index].shapeMode);
            return;
    }
}

bool writeCromContent(std::ostream& stream,
                      const document::ProjectSnapshot& snapshot,
                      std::string& errorMessage,
                      LegacySectionOffsets* sections,
                      LegacyIoProgress* progress)
{
    if (!document::isValidProjectSnapshot(snapshot, &errorMessage)) {
        return false;
    }
    if (snapshot.frames.empty()) {
        errorMessage = "project has no frames";
        return false;
    }
    if (snapshot.backgrounds.size() > 0xffff) {
        errorMessage = "too many backgrounds";
        return false;
    }

    const render::Size sdSize = snapshot.geometry.planeSize(render::Resolution::Sd);
    const render::Size hdSize = snapshot.geometry.planeSize(render::Resolution::Hd);
    const auto& frames = snapshot.frames;

    Writer writer(stream, sections, progress);
    writer.section("header");
    writer.name(snapshot.romName, kRomNameBytes);
    const std::size_t slots = document::kSpritesPerFrame;
    writer.u32(kWrittenCromRevision * static_cast<uint32_t>(sizeof(uint32_t)));
    writer.u32(static_cast<uint32_t>(sdSize.width));
    writer.u32(static_cast<uint32_t>(sdSize.height));
    writer.u32(static_cast<uint32_t>(hdSize.width));
    writer.u32(static_cast<uint32_t>(hdSize.height));
    writer.u32(static_cast<uint32_t>(frames.size()));
    writer.u32(snapshot.colorCount);
    writer.u32(static_cast<uint32_t>(snapshot.comparisonMasks.size()));
    writer.u32(static_cast<uint32_t>(snapshot.sprites.size()));
    writer.u16(static_cast<uint16_t>(snapshot.backgrounds.size()));
    writer.u32(snapshot.geometry.original256x64Flag() ? 1 : 0);
    writer.u32(document::kCromFormatVersion);
    writer.u32(static_cast<uint32_t>(slots));

    const CromContext context = makeCromContext(snapshot);
    for (std::size_t block = 0; block < kCromBlockCount; ++block) {
        const auto kind = static_cast<CromBlock>(block);
        writer.section(cromBlockName(kind));
        const std::size_t count = cromBlockElementCount(snapshot, kind);
        for (std::size_t index = 0; index < count; ++index) {
            writeCromElement(writer, context, kind, index);
        }
    }
    writer.section("settings block");
    writer.bytes(cromSettingsBlock(snapshot));
    writer.section("end");

    if (!writer.finish()) {
        errorMessage = "could not write cROM data";
        return false;
    }
    return true;
}

}  // namespace

const char* cromBlockName(CromBlock block)
{
    // The names of LegacySectionOffsets, in cROM order.
    static constexpr std::array<const char*, kCromBlockCount> kNames{
        "hash codes",
        "shape comparison modes",
        "comparison mask IDs",
        "comparison masks",
        "extra frame flags",
        "SD colorized frames",
        "HD colorized frames",
        "SD dynamic masks",
        "HD dynamic masks",
        "SD dynamic colors",
        "HD dynamic colors",
        "extra sprite flags",
        "frame sprites",
        "sprite originals",
        "SD sprite colors",
        "HD sprite masks",
        "HD sprite colors",
        "active frame flags",
        "SD color rotations",
        "HD color rotations",
        "sprite detection dwords",
        "sprite detection dword positions",
        "sprite detection areas",
        "trigger IDs",
        "frame sprite bounding boxes",
        "extra background flags",
        "SD backgrounds",
        "HD backgrounds",
        "background IDs",
        "SD background masks",
        "HD background masks",
        "SD dynamic shadow directions",
        "SD dynamic shadow colors",
        "HD dynamic shadow directions",
        "HD dynamic shadow colors",
        "SD sprite dynamic colors",
        "HD sprite dynamic colors",
        "SD sprite dynamic masks",
        "HD sprite dynamic masks",
        "sprite shape modes",
    };
    return kNames[static_cast<std::size_t>(block)];
}

std::size_t cromBlockElementCount(const document::ProjectSnapshot& snapshot, CromBlock block)
{
    switch (block) {
        case CromBlock::ComparisonMask:
            return snapshot.comparisonMasks.size();
        case CromBlock::SpriteHasExtra:
        case CromBlock::SpriteOriginal:
        case CromBlock::SpriteColors:
        case CromBlock::SpriteMaskExtra:
        case CromBlock::SpriteColorsExtra:
        case CromBlock::SpriteDetectionDwords:
        case CromBlock::SpriteDetectionPositions:
        case CromBlock::SpriteDetectionAreas:
        case CromBlock::SpriteDynamicColors:
        case CromBlock::SpriteDynamicColorsExtra:
        case CromBlock::SpriteDynamicMask:
        case CromBlock::SpriteDynamicMaskExtra:
        case CromBlock::SpriteShapeMode:
            return snapshot.sprites.size();
        case CromBlock::BackgroundHasExtra:
        case CromBlock::BackgroundColors:
        case CromBlock::BackgroundColorsExtra:
            return snapshot.backgrounds.size();
        default:
            return snapshot.frames.size();
    }
}

struct CromElements::Context : CromContext {
};

CromElements::CromElements(const document::ProjectSnapshot& snapshot)
    : m_context(std::make_unique<Context>())
{
    static_cast<CromContext&>(*m_context) = makeCromContext(snapshot);
}

CromElements::~CromElements() = default;

void CromElements::write(CromBlock block, std::size_t index, std::vector<uint8_t>& out) const
{
    Writer writer;
    writer.buffer().swap(out);
    writer.buffer().clear();
    writeCromElement(writer, *m_context, block, index);
    writer.buffer().swap(out);
}

bool writeLegacyCrom(std::ostream& stream,
                     const document::ProjectSnapshot& snapshot,
                     std::string& errorMessage,
                     LegacySectionOffsets* sections,
                     LegacyIoProgress* progress)
{
    try {
        return writeCromContent(stream, snapshot, errorMessage, sections, progress);
    } catch (const WriteCancelled&) {
        errorMessage = kLegacyIoCancelledMessage;
        return false;
    }
}

uint64_t legacyCromByteCount(const document::ProjectSnapshot& snapshot)
{
    const uint64_t frames = snapshot.frames.size();
    const uint64_t sd = pixelCount(snapshot.geometry.planeSize(render::Resolution::Sd));
    const uint64_t hd = pixelCount(snapshot.geometry.planeSize(render::Resolution::Hd));
    const uint64_t rom = pixelCount(document::originalFrameSize(snapshot));
    const uint64_t colors = snapshot.colorCount;
    const uint64_t masks = snapshot.comparisonMasks.size();
    const uint64_t sprites = snapshot.sprites.size();
    const uint64_t backgrounds = snapshot.backgrounds.size();
    const uint64_t spritePixels = document::kMaxSpriteWidth * document::kMaxSpriteHeight;
    const uint64_t slots = document::kSpritesPerFrame;
    const uint64_t idBytes = 2;
    const uint64_t sets = document::kMaxDynamicSetsPerFrame;
    const uint64_t rotations = document::kMaxColorRotations * document::kMaxColorRotationLength;
    const uint64_t areas = document::kMaxSpriteDetectAreas;
    // The 256x64 flag, the format version and the slots behind the counts.
    const uint64_t header = kRomNameBytes + 9 * 4 + 2 + 4 + 8;
    const uint64_t perFrame = 4 + 1 + 1 + 1 + 2 * sd + 2 * hd + sd + hd + 2 * 2 * sets * colors +
                              idBytes * slots + 1 + 2 * 2 * rotations + 4 + 8 * slots + 2 + sd +
                              hd + sets + 2 * sets + sets + 2 * sets;
    const uint64_t perSprite = 1 + spritePixels + 2 * spritePixels + spritePixels +
                               2 * spritePixels + 4 * areas + 2 * areas + 8 * areas +
                               2 * 2 * document::kMaxDynamicSetsPerSprite * colors +
                               2 * spritePixels + 1;
    const uint64_t perBackground = 1 + 2 * sd + 2 * hd;
    return header + frames * perFrame + masks * rom + sprites * perSprite +
           backgrounds * perBackground + cromSettingsBlock(snapshot).size();
}

std::vector<uint8_t> cromSettingsBlock(const document::ProjectSnapshot& snapshot)
{
    std::vector<uint8_t> block;
    const document::SpeakerLightSettings& lights = snapshot.speakerLights;
    std::size_t framesWithLights = 0;
    for (const auto& frame : snapshot.frames) {
        framesWithLights += document::changesNothing(frame.lights) ? 0 : 1;
    }
    const bool withLights = !lights.empty() || framesWithLights > 0;
    std::size_t framesWithOriginalOnlySets = 0;
    for (const auto& frame : snapshot.frames) {
        framesWithOriginalOnlySets += frame.originalOnlyDynamicSets != 0 ? 1 : 0;
    }
    const bool withOriginalOnlySets = framesWithOriginalOnlySets > 0;
    if (!snapshot.colorizationSettings && !withLights && !withOriginalOnlySets) {
        return block;
    }
    const auto put = [&block](uint64_t value, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; ++i) {
            block.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
        }
    };
    const document::Size rom = document::originalFrameSize(snapshot);
    const bool withScenes =
        snapshot.colorizationSettings && rom.width == 128 && rom.height == 32;
    const bool withScaling = snapshot.colorizationSettings.has_value();
    put(kCromSettingsMagic, 4);
    put(0, 4);  // the payload size, set below
    put((withScenes ? kCromSettingsScenes : 0) | (withScaling ? kCromSettingsScaling : 0) |
            (withLights ? kCromSettingsLights : 0) |
            (withOriginalOnlySets ? kCromSettingsOriginalOnlySets : 0),
        4);
    if (withScenes) {
        const document::ColorizationSettings& settings = *snapshot.colorizationSettings;
        put(settings.scenes.size(), 4);
        for (const document::SceneDefinition& scene : settings.scenes) {
            put(scene.sceneId, 2);
            put(scene.frameCount, 2);
            put(scene.durationMs, 2);
            put(scene.interruptable ? 1 : 0, 1);
            put(scene.immediateStart ? 1 : 0, 1);
            put(scene.repeat, 1);
            put(scene.frameGroups == 0 ? 1 : scene.frameGroups, 1);
            put(scene.random ? 1 : 0, 1);
            put(scene.autoStartSeconds, 1);
            put(scene.options, 1);
        }
    }
    if (withScaling) {
        put(snapshot.colorizationSettings->scalingAlgorithm, 1);
        put(snapshot.colorizationSettings->shadowOffset, 1);
    }
    if (withLights) {
        // The effects by their position; actions name an effect by it.
        put(lights.effects.size(), 4);
        for (const document::LightEffect& effect : lights.effects) {
            put(effect.mode, 1);
            put(effect.options, 1);
            put(effect.speed, 2);
            put(effect.durationMs, 4);
            for (uint32_t color : effect.colors) {
                put(color, 4);
            }
            put(effect.brightness, 1);
            put(0, 3);
        }
        const auto putAction = [&](const document::LightAction& action) {
            uint16_t index = 0;
            for (std::size_t i = 0; i < lights.effects.size(); ++i) {
                if (lights.effects[i].id == action.effectId) {
                    index = static_cast<uint16_t>(i);
                }
            }
            put(static_cast<uint8_t>(action.kind), 1);
            put(0, 1);
            put(action.kind == document::LightActionKind::Effect ? index : 0, 2);
            put(action.kind == document::LightActionKind::Color ? action.color : 0, 4);
        };
        for (const document::LightAction& action : lights.defaults) {
            putAction(action);
        }
        put(framesWithLights, 4);
        for (std::size_t frame = 0; frame < snapshot.frames.size(); ++frame) {
            if (document::changesNothing(snapshot.frames[frame].lights)) {
                continue;
            }
            put(frame, 4);
            for (const document::LightAction& action : snapshot.frames[frame].lights) {
                putAction(action);
            }
        }
    }
    if (withOriginalOnlySets) {
        put(framesWithOriginalOnlySets, 4);
        for (std::size_t frame = 0; frame < snapshot.frames.size(); ++frame) {
            if (snapshot.frames[frame].originalOnlyDynamicSets != 0) {
                put(frame, 4);
                put(snapshot.frames[frame].originalOnlyDynamicSets, 4);
            }
        }
    }
    const uint64_t payload = block.size() - 8;
    for (std::size_t i = 0; i < 4; ++i) {
        block[4 + i] = static_cast<uint8_t>((payload >> (8 * i)) & 0xff);
    }
    return block;
}

}  // namespace colorizing::legacy
