#include "UnsavedChanges.h"

namespace colorizing::app {

bool mayCloseProject(bool hasUnsavedChanges,
                     const std::function<UnsavedChangesAnswer()>& ask,
                     const std::function<bool()>& save)
{
    if (!hasUnsavedChanges) {
        return true;
    }
    switch (ask()) {
        case UnsavedChangesAnswer::Save:
            return save();
        case UnsavedChangesAnswer::Discard:
            return true;
        case UnsavedChangesAnswer::Cancel:
            break;
    }
    return false;
}

}  // namespace colorizing::app
