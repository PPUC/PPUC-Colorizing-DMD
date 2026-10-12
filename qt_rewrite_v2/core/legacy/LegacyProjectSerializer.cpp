#include "legacy/LegacyProjectSerializer.h"

#include <algorithm>
#include <array>

#include "render/CanvasLayout.h"

namespace colorizing::legacy {

namespace {

constexpr std::size_t kRomNameBytes = 64;
constexpr std::size_t kFlushThresholdBytes = 1 << 20;

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

class Writer {
public:
    Writer(std::ostream& stream, LegacySectionOffsets* sections)
        : m_stream(stream)
        , m_sections(sections)
    {
    }

    void section(const char* name)
    {
        if (m_sections) {
            m_sections->emplace_back(name, m_offset);
        }
    }

    void u8(uint8_t value)
    {
        m_buffer.push_back(value);
        advance(1);
    }

    void u16(uint16_t value)
    {
        m_buffer.push_back(static_cast<uint8_t>(value & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        advance(2);
    }

    void u32(uint32_t value)
    {
        m_buffer.push_back(static_cast<uint8_t>(value & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        m_buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
        advance(4);
    }

    void bytes(const std::vector<uint8_t>& values)
    {
        m_buffer.insert(m_buffer.end(), values.begin(), values.end());
        advance(values.size());
    }

    void u16s(const std::vector<uint16_t>& values)
    {
        for (uint16_t value : values) {
            u16(value);
        }
    }

    void u32s(const std::vector<uint32_t>& values)
    {
        for (uint32_t value : values) {
            u32(value);
        }
    }

    void repeatU8(uint8_t value, std::size_t count)
    {
        m_buffer.insert(m_buffer.end(), count, value);
        advance(count);
    }

    void repeatU16(uint16_t value, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i) {
            u16(value);
        }
    }

    // Writes the vector, or `count` copies of `fallback` when it was never populated.
    void bytesOr(const std::vector<uint8_t>& values, std::size_t count, uint8_t fallback)
    {
        if (values.size() == count) {
            bytes(values);
        } else {
            repeatU8(fallback, count);
        }
    }

    void u16sOr(const std::vector<uint16_t>& values, std::size_t count, uint16_t fallback)
    {
        if (values.size() == count) {
            u16s(values);
        } else {
            repeatU16(fallback, count);
        }
    }

    void name(const std::string& value, std::size_t length)
    {
        for (std::size_t i = 0; i < length; ++i) {
            u8(i + 1 < length && i < value.size() ? static_cast<uint8_t>(value[i]) : uint8_t{0});
        }
    }

    [[nodiscard]] bool finish()
    {
        flush();
        m_stream.flush();
        return static_cast<bool>(m_stream);
    }

private:
    void advance(std::size_t count)
    {
        m_offset += count;
        if (m_buffer.size() >= kFlushThresholdBytes) {
            flush();
        }
    }

    void flush()
    {
        m_stream.write(reinterpret_cast<const char*>(m_buffer.data()),
                       static_cast<std::streamsize>(m_buffer.size()));
        m_buffer.clear();
    }

    std::ostream& m_stream;
    LegacySectionOffsets* m_sections = nullptr;
    std::vector<uint8_t> m_buffer;
    std::size_t m_offset = 0;
};

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
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        writer.u16(dynamicMask->pixels[i] < 255 ? uint16_t{0} : frame.pixels[i]);
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

uint32_t legacyFrameHash(const document::IndexedFrame& original,
                         const document::IndexedFrame* comparisonMask,
                         bool shapeMode)
{
    static const std::array<uint32_t, 256> table = buildCrc32Table();
    uint32_t crc = 0xFFFFFFFF;
    for (std::size_t i = 0; i < original.pixels.size(); ++i) {
        if (comparisonMask && i < comparisonMask->pixels.size() &&
            comparisonMask->pixels[i] != 0) {
            continue;
        }
        uint8_t value = original.pixels[i];
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

bool isLegacyFrameActive(const document::ProjectFrameSnapshot& frame,
                         const std::vector<uint16_t>& originalColors)
{
    if (frame.durationMs >= kSkipFrameDurationMs) {
        return true;
    }
    // A 256x64 project renders its HD frame only, one to one with the original; its SD frame
    // is never shown and does not count.
    const bool hdOnly = frame.originalSd.size.width == render::kHdFrameSize.width;
    if (hdOnly && !frame.colorizedHd) {
        return false;
    }
    const document::Rgb565Frame& colorized = hdOnly ? *frame.colorizedHd : frame.colorizedSd;
    const document::IndexedFrame* dynamicMask =
        !frame.dynamicData ? nullptr
                           : hdOnly ? &frame.dynamicData->maskHd : &frame.dynamicData->maskSd;
    for (std::size_t index = 0; index < colorized.pixels.size(); ++index) {
        const uint8_t original = index < frame.originalSd.pixels.size()
                                     ? frame.originalSd.pixels[index]
                                     : uint8_t{0};
        const uint16_t plain =
            original < originalColors.size() ? originalColors[original] : uint16_t{0};
        // The saved frame has its dynamic pixels blanked; any dynamic pixel counts.
        const bool dynamic = dynamicMask && index < dynamicMask->pixels.size() &&
                             dynamicMask->pixels[index] != 255;
        if (dynamic || colorized.pixels[index] != plain) {
            return true;
        }
    }
    return false;
}

bool writeLegacyCrom(std::ostream& stream,
                     const document::ProjectSnapshot& snapshot,
                     std::string& errorMessage,
                     LegacySectionOffsets* sections)
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

    const render::Size sdSize = render::kSdFrameSize;
    const render::Size hdSize = render::kHdFrameSize;
    // Sprite bounds are original frame pixels.
    const render::Size originalSize = document::originalFrameSize(snapshot);
    const std::size_t sdPixels = pixelCount(sdSize);
    const std::size_t hdPixels = pixelCount(hdSize);
    const std::size_t spritePixels = document::kMaxSpriteWidth * document::kMaxSpriteHeight;
    const std::size_t dynamicColorCount =
        document::kMaxDynamicSetsPerFrame * static_cast<std::size_t>(snapshot.colorCount);
    const std::size_t spriteDynamicColorCount =
        document::kMaxDynamicSetsPerSprite * static_cast<std::size_t>(snapshot.colorCount);
    const std::size_t rotationValueCount =
        document::kMaxColorRotations * document::kMaxColorRotationLength;
    const std::size_t shadowCount = document::kMaxDynamicSetsPerFrame;
    const std::vector<uint16_t> originalColors = legacyOriginalColors(snapshot.colorCount);
    const auto& frames = snapshot.frames;

    Writer writer(stream, sections);
    writer.section("header");
    writer.name(snapshot.romName, kRomNameBytes);
    // Older revisions are written as 19, the oldest one that carries every block below.
    const uint32_t revision = std::clamp(snapshot.cromRevision,
                                         document::kOldestWritableCromRevision,
                                         document::kCurrentCromRevision);
    writer.u32(revision * static_cast<uint32_t>(sizeof(uint32_t)));
    writer.u32(static_cast<uint32_t>(sdSize.width));
    writer.u32(static_cast<uint32_t>(sdSize.height));
    writer.u32(static_cast<uint32_t>(hdSize.width));
    writer.u32(static_cast<uint32_t>(hdSize.height));
    writer.u32(static_cast<uint32_t>(frames.size()));
    writer.u32(snapshot.colorCount);
    writer.u32(static_cast<uint32_t>(snapshot.comparisonMasks.size()));
    writer.u32(static_cast<uint32_t>(snapshot.sprites.size()));
    writer.u16(static_cast<uint16_t>(snapshot.backgrounds.size()));
    if (revision >= 20) {
        writer.u32(snapshot.originalIs256x64 ? 1 : 0);
    }

    writer.section("hash codes");
    for (const auto& frame : frames) {
        // The Win32 editor only treats the exact value 1 as shape mode when hashing.
        writer.u32(legacyFrameHash(frame.originalSd,
                                   comparisonMaskForFrame(snapshot, frame),
                                   frame.shapeComparisonMode == 1));
    }
    writer.section("shape comparison modes");
    for (const auto& frame : frames) {
        writer.u8(frame.shapeComparisonMode);
    }
    writer.section("comparison mask IDs");
    for (const auto& frame : frames) {
        writer.u8(frame.comparisonMaskId);
    }
    writer.section("comparison masks");
    for (const auto& mask : snapshot.comparisonMasks) {
        writer.bytes(mask.pixels);
    }
    writer.section("extra frame flags");
    for (const auto& frame : frames) {
        writer.u8(frame.colorizedHd ? 1 : 0);
    }
    writer.section("SD colorized frames");
    for (const auto& frame : frames) {
        writeColorizedUnderDynamicMask(writer,
                                       frame.colorizedSd,
                                       frame.dynamicData ? &frame.dynamicData->maskSd : nullptr);
    }
    writer.section("HD colorized frames");
    for (const auto& frame : frames) {
        if (frame.colorizedHd) {
            writeColorizedUnderDynamicMask(
                writer,
                *frame.colorizedHd,
                frame.dynamicData ? &frame.dynamicData->maskHd : nullptr);
        } else if (frame.inactiveHd) {
            writer.u16s(frame.inactiveHd->pixels);
        } else {
            writer.repeatU16(0, hdPixels);
        }
    }
    writer.section("SD dynamic masks");
    for (const auto& frame : frames) {
        if (frame.dynamicData) {
            writer.bytes(frame.dynamicData->maskSd.pixels);
        } else {
            writer.repeatU8(255, sdPixels);
        }
    }
    writer.section("HD dynamic masks");
    for (const auto& frame : frames) {
        if (frame.dynamicData) {
            writer.bytes(frame.dynamicData->maskHd.pixels);
        } else {
            writer.repeatU8(255, hdPixels);
        }
    }
    writer.section("SD dynamic colors");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.dynamicData ? frame.dynamicData->colorsSd : std::vector<uint16_t>{},
                      dynamicColorCount,
                      0);
    }
    writer.section("HD dynamic colors");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.dynamicData ? frame.dynamicData->colorsHd : std::vector<uint16_t>{},
                      dynamicColorCount,
                      0);
    }
    writer.section("extra sprite flags");
    for (const auto& sprite : snapshot.sprites) {
        writer.u8(sprite.hasHd ? 1 : 0);
    }
    writer.section("frame sprites");
    for (const auto& frame : frames) {
        for (std::size_t slot = 0; slot < document::kMaxSpritesPerFrame; ++slot) {
            writer.u8(slot < frame.spriteReferences.size() ? frame.spriteReferences[slot].spriteId
                                                           : uint8_t{255});
        }
    }
    writer.section("sprite originals");
    for (const auto& sprite : snapshot.sprites) {
        writer.bytes(sprite.originalSd.pixels);
    }
    writer.section("SD sprite colors");
    for (const auto& sprite : snapshot.sprites) {
        writer.u16s(sprite.colorizedSd.pixels);
    }
    writer.section("HD sprite masks");
    for (const auto& sprite : snapshot.sprites) {
        writer.bytes(sprite.maskHd.pixels);
    }
    writer.section("HD sprite colors");
    for (const auto& sprite : snapshot.sprites) {
        writer.u16s(sprite.colorizedHd.pixels);
    }
    writer.section("active frame flags");
    for (const auto& frame : frames) {
        writer.u8(isLegacyFrameActive(frame, originalColors) ? 1 : 0);
    }
    writer.section("SD color rotations");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.colorRotationsSd, rotationValueCount, 0);
    }
    writer.section("HD color rotations");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.colorRotationsHd, rotationValueCount, 0);
    }
    writer.section("sprite detection dwords");
    for (const auto& sprite : snapshot.sprites) {
        writer.u32s(sprite.detectionDwords);
    }
    writer.section("sprite detection dword positions");
    for (const auto& sprite : snapshot.sprites) {
        writer.u16s(sprite.detectionDwordPositions);
    }
    writer.section("sprite detection areas");
    for (const auto& sprite : snapshot.sprites) {
        for (const auto& area : sprite.detectionAreas) {
            writer.u16(area.x);
            writer.u16(area.y);
            writer.u16(area.width);
            writer.u16(area.height);
        }
    }
    writer.section("trigger IDs");
    for (const auto& frame : frames) {
        writer.u32(frame.triggerId);
    }
    writer.section("frame sprite bounding boxes");
    for (const auto& frame : frames) {
        for (std::size_t slot = 0; slot < document::kMaxSpritesPerFrame; ++slot) {
            if (slot < frame.spriteReferences.size()) {
                const auto& reference = frame.spriteReferences[slot];
                writer.u16(reference.minX);
                writer.u16(reference.minY);
                writer.u16(reference.maxX);
                writer.u16(reference.maxY);
            } else {
                writer.u16(0);
                writer.u16(0);
                writer.u16(static_cast<uint16_t>(originalSize.width - 1));
                writer.u16(static_cast<uint16_t>(originalSize.height - 1));
            }
        }
    }
    writer.section("extra background flags");
    for (const auto& background : snapshot.backgrounds) {
        writer.u8(background.colorizedHd ? 1 : 0);
    }
    writer.section("SD backgrounds");
    for (const auto& background : snapshot.backgrounds) {
        writer.u16s(background.colorizedSd.pixels);
    }
    writer.section("HD backgrounds");
    for (const auto& background : snapshot.backgrounds) {
        if (background.colorizedHd) {
            writer.u16s(background.colorizedHd->pixels);
        } else if (background.inactiveHd) {
            writer.u16s(background.inactiveHd->pixels);
        } else {
            writer.repeatU16(0, hdPixels);
        }
    }
    writer.section("background IDs");
    for (const auto& frame : frames) {
        writer.u16(frame.backgroundData ? frame.backgroundData->backgroundId : uint16_t{0xffff});
    }
    // A frame that never had background data gets the editor's "whole frame" default mask.
    writer.section("SD background masks");
    for (const auto& frame : frames) {
        if (frame.backgroundData) {
            writer.bytes(frame.backgroundData->maskSd.pixels);
        } else {
            writer.repeatU8(1, sdPixels);
        }
    }
    writer.section("HD background masks");
    for (const auto& frame : frames) {
        if (frame.backgroundData) {
            writer.bytes(frame.backgroundData->maskHd.pixels);
        } else {
            writer.repeatU8(1, hdPixels);
        }
    }
    const std::vector<uint8_t> noDirections;
    const std::vector<uint16_t> noColors;
    writer.section("SD dynamic shadow directions");
    for (const auto& frame : frames) {
        writer.bytesOr(frame.dynamicData ? frame.dynamicData->shadowDirectionsSd : noDirections,
                       shadowCount,
                       0);
    }
    writer.section("SD dynamic shadow colors");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.dynamicData ? frame.dynamicData->shadowColorsSd : noColors,
                      shadowCount,
                      0);
    }
    writer.section("HD dynamic shadow directions");
    for (const auto& frame : frames) {
        writer.bytesOr(frame.dynamicData ? frame.dynamicData->shadowDirectionsHd : noDirections,
                       shadowCount,
                       0);
    }
    writer.section("HD dynamic shadow colors");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.dynamicData ? frame.dynamicData->shadowColorsHd : noColors,
                      shadowCount,
                      0);
    }
    writer.section("SD sprite dynamic colors");
    for (const auto& sprite : snapshot.sprites) {
        writer.u16sOr(sprite.dynamicColorsSd, spriteDynamicColorCount, 0);
    }
    writer.section("HD sprite dynamic colors");
    for (const auto& sprite : snapshot.sprites) {
        writer.u16sOr(sprite.dynamicColorsHd, spriteDynamicColorCount, 0);
    }
    writer.section("SD sprite dynamic masks");
    for (const auto& sprite : snapshot.sprites) {
        writer.bytesOr(sprite.dynamicMaskSd.pixels, spritePixels, 255);
    }
    writer.section("HD sprite dynamic masks");
    for (const auto& sprite : snapshot.sprites) {
        writer.bytesOr(sprite.dynamicMaskHd.pixels, spritePixels, 255);
    }
    writer.section("sprite shape modes");
    for (const auto& sprite : snapshot.sprites) {
        writer.u8(sprite.shapeMode);
    }
    writer.section("end");

    if (!writer.finish()) {
        errorMessage = "could not write cROM data";
        return false;
    }
    return true;
}

