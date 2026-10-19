#include "legacy/LegacyProjectIo.h"

#include <utility>

namespace colorizing::legacy {

LegacyLoadResult makeLoadFailure(std::string message)
{
    LegacyLoadResult result;
    result.error.message = std::move(message);
    result.ok = false;
    return result;
}

LegacySaveResult makeSaveFailure(std::string message)
{
    LegacySaveResult result;
    result.error.message = std::move(message);
    result.ok = false;
    return result;
}

LegacyLoadResult makeLoadSuccess(document::ProjectSnapshot snapshot)
{
    LegacyLoadResult result;
    result.snapshot = std::move(snapshot);
    result.ok = true;
    return result;
}

LegacySaveResult makeSaveSuccess()
{
    LegacySaveResult result;
    result.ok = true;
    return result;
}

LegacyLoadResult makeLoadCancelled()
{
    LegacyLoadResult result = makeLoadFailure(kLegacyIoCancelledMessage);
    result.cancelled = true;
    return result;
}

LegacySaveResult makeSaveCancelled()
{
    LegacySaveResult result = makeSaveFailure(kLegacyIoCancelledMessage);
    result.cancelled = true;
    return result;
}

}  // namespace colorizing::legacy

