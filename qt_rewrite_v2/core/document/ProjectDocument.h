#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document/EditorMetadata.h"
#include "render/CanvasTypes.h"

namespace colorizing::document {

using render::Resolution;
using render::Size;

struct Rgb565Frame {
    Size size;
    std::vector<uint16_t> pixels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t pixelCount() const;
};

struct IndexedFrame {
    Size size;
    std::vector<uint8_t> pixels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t pixelCount() const;
};

constexpr std::size_t kMaxDynamicSetsPerFrame = 32;
constexpr std::size_t kMaxDynamicSetsPerSprite = 9;
constexpr std::size_t kMaxSpritesPerFrame = 32;
constexpr std::size_t kMaxSpriteWidth = 256;
constexpr std::size_t kMaxSpriteHeight = 64;
constexpr std::size_t kMaxSpriteDetectAreas = 4;
constexpr std::size_t kMaxColorRotations = 4;
constexpr std::size_t kMaxColorRotationLength = 64;
constexpr std::size_t kMaxColorRotationColors = kMaxColorRotationLength - 2;
constexpr std::size_t kPaletteCount = 256;
constexpr std::size_t kPaletteColorCount = 64;

struct FrameDynamicData {
    IndexedFrame maskSd;
    IndexedFrame maskHd;
    std::vector<uint16_t> colorsSd;
    std::vector<uint16_t> colorsHd;
    std::vector<uint8_t> shadowDirectionsSd;
    std::vector<uint16_t> shadowColorsSd;
    std::vector<uint8_t> shadowDirectionsHd;
    std::vector<uint16_t> shadowColorsHd;
};

struct BackgroundRecord {
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    // HD pixels the Win32 editor left in the file although HD is switched off. They are kept so
    // an unedited project saves back unchanged, and dropped once HD is created or removed here.
    std::optional<Rgb565Frame> inactiveHd;
};

struct FrameBackgroundData {
    uint16_t backgroundId = 0xffff;
    IndexedFrame maskSd;
    IndexedFrame maskHd;
};

struct SpriteDetectionArea {
    uint16_t x = 0xffff;
    uint16_t y = 0xffff;
    uint16_t width = 0xffff;
    uint16_t height = 0xffff;
};

struct SpriteRecord {
    bool hasHd = false;
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    IndexedFrame maskHd;
    Rgb565Frame colorizedHd;
    std::vector<uint32_t> detectionDwords;
    std::vector<uint16_t> detectionDwordPositions;
    std::vector<SpriteDetectionArea> detectionAreas;
    std::string name;
    // Sprite dynamic content: 255 in the mask means "not dynamic". Empty containers are
    // normalized to that default when a document is built.
    IndexedFrame dynamicMaskSd;
    IndexedFrame dynamicMaskHd;
    std::vector<uint16_t> dynamicColorsSd;
    std::vector<uint16_t> dynamicColorsHd;
    uint8_t shapeMode = 0;
};

struct FrameSpriteReference {
    uint8_t spriteId = 255;
    uint16_t minX = 0;
    uint16_t minY = 0;
    uint16_t maxX = 0;
    uint16_t maxY = 0;
};

struct FrameRecord {
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    uint8_t shapeComparisonMode = 0;
    uint8_t comparisonMaskId = 0;
    std::optional<FrameDynamicData> dynamicData;
    std::optional<FrameBackgroundData> backgroundData;
    std::vector<FrameSpriteReference> spriteReferences;
    uint32_t triggerId = 0;
    uint32_t durationMs = 0;
    std::vector<uint16_t> colorRotationsSd;
    std::vector<uint16_t> colorRotationsHd;
    // See BackgroundRecord::inactiveHd.
    std::optional<Rgb565Frame> inactiveHd;
};

struct UnsupportedLegacyPayload {
    std::string name;
    std::vector<uint8_t> bytes;
};

struct PaletteRecord {
    std::vector<uint16_t> colors;
    std::string name;
};

struct ProjectCreateOptions {
    std::string romName;
    uint32_t frameCount = 0;
    bool createHdFrames = false;
    uint32_t colorCount = 4;
};

struct ProjectSnapshot;

class ProjectDocument {
public:
    [[nodiscard]] static ProjectDocument createEmpty(const ProjectCreateOptions& options);
    [[nodiscard]] static ProjectDocument fromSnapshot(const ProjectSnapshot& snapshot);

    [[nodiscard]] const std::string& romName() const;
    void setRomName(std::string name);

    [[nodiscard]] std::size_t frameCount() const;
    [[nodiscard]] uint32_t colorCount() const;
    // Size of the original frames and comparison masks: 256x64 for such ROMs, else 128x32.
    [[nodiscard]] Size originalFrameSize() const;
    [[nodiscard]] const EditorMetadata& editorMetadata() const;
    [[nodiscard]] bool isDirty() const;
    void markClean();

