#pragma once

#include <functional>

#include <QString>

#include "document/ProjectDocument.h"

namespace colorizing::app {

// What the user answers when a project with unsaved changes is about to be closed or replaced.
enum class UnsavedChangesAnswer {
    Save,
    Discard,
    Cancel,
};

// Whether the open project may be closed or replaced (window close, Quit, Open Project, Open
// Recent, New Project From Dump). A project without unsaved changes may, without a question.
// Otherwise ask() is asked once: Discard allows it, Cancel does not, and Save allows it only
// if save() wrote the project (a new project asks where to first, which can be cancelled, and
// a save can fail).
[[nodiscard]] bool mayCloseProject(bool hasUnsavedChanges,
                                   const std::function<UnsavedChangesAnswer()>& ask,
                                   const std::function<bool()>& save);

// What a save of a 192x64 project loses: the 96x32 pictures its files still hold, which the
// editor dropped on load. Empty when there are none.
[[nodiscard]] QString discardedExtraPlaneWarning(const document::DiscardedExtraPlane& discarded);

}  // namespace colorizing::app
