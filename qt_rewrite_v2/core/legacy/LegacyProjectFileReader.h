#pragma once

#include <optional>

#include "document/ProjectDocument.h"
#include "legacy/LegacyProjectIo.h"

namespace colorizing::legacy {

class LegacyProjectFileReader final : public ILegacyProjectReader {
public:
    [[nodiscard]] LegacyLoadResult load(const LegacyProjectPaths& paths,
                                        LegacyIoProgress* progress = nullptr) const override;
};

struct LegacyDocumentLoadResult {
    std::optional<document::ProjectDocument> document;
    LegacyIoError error;
    bool ok = false;
    bool cancelled = false;
};

// What File -> Open does off the UI thread: reads the project and moves the snapshot into a
// document (ProjectDocument::fromSnapshot()), so that the parsed blocks exist once. Qt-free;
// errors, including exceptions of the document, come back as the result's message.
[[nodiscard]] LegacyDocumentLoadResult loadLegacyProjectDocument(const LegacyProjectPaths& paths,
                                                                 LegacyIoProgress* progress);

}  // namespace colorizing::legacy

