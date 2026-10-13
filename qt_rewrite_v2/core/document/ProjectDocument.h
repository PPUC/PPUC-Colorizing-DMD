#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document/EditorMetadata.h"
#include "render/CanvasTypes.h"
#include "render/ProjectGeometry.h"

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
// Trigger ID of a frame that triggers nothing, as stored in the cROM.
constexpr uint32_t kNoTriggerId = 0xffffffff;

// The names of the record fields follow the stored planes (render::Resolution): ...Sd is the
// original plane and ...Hd the extra plane, whatever their sizes are in the project's
// render::ProjectGeometry. originalSd is the ROM frame.
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
    uint32_t triggerId = kNoTriggerId;
    uint32_t durationMs = 0;
    std::vector<uint16_t> colorRotationsSd;
    std::vector<uint16_t> colorRotationsHd;
    // See BackgroundRecord::inactiveHd.
    std::optional<Rgb565Frame> inactiveHd;
};

// Which related HD data Create HD upscales besides the frame itself. Dynamic data is not
// among them: the runtime takes it from the original plane, see ProjectDocument::dynamicPlane().
struct FrameHdParts {
    bool backgroundMask = false;
    bool backgroundImage = false;
};

struct BackgroundHdData {
    std::size_t backgroundIndex = 0;
    std::optional<Rgb565Frame> colorizedHd;
    std::optional<Rgb565Frame> inactiveHd;
};

// The HD data Create HD and Remove HD replace, so that it can be put back exactly. The
// optional parts are only filled for the FrameHdParts that were asked for.
struct FrameHdState {
    std::optional<Rgb565Frame> colorizedHd;
    std::optional<Rgb565Frame> inactiveHd;
    std::optional<IndexedFrame> backgroundMask;
    // The background that was assigned when the state was taken.
    std::optional<BackgroundHdData> background;
};

struct SpriteHdState {
    bool hasHd = false;
    IndexedFrame maskHd;
    Rgb565Frame colorizedHd;
};

// Missing HD data of an HD-only project. The runtime shows a frame only if the frame, its
// background and every sprite assigned to it have HD data.
struct FrameHdGaps {
    bool frame = false;
    bool background = false;
    // Sprite IDs, ascending and unique.
    std::vector<uint8_t> sprites;

    [[nodiscard]] bool any() const { return frame || background || !sprites.empty(); }
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

// What the IDs of a subset snapshot stand for: subset index -> document index.
struct SnapshotSubsetMap {
    std::vector<std::size_t> frames;
    std::vector<std::size_t> comparisonMasks;
    std::vector<std::size_t> backgrounds;
    std::vector<std::size_t> sprites;
};

class ProjectDocument {
public:
    [[nodiscard]] static ProjectDocument createEmpty(const ProjectCreateOptions& options);
    [[nodiscard]] static ProjectDocument fromSnapshot(const ProjectSnapshot& snapshot);

    [[nodiscard]] const std::string& romName() const;
    void setRomName(std::string name);

    [[nodiscard]] std::size_t frameCount() const;
    [[nodiscard]] uint32_t colorCount() const;
    [[nodiscard]] const render::ProjectGeometry& geometry() const;
    // Size of the ROM frame: of the original frames and the comparison masks.
    [[nodiscard]] Size originalFrameSize() const;
    // A project with 256x64 original frames (PlaneLayout::NativeExtra). The runtime renders
    // its HD plane only, one to one with the original; the SD data of frames, backgrounds and
    // sprites is stored and written back but never shown or edited. Sprite originals,
    // detection areas and frame sprite bounds are original frame pixels in every project.
    [[nodiscard]] bool rendersHdPlaneOnly() const;
    // A project without an editable extra plane (PlaneLayout::NativeOriginal, 192x64 with a
    // stored 96x32 plane): only the original plane is shown and edited. The 96x32 data of a
    // snapshot is discarded by fromSnapshot() and not held here; toSnapshot() returns it as
    // "no extra data" (discardExtraPlane()). No frame, background or sprite has HD, HD data
    // cannot be created, and every access to Resolution::Hd throws std::logic_error.
    [[nodiscard]] bool editsOriginalPlaneOnly() const;
    // What keeps the runtime from showing the frame; nothing for other projects.
    [[nodiscard]] FrameHdGaps frameHdGaps(std::size_t frameIndex) const;
    [[nodiscard]] const EditorMetadata& editorMetadata() const;
    [[nodiscard]] bool isDirty() const;
    void markClean();
    // Goes up with every change and is not used by another document's content in this
    // process, so that data derived from a document can tell it is stale.
    [[nodiscard]] uint64_t revision() const;
    // The revision at which what frame identification depends on last changed: the original
    // frames, the comparison masks and each frame's mask and shape mode.
    [[nodiscard]] uint64_t identificationRevision() const;

