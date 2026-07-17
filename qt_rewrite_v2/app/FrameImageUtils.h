#pragma once

#include <QColor>
#include <QImage>

#include "document/ProjectDocument.h"

namespace colorizing::app {

[[nodiscard]] QColor rgb565ToColor(uint16_t value);
[[nodiscard]] QColor originalIndexToColor(uint8_t value);
[[nodiscard]] QImage renderRgb565FrameImage(const document::Rgb565Frame& frame, int pixelScale);
[[nodiscard]] QImage renderIndexedFrameImage(const document::IndexedFrame& frame, int pixelScale);

}  // namespace colorizing::app

