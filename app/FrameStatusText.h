#pragma once

#include <QString>

#include <cstddef>
#include <vector>

namespace colorizing::document {
class ProjectDocument;
}

namespace colorizing::app {

// The part of the frame status line about the selected frames: how many, and how long they
// last together (issue #28: the length of a scene to make of them). Empty for one frame.
[[nodiscard]] QString selectedFramesStatusText(const document::ProjectDocument& document,
                                               const std::vector<std::size_t>& frames);

}  // namespace colorizing::app