bool writeLegacyCrp(std::ostream& stream,
                    const document::ProjectSnapshot& snapshot,
                    std::string& errorMessage,
                    LegacySectionOffsets* sections)
{
    if (!document::isValidProjectSnapshot(snapshot, &errorMessage)) {
        return false;
    }

    // Sprite and palette names are edited through the model; everything else is carried in the
    // metadata tables. Unchanged names keep their original bytes.
    document::EditorMetadata metadata = snapshot.editorMetadata;
    for (std::size_t i = 0; i < snapshot.sprites.size(); ++i) {
        document::setFixedName(metadata.spriteNames,
                               i,
                               document::kSpriteNameBytes,
                               snapshot.sprites[i].name);
    }
    const bool hasPalettes = snapshot.palettes.size() == document::kPaletteCount;
    for (std::size_t i = 0; hasPalettes && i < snapshot.palettes.size(); ++i) {
        document::setFixedName(metadata.paletteNames,
                               i,
                               document::kPaletteNameBytes,
                               snapshot.palettes[i].name);
    }

    Writer writer(stream, sections);
    writer.section("ROM name");
    writer.name(snapshot.romName, kRomNameBytes);
    writer.section("original frames");
    for (const auto& frame : snapshot.frames) {
        writer.bytes(frame.originalSd.pixels);
    }
    writer.section("color sets");
    writer.u32s(metadata.activeColorSets);
    writer.u16s(metadata.colorSets);
    writer.u8(metadata.currentColorSet);
    writer.u8(metadata.firstDisplayedColorSet);
    writer.bytes(metadata.colorSetNames);
    writer.section("draw settings");
    writer.u32(metadata.drawColorMode);
    writer.u8(metadata.drawMode);
    writer.u32(static_cast<uint32_t>(metadata.maskSelectionMode));
    writer.u32(metadata.fillMode);
    writer.section("mask names");
    writer.bytes(metadata.maskNames);
    writer.section("sections");
    writer.u32(metadata.sectionCount);
    writer.u32s(metadata.sectionFirstFrames);
    writer.bytes(metadata.sectionNames);
    writer.section("sprite names");
    writer.bytes(metadata.spriteNames);
    writer.section("sprite source frames");
    writer.u32s(metadata.spriteSourceFrames);
    writer.section("frame durations");
    for (const auto& frame : snapshot.frames) {
        writer.u32(frame.durationMs);
    }
    writer.section("sprite source rectangles");
    writer.u16s(metadata.spriteSourceRects);
    writer.u32s(metadata.spriteSourceMirrors);
    writer.section("palettes");
    for (std::size_t i = 0; i < document::kPaletteCount; ++i) {
        writer.u16sOr(hasPalettes ? snapshot.palettes[i].colors : std::vector<uint16_t>{},
                      document::kPaletteColorCount,
                      0);
    }
    writer.section("edit colors");
    writer.u16s(metadata.editColors);
    writer.section("image positions");
    writer.u32(metadata.imagePositionCount);
    writer.bytes(metadata.imagePositionNames);
    for (int32_t value : metadata.imagePositions) {
        writer.u32(static_cast<uint32_t>(value));
    }
    writer.section("palette names");
    writer.bytes(metadata.paletteNames);
    writer.section("imported palettes");
    writer.u32(static_cast<uint32_t>(metadata.importedPalettes.size() /
                                     document::kImportedPaletteBytes));
    writer.bytes(metadata.importedPalettes);
    writer.section("elapsed time");
    writer.u32(metadata.timeElapsedMs);
    writer.section("PuP pack");
    writer.u32(metadata.hasPupPack);
    writer.bytes(metadata.pupPackName);
    writer.section("end");

    if (!writer.finish()) {
        errorMessage = "could not write cRP data";
        return false;
    }
    return true;
}

}  // namespace colorizing::legacy
