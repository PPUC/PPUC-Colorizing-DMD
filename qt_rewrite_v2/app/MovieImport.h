#pragma once

#include <QImage>
#include <QRect>

#include <cstdint>
#include <vector>

namespace colorizing::app {

// The movie clip import (VideoImportDialog): which picture of the clip each selected frame gets,
// and the part of it that is used.

// The clip time of each frame, from the first selected frame on: startMs, then on by each
// frame's duration (durationsMs, as the frames play; the last one's is not needed), or by
// intervalMs when it is above 0.
[[nodiscard]] std::vector<double> movieFrameTimes(double startMs,
                                                  const std::vector<uint32_t>& durationsMs,
                                                  int intervalMs);

// The crop zone of the picture (all of it for an empty or outside zone), the part that is
// scaled into the frame.
[[nodiscard]] QImage croppedMoviePicture(const QImage& picture, const QRect& crop);

}  // namespace colorizing::app
