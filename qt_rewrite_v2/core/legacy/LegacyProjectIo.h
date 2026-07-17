#pragma once

#include <string>
#include <utility>

#include "document/ProjectSnapshot.h"

namespace colorizing::legacy {

struct LegacyProjectPaths {
    std::string cromPath;
    std::string crpPath;
};

struct LegacyIoError {
    std::string message;
};

struct LegacyLoadResult {
    document::ProjectSnapshot snapshot;
    LegacyIoError error;
    bool ok = false;
};

struct LegacySaveResult {
    LegacyIoError error;
    bool ok = false;
};

class ILegacyProjectReader {
public:
    virtual ~ILegacyProjectReader() = default;
    [[nodiscard]] virtual LegacyLoadResult load(const LegacyProjectPaths& paths) const = 0;
};

class ILegacyProjectWriter {
public:
    virtual ~ILegacyProjectWriter() = default;
    [[nodiscard]] virtual LegacySaveResult save(const LegacyProjectPaths& paths,
                                                const document::ProjectSnapshot& snapshot) const = 0;
};

[[nodiscard]] LegacyLoadResult makeLoadFailure(std::string message);
[[nodiscard]] LegacySaveResult makeSaveFailure(std::string message);
[[nodiscard]] LegacyLoadResult makeLoadSuccess(document::ProjectSnapshot snapshot);
[[nodiscard]] LegacySaveResult makeSaveSuccess();

}  // namespace colorizing::legacy