    [[nodiscard]] bool hasHdFrame(std::size_t frameIndex) const;
    void createHdFrame(std::size_t frameIndex);
    void removeHdFrame(std::size_t frameIndex);
    void upscaleDynamicMaskToHd(std::size_t frameIndex);
    void upscaleBackgroundMaskToHd(std::size_t frameIndex);
    void upscaleAssignedBackgroundToHd(std::size_t frameIndex);

    [[nodiscard]] const IndexedFrame& originalFrame(std::size_t frameIndex) const;
    [[nodiscard]] const Rgb565Frame& colorizedFrame(std::size_t frameIndex,
                                                    Resolution resolution) const;
    [[nodiscard]] uint8_t shapeComparisonMode(std::size_t frameIndex) const;
    void setShapeComparisonMode(std::size_t frameIndex, uint8_t mode);
    [[nodiscard]] uint8_t comparisonMaskId(std::size_t frameIndex) const;
    void setComparisonMaskId(std::size_t frameIndex, uint8_t maskId);
    [[nodiscard]] uint32_t triggerId(std::size_t frameIndex) const;
    void setTriggerId(std::size_t frameIndex, uint32_t triggerId);
    [[nodiscard]] uint32_t frameDurationMs(std::size_t frameIndex) const;
    void setFrameDurationMs(std::size_t frameIndex, uint32_t durationMs);
    [[nodiscard]] const std::vector<uint16_t>& colorRotations(std::size_t frameIndex,
                                                              Resolution resolution) const;
    [[nodiscard]] uint16_t colorRotationLength(std::size_t frameIndex,
                                               Resolution resolution,
                                               std::size_t slotIndex) const;
    [[nodiscard]] uint16_t colorRotationDelayMs(std::size_t frameIndex,
                                                Resolution resolution,
                                                std::size_t slotIndex) const;
    [[nodiscard]] uint16_t colorRotationColor(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t slotIndex,
                                              std::size_t colorIndex) const;
    void setColorRotationLength(std::size_t frameIndex,
                                Resolution resolution,
                                std::size_t slotIndex,
                                uint16_t length);
    void setColorRotationDelayMs(std::size_t frameIndex,
                                 Resolution resolution,
                                 std::size_t slotIndex,
                                 uint16_t delayMs);
    void setColorRotationColor(std::size_t frameIndex,
                               Resolution resolution,
                               std::size_t slotIndex,
                               std::size_t colorIndex,
                               uint16_t rgb565);
    void clearColorRotationSlot(std::size_t frameIndex,
                                Resolution resolution,
                                std::size_t slotIndex);
    [[nodiscard]] std::size_t comparisonMaskCount() const;
    [[nodiscard]] const IndexedFrame& comparisonMask(std::size_t maskIndex) const;
    [[nodiscard]] uint8_t comparisonMaskPixel(std::size_t maskIndex, int x, int y) const;
    void setComparisonMaskPixel(std::size_t maskIndex, int x, int y, uint8_t value);
    [[nodiscard]] const std::optional<FrameDynamicData>& dynamicData(std::size_t frameIndex) const;
    [[nodiscard]] uint8_t dynamicMaskPixel(std::size_t frameIndex,
                                           Resolution resolution,
                                           int x,
                                           int y) const;
    void setDynamicMaskPixel(std::size_t frameIndex,
                             Resolution resolution,
                             int x,
                             int y,
                             uint8_t value);
    [[nodiscard]] uint16_t dynamicColor(std::size_t frameIndex,
                                        Resolution resolution,
                                        std::size_t setIndex,
                                        std::size_t originalColorIndex) const;
    void setDynamicColor(std::size_t frameIndex,
                         Resolution resolution,
                         std::size_t setIndex,
                         std::size_t originalColorIndex,
                         uint16_t rgb565);
    [[nodiscard]] uint8_t dynamicShadowDirection(std::size_t frameIndex,
                                                 Resolution resolution,
                                                 std::size_t setIndex) const;
    [[nodiscard]] uint16_t dynamicShadowColor(std::size_t frameIndex,
                                              Resolution resolution,
                                              std::size_t setIndex) const;
    void setDynamicShadowDirection(std::size_t frameIndex,
                                   Resolution resolution,
                                   std::size_t setIndex,
                                   uint8_t direction);
    void setDynamicShadowColor(std::size_t frameIndex,
                               Resolution resolution,
                               std::size_t setIndex,
                               uint16_t rgb565);
    [[nodiscard]] std::size_t backgroundCount() const;
    [[nodiscard]] const BackgroundRecord& background(std::size_t backgroundIndex) const;
    [[nodiscard]] const Rgb565Frame& backgroundFrame(std::size_t backgroundIndex,
                                                     Resolution resolution) const;
    void setBackgroundFrame(std::size_t backgroundIndex,
                            Resolution resolution,
                            Rgb565Frame frame);
    void removeBackgroundHdFrame(std::size_t backgroundIndex);
    [[nodiscard]] uint16_t backgroundPixel(std::size_t backgroundIndex,
                                           Resolution resolution,
                                           int x,
                                           int y) const;
    void setBackgroundPixel(std::size_t backgroundIndex,
                            Resolution resolution,
                            int x,
                            int y,
                            uint16_t rgb565);
    [[nodiscard]] const std::optional<FrameBackgroundData>& backgroundData(
        std::size_t frameIndex) const;
    [[nodiscard]] uint16_t backgroundId(std::size_t frameIndex) const;
    void setBackgroundId(std::size_t frameIndex, uint16_t backgroundId);
    [[nodiscard]] uint8_t backgroundMaskPixel(std::size_t frameIndex,
                                              Resolution resolution,
                                              int x,
                                              int y) const;
    void setBackgroundMaskPixel(std::size_t frameIndex,
                                Resolution resolution,
                                int x,
                                int y,
                                uint8_t value);