    [[nodiscard]] bool hasHdFrame(std::size_t frameIndex) const;
    // Upscales the SD frame. In an HD-only project there is nothing to upscale: the HD frame
    // becomes the original in its plain colors (plainOriginalColors).
    void createHdFrame(std::size_t frameIndex);
    // Removing and upscaling from SD throw std::logic_error in an HD-only project.
    void removeHdFrame(std::size_t frameIndex);
    void upscaleBackgroundMaskToHd(std::size_t frameIndex);
    // In an HD-only project: gives the assigned background a black HD image if it has none.
    void upscaleAssignedBackgroundToHd(std::size_t frameIndex);
    // Throws when a requested part does not exist for the frame.
    [[nodiscard]] FrameHdState frameHdState(std::size_t frameIndex, FrameHdParts parts) const;
    // Writes back everything the state holds; a state that does not fit the frame is rejected
    // without changing anything.
    void setFrameHdState(std::size_t frameIndex, const FrameHdState& state);

    [[nodiscard]] const IndexedFrame& originalFrame(std::size_t frameIndex) const;
    [[nodiscard]] const Rgb565Frame& colorizedFrame(std::size_t frameIndex,
                                                    Resolution resolution) const;
    [[nodiscard]] uint8_t shapeComparisonMode(std::size_t frameIndex) const;
    void setShapeComparisonMode(std::size_t frameIndex, uint8_t mode);
    // Puts back a mode a file was loaded with, including the unknown legacy values that
    // setShapeComparisonMode rejects.
    void restoreShapeComparisonMode(std::size_t frameIndex, uint8_t mode);
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
    // The plane whose dynamic masks, colors and shadows the runtime renders from and the only
    // one they can be changed on: the plane with the ROM frame's size. In an ordinary project
    // that is the 128x32 plane also for the 256x64 picture, which libserum composes from the
    // 128x32 dynamic zones, upscaled (Colorize_Framev2: layer mode forces frameHasDynamicExtra
    // false). The 256x64 dynamic data such a project stores is loaded, readable and written
    // back unchanged; the setters below throw std::logic_error for it.
    [[nodiscard]] Resolution dynamicPlane() const;
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
    // Upscales the SD sprite. In an HD-only project the HD sprite lines up with the sprite
    // original instead: its mask is the original's shape, its colors the plain original colors.
    void createHdSprite(std::size_t spriteIndex);
    // Throws std::logic_error in an HD-only project.
    void removeHdSprite(std::size_t spriteIndex);
    [[nodiscard]] SpriteHdState spriteHdState(std::size_t spriteIndex) const;
    void setSpriteHdState(std::size_t spriteIndex, SpriteHdState state);
    [[nodiscard]] uint8_t spriteOriginalPixel(std::size_t spriteIndex,
                                              int x,
                                              int y) const;
    // Both captures take a frame's original and a colorized frame of the same size. In an
    // HD-only project that is the HD frame: the captured colors become the HD sprite, its mask
    // the captured shape, and the unused SD sprite data is reset (colors 0, not dynamic).
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
    // The given frames alone, in that order, with the comparison masks, backgrounds and sprites
    // they reference and nothing else. The IDs in the frames are renumbered to match, in order
    // of first use; an ID that points at nothing becomes "none". Palettes, editor metadata and
    // unsupported payloads are left out. Costs what the listed frames and their items cost,
    // not what the project costs. Throws std::out_of_range for a frame that does not exist.
    [[nodiscard]] ProjectSnapshot toSubsetSnapshot(const std::vector<std::size_t>& frameIndices,
                                                   SnapshotSubsetMap* map = nullptr) const;

private:
    void replaceCapturedHdOnlySprite(SpriteRecord& sprite,
                                     IndexedFrame original,
                                     Rgb565Frame colorized);
    void requireEditablePlane(Resolution resolution) const;
    void requireEditableExtraPlane() const;
    void requireDynamicPlane(Resolution resolution) const;
    void markChanged();

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
    render::ProjectGeometry m_geometry;
    bool m_dirty = false;
    uint64_t m_revision = 0;
    uint64_t m_identificationRevision = 0;
};

// The colors of an uncolorized frame: a ramp over the original color indices, as the Win32
// editor writes it.
[[nodiscard]] std::vector<uint16_t> plainOriginalColors(uint32_t colorCount);

[[nodiscard]] Rgb565Frame makeRgb565Frame(Size size, uint16_t fill = 0);
[[nodiscard]] IndexedFrame makeIndexedFrame(Size size, uint8_t fill = 0);

}  // namespace colorizing::document
