#pragma once

#include <optional>

#include "edit/DrawingGeometry.h"

namespace colorizing::app {

// Pick picks one color and gives the tool chosen before it back (issue #29): with Rect chosen,
// Pick, a click, and Rect draws on with the picked color.
class PickToolReturn {
public:
    // A tool was chosen; previous: the one before.
    void toolChosen(edit::DrawingTool previous, edit::DrawingTool chosen)
    {
        if (chosen != edit::DrawingTool::ColorPicker) {
            m_before.reset();
        } else if (previous != edit::DrawingTool::ColorPicker) {
            m_before = previous;
        }
    }

    // After a pick with the current tool: the tool to go back to; nothing to stay.
    [[nodiscard]] std::optional<edit::DrawingTool> afterPick(edit::DrawingTool current) const
    {
        return current == edit::DrawingTool::ColorPicker ? m_before : std::nullopt;
    }

private:
    std::optional<edit::DrawingTool> m_before;
};

}  // namespace colorizing::app
