#include "legacy/LegacyProjectSerializer.h"

#include <algorithm>
#include <array>

#include "legacy/LegacyProjectIo.h"
#include "render/CanvasLayout.h"

namespace colorizing::legacy {

namespace {

// The header revision a save writes: 21 for libserum 3.0 on (the format version and the sprite
// slots in the header); otherwise the revision the project was loaded with, older ones as 19,
// the oldest one that carries every block.
uint32_t writtenCromRevision(const document::ProjectSnapshot& snapshot)
{
    if (document::libserumLimits(snapshot.libserumTarget).cromFormatVersion >= 1) {
        return 21;
    }
    return std::clamp(snapshot.cromRevision, document::kOldestWritableCromRevision,
                      document::kCurrentCromRevision);
}

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

// Thrown by Writer when its progress asks to cancel; caught where the file is written.
struct WriteCancelled {
};

class Writer {
public:
    Writer(std::ostream& stream, LegacySectionOffsets* sections, LegacyIoProgress* progress)
        : m_stream(stream)
        , m_sections(sections)
        , m_progress(progress)
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

    template <typename Values>
    void bytes(const Values& values)
    {
        m_buffer.insert(m_buffer.end(), values.begin(), values.end());
        advance(values.size());
    }

    template <typename Values>
    void u16s(const Values& values)
    {
        const std::size_t start = m_buffer.size();
        m_buffer.resize(start + 2 * values.size());
        uint8_t* out = m_buffer.data() + start;
        for (const uint16_t value : values) {
            *out++ = static_cast<uint8_t>(value & 0xff);
            *out++ = static_cast<uint8_t>((value >> 8) & 0xff);
        }
        advance(2 * values.size());
    }

    template <typename Values>
    void u32s(const Values& values)
    {
        for (const uint32_t value : values) {
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
        const std::size_t start = m_buffer.size();
        m_buffer.resize(start + 2 * count);
        uint8_t* out = m_buffer.data() + start;
        for (std::size_t i = 0; i < count; ++i) {
            *out++ = static_cast<uint8_t>(value & 0xff);
            *out++ = static_cast<uint8_t>((value >> 8) & 0xff);
        }
        advance(2 * count);
    }

    // Writes the values, or `count` copies of `fallback` when they were never populated.
    template <typename Values>
    void bytesOr(const Values& values, std::size_t count, uint8_t fallback)
    {
        if (values.size() == count) {
            bytes(values);
        } else {
            repeatU8(fallback, count);
        }
    }

    template <typename Values>
    void u16sOr(const Values& values, std::size_t count, uint16_t fallback)
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
        if (m_progress) {
            m_progress->done.fetch_add(m_buffer.size());
            if (m_progress->cancel.load()) {
                m_buffer.clear();
                throw WriteCancelled{};
            }
        }
        m_buffer.clear();
    }

    std::ostream& m_stream;
    LegacySectionOffsets* m_sections = nullptr;
    LegacyIoProgress* m_progress = nullptr;
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
    const document::CompactArray<uint8_t> noDirections;
    const document::CompactArray<uint16_t> noColors;

