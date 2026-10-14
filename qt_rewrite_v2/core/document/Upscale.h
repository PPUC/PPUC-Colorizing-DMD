#pragma once

#include <string>

#include "document/ProjectDocument.h"

namespace colorizing::document {

// UpscaleFilter (ProjectDocument.h): how Create HD makes a picture twice the size.

[[nodiscard]] Rgb565Frame upscale2x(const Rgb565Frame& frame, UpscaleFilter filter);
[[nodiscard]] std::string upscaleFilterName(UpscaleFilter filter);

}  // namespace colorizing::document
