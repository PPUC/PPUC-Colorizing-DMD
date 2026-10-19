#pragma once

#include <cstddef>

#include <QIcon>
#include <QSize>

#include "document/ProjectDocument.h"

namespace colorizing::app {

[[nodiscard]] QIcon makeFrameThumbnail(const document::ProjectDocument& document,
                                       std::size_t frameIndex,
                                       const QSize& iconSize);

// The empty box a frame list row shows until its thumbnail is built.
[[nodiscard]] QIcon makeFrameThumbnailPlaceholder(const QSize& iconSize);

}  // namespace colorizing::app

