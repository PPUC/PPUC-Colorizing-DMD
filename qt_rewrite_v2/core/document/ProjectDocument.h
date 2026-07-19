#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

struct FrameDynamicData {
    IndexedFrame maskSd;
    IndexedFrame maskHd;
    std::vector<uint16_t> colorsSd;
    std::vector<uint16_t> colorsHd;
};

struct BackgroundRecord {
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
};

struct FrameBackgroundData {
    uint16_t backgroundId = 0xffff;
    IndexedFrame maskSd;
    IndexedFrame maskHd;
};

struct FrameRecord {
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    uint8_t shapeComparisonMode = 0;
    uint8_t comparisonMaskId = 0;
    std::optional<FrameDynamicData> dynamicData;
    std::optional<FrameBackgroundData> backgroundData;
};

struct UnsupportedLegacyPayload {
    std::string name;
    std::vector<uint8_t> bytes;
};

struct ProjectCreateOptions {
    std::string romName;
    uint32_t frameCount = 0;
    bool createHdFrames = false;
};

struct ProjectSnapshot;

class ProjectDocument {
public:
    [[nodiscard]] static ProjectDocument createEmpty(const ProjectCreateOptions& options);
    [[nodiscard]] static ProjectDocument fromSnapshot(const ProjectSnapshot& snapshot);

    [[nodiscard]] const std::string& romName() const;
    void setRomName(std::string name);

    [[nodiscard]] std::size_t frameCount() const;
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
    [[nodiscard]] std::size_t backgroundCount() const;
    [[nodiscard]] const BackgroundRecord& background(std::size_t backgroundIndex) const;
    [[nodiscard]] const Rgb565Frame& backgroundFrame(std::size_t backgroundIndex,
                                                     Resolution resolution) const;
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
    std::vector<UnsupportedLegacyPayload> m_unsupportedPayloads;
    bool m_dirty = false;
};

[[nodiscard]] Rgb565Frame makeRgb565Frame(Size size, uint16_t fill = 0);
[[nodiscard]] IndexedFrame makeIndexedFrame(Size size, uint8_t fill = 0);

}  // namespace colorizing::document
