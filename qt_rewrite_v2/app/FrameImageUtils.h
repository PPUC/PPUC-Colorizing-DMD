#pragma once

#include <cstddef>
#include <vector>

#include <QColor>
#include <QImage>

#include "document/ProjectDocument.h"

class QPainter;

namespace colorizing::app {

enum class DmdPixelShape {
    Square,
    Dot,
};

struct DmdRenderOptions {
    DmdPixelShape pixelShape = DmdPixelShape::Square;
    double pixelCoverage = 1.0;
};

// A sprite found in a frame's ROM frame: its part (spriteX, spriteY), width x height, shows at
// (frameX, frameY). All in ROM frame pixels.
struct SpriteMatch {
    std::size_t spriteIndex = 0;
    int frameX = 0;
    int frameY = 0;
    int spriteX = 0;
    int spriteY = 0;
    int width = 0;
    int height = 0;
};

// The sprites of the frame's sprite slots that are in its ROM frame, in the order they are
// drawn. Follows libserum's Check_Spritesv2().
[[nodiscard]] std::vector<SpriteMatch> matchFrameSprites(const document::ProjectDocument& document,
                                                         std::size_t frameIndex);

[[nodiscard]] QColor rgb565ToColor(uint16_t value);
[[nodiscard]] QColor originalIndexToColor(uint8_t value);
[[nodiscard]] uint16_t resolvedFramePixel(const document::ProjectDocument& document,
                                          std::size_t frameIndex,
                                          render::Resolution resolution,
                                          int x,
                                          int y,
                                          bool includeBackgroundLayer);
// The frame as the runtime renders it on the plane, with dynamic content, background and
// sprites. An HD-only project has the HD plane alone: asking for SD throws std::logic_error,
// and a frame without HD data is black, as nothing is rendered for it.
[[nodiscard]] document::Rgb565Frame composeResolvedFrame(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    render::Resolution resolution,
    bool includeBackgroundLayer);
void applyColorRotations(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution resolution,
                         int elapsedMs,
                         document::Rgb565Frame& frame);
void paintDmdPixel(QPainter& painter,
                   double x,
                   double y,
                   double cellSize,
                   const QColor& color,
                   const DmdRenderOptions& options);
[[nodiscard]] QImage renderRgb565FrameImage(const document::Rgb565Frame& frame,
                                            int pixelScale,
                                            DmdRenderOptions options = {});
[[nodiscard]] QImage renderResolvedFrameImage(const document::ProjectDocument& document,
                                              std::size_t frameIndex,
                                              render::Resolution resolution,
                                              int pixelScale,
                                              bool includeBackgroundLayer,
                                              DmdRenderOptions options = {});
[[nodiscard]] QImage renderIndexedFrameImage(const document::IndexedFrame& frame,
                                             int pixelScale,
                                             DmdRenderOptions options = {});
// One image pixel per sprite pixel; falls back to SD when the sprite has no HD data. With
// hdOnly (ProjectDocument::rendersHdPlaneOnly()) there is no such fallback: the image is black.
[[nodiscard]] QImage renderSpriteColorizedPreviewImage(const document::SpriteRecord& sprite,
                                                       render::Resolution resolution,
                                                       bool hdOnly = false);
// One image pixel per background pixel; the same fallback rule, with the project's geometry
// saying whether it is an HD-only one.
[[nodiscard]] QImage renderBackgroundPreviewImage(const document::BackgroundRecord& background,
                                                  render::Resolution resolution,
                                                  const render::ProjectGeometry& geometry = {});

}  // namespace colorizing::app
