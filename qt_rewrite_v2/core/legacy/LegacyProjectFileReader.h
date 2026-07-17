#pragma once

#include "legacy/LegacyProjectIo.h"

namespace colorizing::legacy {

class LegacyProjectFileReader final : public ILegacyProjectReader {
public:
    [[nodiscard]] LegacyLoadResult load(const LegacyProjectPaths& paths) const override;
};

}  // namespace colorizing::legacy

