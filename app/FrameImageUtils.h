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

// The pixels of the plane the sprites found on the frame paint (row-major, 1 painted), as
// composeResolvedFrame() draws them: for the canvas's outline of the found sprites.
[[nodiscard]] std::vector<uint8_t> spriteCoverage(const document::ProjectDocument& document,
                                                  std::size_t frameIndex,
                                                  render::Resolution resolution);

[[nodiscard]] QColor rgb565ToColor(uint16_t value);
// The color the editor shows a shade of an original frame in: an amber ramp over the 4 or 16
// shades of the project (colorCount, ProjectDocument::colorCount()).
[[nodiscard]] QColor originalIndexToColor(uint8_t value, uint32_t colorCount = 4);
[[nodiscard]] uint16_t resolvedFramePixel(const document::ProjectDocument& document,
                                          std::size_t frameIndex,
                                          render::Resolution resolution,
                                          int x,
                                          int y,
                                          bool includeBackgroundLayer);
// The frame as the runtime renders it on the plane, with dynamic content, background and
// sprites. An HD-only project has the HD plane alone: asking for SD throws std::logic_error,
// and a frame without HD data is black, as nothing is rendered for it.
//
// notRotating, if given, gets one entry per pixel: 1 for a pixel the frame's color rotations
// leave alone whatever its color is. Those are libserum's: a pixel colored from a dynamic
// zone's set and a dynamic shadow (Colorize_Framev2 gives both the rotation entry 0xffff).
// Background, static and sprite pixels rotate if their color is in a rotation.
[[nodiscard]] document::Rgb565Frame composeResolvedFrame(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    render::Resolution resolution,
    bool includeBackgroundLayer,
    std::vector<uint8_t>* notRotating = nullptr,
    // Without the sprites libserum finds on the frame ("Show Sprites" off).
    bool includeSprites = true,
    // How long the background scene the frame starts has played: which of its frames shows
    // through the background mask (ProjectDocument::backgroundPicture()).
    uint32_t sceneElapsedMs = 0);
// How many pixels of the frame's picture a rotation slot moves: those that are not dynamic
// content or a dynamic shadow and whose color the slot owns (the first slot, within its
// length, that lists the color; see applyColorRotations()), out of the picture's pixels that
// are not black.
struct RotationSlotUse {
    std::size_t rotating = 0;
    std::size_t colored = 0;
};
[[nodiscard]] RotationSlotUse rotationSlotUse(const document::ProjectDocument& document,
                                              std::size_t frameIndex,
                                              render::Resolution resolution,
                                              std::size_t slotIndex);

// The frame's color rotations elapsedMs after they started, every slot stepping at its own
// delay: a pixel whose color is in a rotation takes the color that many places further in it.
// frame and notRotating are those of composeResolvedFrame(); without notRotating every pixel
// of a rotating color moves, which is right only for a picture without dynamic content.
void applyColorRotations(const document::ProjectDocument& document,
                         std::size_t frameIndex,
                         render::Resolution resolution,
                         int elapsedMs,
                         document::Rgb565Frame& frame,
                         const std::vector<uint8_t>* notRotating = nullptr);
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
// colorCount: the project's original colors, see originalIndexToColor().
[[nodiscard]] QImage renderIndexedFrameImage(const document::IndexedFrame& frame,
                                             int pixelScale,
                                             DmdRenderOptions options = {},
                                             uint32_t colorCount = 4);
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
