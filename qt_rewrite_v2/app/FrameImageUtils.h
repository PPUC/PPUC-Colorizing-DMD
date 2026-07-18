#pragma once

#include <QColor>
#include <QImage>

#include "document/ProjectDocument.h"

namespace colorizing::app {

[[nodiscard]] QColor rgb565ToColor(uint16_t value);
[[nodiscard]] QColor originalIndexToColor(uint8_t value);
[[nodiscard]] uint16_t resolvedFramePixel(const document::ProjectDocument& document,
                                          std::size_t frameIndex,
                                          render::Resolution resolution,
                                          int x,
                                          int y,
                                          bool includeBackgroundLayer);
[[nodiscard]] QImage renderRgb565FrameImage(const document::Rgb565Frame& frame, int pixelScale);
[[nodiscard]] QImage renderResolvedFrameImage(const document::ProjectDocument& document,
                                              std::size_t frameIndex,
                                              render::Resolution resolution,
                                              int pixelScale,
                                              bool includeBackgroundLayer);
[[nodiscard]] QImage renderIndexedFrameImage(const document::IndexedFrame& frame, int pixelScale);

}  // namespace colorizing::app
