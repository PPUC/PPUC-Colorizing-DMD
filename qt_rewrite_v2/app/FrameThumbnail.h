#pragma once

#include <cstddef>

#include <QIcon>
#include <QSize>

#include "document/ProjectDocument.h"

namespace colorizing::app {

[[nodiscard]] QIcon makeFrameThumbnail(const document::ProjectDocument& document,
                                       std::size_t frameIndex,
                                       const QSize& iconSize);

}  // namespace colorizing::app

