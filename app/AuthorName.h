#pragma once

#include <QString>

namespace colorizing::app {

// The name of the person working with the editor (Settings -> Projects): a save writes it as
// the author of every frame, mask, background, sprite and palette that changed. Empty until
// it is set.
[[nodiscard]] QString loadAuthorName();
void saveAuthorName(const QString& name);

}  // namespace colorizing::app
