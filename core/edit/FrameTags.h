#pragma once

#include <string>
#include <vector>

namespace colorizing::edit {

// Frame tags (ProjectDocument::frameTags()): groups of frames such as "Multiball" or
// "Settings menu" the frame strip can be filtered by. A frame can have several.

// The tags of a comma-separated list, trimmed, sorted, each once, empty ones left out.
[[nodiscard]] std::vector<std::string> parseFrameTags(const std::string& text);
// The tags as the list the Tags field shows: "Multiball, Wizard Mode".
[[nodiscard]] std::string frameTagsText(const std::vector<std::string>& tags);

// What changing the Tags field from `before` to `after` does to a frame with `tags`: the tags
// added there are added, the tags removed there are removed, its other tags stay. So the field
// of the current frame changes all selected frames without overwriting their own tags.
[[nodiscard]] std::vector<std::string> retaggedFrame(const std::vector<std::string>& tags,
                                                     const std::vector<std::string>& before,
                                                     const std::vector<std::string>& after);

}  // namespace colorizing::edit
