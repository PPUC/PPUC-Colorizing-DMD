#pragma once

#include "legacy/LegacyProjectIo.h"

namespace colorizing::legacy {

class LegacyProjectFileWriter final : public ILegacyProjectWriter {
public:
    [[nodiscard]] LegacySaveResult save(const LegacyProjectPaths& paths,
                                        const document::ProjectSnapshot& snapshot,
                                        LegacyIoProgress* progress = nullptr) const override;
};

}  // namespace colorizing::legacy
