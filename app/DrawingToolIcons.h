#pragma once

#include <QIcon>

#include "edit/DrawingGeometry.h"

namespace colorizing::app {

// The icon of a drawing tool's toolbar button, in the colors of the application's palette at the
// time it is painted, so that it follows the theme: the text color of a button, or that of a
// checked one (checkedButtonTextColor()) while it is on.
[[nodiscard]] QIcon drawingToolIcon(edit::DrawingTool tool);

}  // namespace colorizing::app
