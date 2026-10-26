#pragma once

#include "legacy/LegacyProjectIo.h"

// DEPRECATED test support (see tests/legacy-writers/README.md): the editor writes no legacy
// project files. To be removed together with the tests that still write them.

namespace colorizing::legacy {

class ILegacyProjectWriter {
public:
    virtual ~ILegacyProjectWriter() = default;
    // progress may be null. The snapshot is only read; a document's toSnapshot() shares its
    // blocks with the document (copy on write), so it costs no copy of the pixels.
    [[nodiscard]] virtual LegacySaveResult save(const LegacyProjectPaths& paths,
                                                const document::ProjectSnapshot& snapshot,
                                                LegacyIoProgress* progress = nullptr) const = 0;
};

// Writes a project as the Win32 editor (.cROM or .cRZ, and .cRP) or the editor versions
// before the project file (.cRZ and .cRPX) have it, the .cRP or .cRPX by the path's extension.
class LegacyProjectFileWriter final : public ILegacyProjectWriter {
public:
    [[nodiscard]] LegacySaveResult save(const LegacyProjectPaths& paths,
                                        const document::ProjectSnapshot& snapshot,
                                        LegacyIoProgress* progress = nullptr) const override;
};

}  // namespace colorizing::legacy