    Writer writer(stream, sections, progress);
    writer.section("header");
    writer.name(snapshot.romName, kRomNameBytes);
    const uint32_t revision = writtenCromRevision(snapshot);
    const document::LibserumLimits limits = document::libserumLimits(snapshot.libserumTarget);
    const std::size_t slots = limits.spritesPerFrame;
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
        writer.u32(snapshot.geometry.original256x64Flag() ? 1 : 0);
    }
    // Revision 21 (libserum 3.0 on): the format version and the sprite slots of a frame.
    if (revision >= 21) {
        writer.u32(limits.cromFormatVersion);
        writer.u32(static_cast<uint32_t>(slots));
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
        writer.u16sOr(frame.dynamicData ? frame.dynamicData->colorsSd : noColors,
                      dynamicColorCount,
                      0);
    }
    writer.section("HD dynamic colors");
    for (const auto& frame : frames) {
        writer.u16sOr(frame.dynamicData ? frame.dynamicData->colorsHd : noColors,
                      dynamicColorCount,
                      0);
    }
    writer.section("extra sprite flags");
    for (const auto& sprite : snapshot.sprites) {
        writer.u8(sprite.hasHd ? 1 : 0);
    }
    writer.section("frame sprites");
    for (const auto& frame : frames) {
        for (std::size_t slot = 0; slot < slots; ++slot) {
            const uint16_t spriteId = slot < frame.spriteReferences.size()
                                          ? frame.spriteReferences[slot].spriteId
                                          : document::kNoSpriteId;
            if (limits.cromFormatVersion >= 1) {
                writer.u16(spriteId);
            } else {
                // 8-bit IDs, 255 for an empty slot.
                writer.u8(spriteId == document::kNoSpriteId ? uint8_t{255}
                                                            : static_cast<uint8_t>(spriteId));
            }
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
    const render::Resolution nativePlane = snapshot.geometry.nativePlane();
    for (const auto& frame : frames) {
        writer.u8(isLegacyFrameActive(frame, originalColors, nativePlane) ? 1 : 0);
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
        for (std::size_t slot = 0; slot < slots; ++slot) {
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
    writer.section("settings block");
    writer.bytes(cromSettingsBlock(snapshot));
    writer.section("end");

    if (!writer.finish()) {
        errorMessage = "could not write cROM data";
        return false;
    }
    return true;
}

bool writeCrpContent(std::ostream& stream,
                     const document::ProjectSnapshot& snapshot,
                     std::string& errorMessage,
                     LegacySectionOffsets* sections,
                     LegacyIoProgress* progress,
                     bool withPupPack)
{
    if (!document::isValidProjectSnapshot(snapshot, &errorMessage)) {
        return false;
    }

    // Sprite and palette names are edited through the model; everything else is carried in the
    // metadata tables. Unchanged names keep their original bytes.
    document::EditorMetadata metadata = snapshot.editorMetadata;
    document::ensureSpriteTableEntries(metadata, snapshot.sprites.size());
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

    Writer writer(stream, sections, progress);
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
    // The cRP has room for kCrpSpriteEntries sprites; the .cRPX keeps the entries beyond.
    document::truncateSpriteTableEntries(metadata, document::kCrpSpriteEntries);
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
    writer.u32(withPupPack ? metadata.hasPupPack : 0);
    writer.bytes(document::legacyPupPackNameBytes(withPupPack ? metadata.pupPackName
                                                              : std::string()));
    writer.section("end");

    if (!writer.finish()) {
        errorMessage = "could not write cRP data";
        return false;
    }
    return true;
}

}  // namespace

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

bool writeLegacyCrp(std::ostream& stream,
                    const document::ProjectSnapshot& snapshot,
                    std::string& errorMessage,
                    LegacySectionOffsets* sections,
                    LegacyIoProgress* progress)
{
    try {
        return writeCrpContent(stream, snapshot, errorMessage, sections, progress, true);
    } catch (const WriteCancelled&) {
        errorMessage = kLegacyIoCancelledMessage;
        return false;
    }
}

bool writeCrpxCrpPart(std::ostream& stream,
                      const document::ProjectSnapshot& snapshot,
                      std::string& errorMessage,
                      LegacySectionOffsets* sections,
                      LegacyIoProgress* progress)
{
    try {
        return writeCrpContent(stream, snapshot, errorMessage, sections, progress, false);
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
    const document::LibserumLimits limits = document::libserumLimits(snapshot.libserumTarget);
    const uint64_t slots = limits.spritesPerFrame;
    const uint64_t idBytes = limits.cromFormatVersion >= 1 ? 2 : 1;
    const uint64_t sets = document::kMaxDynamicSetsPerFrame;
    const uint64_t rotations = document::kMaxColorRotations * document::kMaxColorRotationLength;
    const uint64_t areas = document::kMaxSpriteDetectAreas;
    const uint32_t revision = writtenCromRevision(snapshot);
    const uint64_t header = kRomNameBytes + 9 * 4 + 2 + (revision >= 20 ? 4 : 0) +
                            (revision >= 21 ? 8 : 0);
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
    if (!snapshot.colorizationSettings) {
        return block;
    }
    const document::ColorizationSettings& settings = *snapshot.colorizationSettings;
    const auto put = [&block](uint64_t value, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; ++i) {
            block.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
        }
    };
    const document::Size rom = document::originalFrameSize(snapshot);
    const bool withScenes = rom.width == 128 && rom.height == 32;
    put(kCromSettingsMagic, 4);
    put(0, 4);  // the payload size, set below
    put(withScenes ? 3 : 2, 4);
    if (withScenes) {
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
    put(settings.scalingAlgorithm, 1);
    put(settings.shadowOffset, 1);
    const uint64_t payload = block.size() - 8;
    for (std::size_t i = 0; i < 4; ++i) {
        block[4 + i] = static_cast<uint8_t>((payload >> (8 * i)) & 0xff);
    }
    return block;
}

uint64_t legacyCrpByteCount(const document::ProjectSnapshot& snapshot)
{
    const document::EditorMetadata& metadata = snapshot.editorMetadata;
    const uint64_t frames = snapshot.frames.size();
    const uint64_t rom = pixelCount(document::originalFrameSize(snapshot));
    return kRomNameBytes + frames * rom + 4 * metadata.activeColorSets.size() +
           2 * metadata.colorSets.size() + 1 + 1 + metadata.colorSetNames.size() + 4 + 1 + 4 +
           4 + metadata.maskNames.size() + 4 + 4 * metadata.sectionFirstFrames.size() +
           metadata.sectionNames.size() +
           document::kCrpSpriteEntries * (document::kSpriteNameBytes + 4 + 2 * 4 + 4 * 2) +
           4 * frames +
           2 * document::kPaletteCount * document::kPaletteColorCount +
           2 * metadata.editColors.size() + 4 + metadata.imagePositionNames.size() +
           4 * metadata.imagePositions.size() + metadata.paletteNames.size() + 4 +
           metadata.importedPalettes.size() + 4 + 4 + document::kPupPackNameBytes;
}

}  // namespace colorizing::legacy
