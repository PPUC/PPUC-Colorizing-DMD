#include "document/ProjectSnapshot.h"

#include "render/CanvasLayout.h"

namespace colorizing::document {

namespace {

bool hasSize(Size actual, Size expected)
{
    return actual.width == expected.width && actual.height == expected.height;
}

bool fail(std::string* errorMessage, const std::string& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace

bool isValidProjectSnapshot(const ProjectSnapshot& snapshot, std::string* errorMessage)
{
    if (snapshot.romName.empty()) {
        return fail(errorMessage, "ROM name is empty");
    }

    for (std::size_t i = 0; i < snapshot.comparisonMasks.size(); ++i) {
        const IndexedFrame& mask = snapshot.comparisonMasks[i];
        if (!mask.isValid() || !hasSize(mask.size, render::kSdFrameSize)) {
            return fail(errorMessage, "comparison mask " + std::to_string(i) +
                                          " has invalid SD data");
        }
    }

    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        const ProjectFrameSnapshot& frame = snapshot.frames[i];
        if (!frame.originalSd.isValid() || !hasSize(frame.originalSd.size, render::kSdFrameSize)) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid SD original data");
        }
        if (!frame.colorizedSd.isValid() || !hasSize(frame.colorizedSd.size, render::kSdFrameSize)) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid SD colorized data");
        }
        if (frame.colorizedHd &&
            (!frame.colorizedHd->isValid() || !hasSize(frame.colorizedHd->size, render::kHdFrameSize))) {
            return fail(errorMessage, "frame " + std::to_string(i) + " has invalid HD colorized data");
        }
    }

    return true;
}

}  // namespace colorizing::document
