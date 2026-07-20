#pragma once

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

[[nodiscard]] QColor rgb565ToColor(uint16_t value);
[[nodiscard]] QColor originalIndexToColor(uint8_t value);
[[nodiscard]] uint16_t resolvedFramePixel(const document::ProjectDocument& document,
                                          std::size_t frameIndex,
                                          render::Resolution resolution,
                                          int x,
                                          int y,
                                          bool includeBackgroundLayer);
[[nodiscard]] document::Rgb565Frame composeResolvedFrame(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    render::Resolution resolution,
    bool includeBackgroundLayer);
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

}  // namespace colorizing::app