    [[nodiscard]] std::size_t spriteCount() const;
    [[nodiscard]] const SpriteRecord& sprite(std::size_t spriteIndex) const;
    void setSpriteName(std::size_t spriteIndex, std::string name);
    [[nodiscard]] std::size_t paletteCount() const;
    [[nodiscard]] const PaletteRecord& palette(std::size_t paletteIndex) const;
    [[nodiscard]] uint16_t paletteColor(std::size_t paletteIndex,
                                        std::size_t colorIndex) const;
    void setPaletteColor(std::size_t paletteIndex, std::size_t colorIndex, uint16_t rgb565);
    void setPaletteName(std::size_t paletteIndex, std::string name);
    [[nodiscard]] bool hasHdSprite(std::size_t spriteIndex) const;
    void createHdSprite(std::size_t spriteIndex);
    void removeHdSprite(std::size_t spriteIndex);
    [[nodiscard]] uint8_t spriteOriginalPixel(std::size_t spriteIndex,
                                              int x,
                                              int y) const;
    void replaceSpriteFromFrame(std::size_t spriteIndex,
                                const IndexedFrame& originalFrame,
                                const Rgb565Frame& colorizedFrame,
                                int sourceX,
                                int sourceY,
                                int width,
                                int height);
    void replaceSpriteFromFrameSelection(std::size_t spriteIndex,
                                         const IndexedFrame& originalFrame,
                                         const Rgb565Frame& colorizedFrame,
                                         const std::vector<render::Point>& sourcePixels);
    void setSpriteOriginalPixel(std::size_t spriteIndex,
                                int x,
                                int y,
                                uint8_t value);
    [[nodiscard]] uint16_t spriteColorizedPixel(std::size_t spriteIndex,
                                                Resolution resolution,
                                                int x,
                                                int y) const;
    void setSpriteColorizedPixel(std::size_t spriteIndex,
                                 Resolution resolution,
                                 int x,
                                 int y,
                                 uint16_t rgb565);
    void setSpriteDetectionArea(std::size_t spriteIndex,
                                std::size_t areaIndex,
                                SpriteDetectionArea area);
    [[nodiscard]] const std::vector<FrameSpriteReference>& frameSpriteReferences(
        std::size_t frameIndex) const;
    void setFrameSpriteReference(std::size_t frameIndex,
                                 std::size_t slotIndex,
                                 FrameSpriteReference reference);
    void clearFrameSpriteReference(std::size_t frameIndex, std::size_t slotIndex);

    void setColorizedPixel(std::size_t frameIndex,
                           Resolution resolution,
                           int x,
                           int y,
                           uint16_t rgb565);
    [[nodiscard]] uint16_t colorizedPixel(std::size_t frameIndex,
                                          Resolution resolution,
                                          int x,
                                          int y) const;

    void addUnsupportedPayload(UnsupportedLegacyPayload payload);
    [[nodiscard]] const std::vector<UnsupportedLegacyPayload>& unsupportedPayloads() const;
    [[nodiscard]] ProjectSnapshot toSnapshot() const;

private:
    std::string m_romName;
    std::vector<FrameRecord> m_frames;
    std::vector<IndexedFrame> m_comparisonMasks;
    std::vector<BackgroundRecord> m_backgrounds;
    std::vector<SpriteRecord> m_sprites;
    std::vector<PaletteRecord> m_palettes;
    std::vector<UnsupportedLegacyPayload> m_unsupportedPayloads;
    EditorMetadata m_editorMetadata = makeDefaultEditorMetadata();
    uint32_t m_colorCount = 4;
    uint32_t m_cromRevision = 20;
    bool m_originalIs256x64 = false;
    bool m_dirty = false;
};

[[nodiscard]] Rgb565Frame makeRgb565Frame(Size size, uint16_t fill = 0);
[[nodiscard]] IndexedFrame makeIndexedFrame(Size size, uint8_t fill = 0);

}  // namespace colorizing::document
