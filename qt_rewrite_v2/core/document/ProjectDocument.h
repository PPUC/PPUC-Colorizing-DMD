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

struct FrameRecord {
    IndexedFrame originalSd;
    Rgb565Frame colorizedSd;
    std::optional<Rgb565Frame> colorizedHd;
    uint8_t shapeComparisonMode = 0;
    uint8_t comparisonMaskId = 0;
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

    [[nodiscard]] const IndexedFrame& originalFrame(std::size_t frameIndex) const;
    [[nodiscard]] const Rgb565Frame& colorizedFrame(std::size_t frameIndex,
                                                    Resolution resolution) const;

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
    std::vector<UnsupportedLegacyPayload> m_unsupportedPayloads;
    bool m_dirty = false;
};

[[nodiscard]] Rgb565Frame makeRgb565Frame(Size size, uint16_t fill = 0);
[[nodiscard]] IndexedFrame makeIndexedFrame(Size size, uint8_t fill = 0);

}  // namespace colorizing::document
