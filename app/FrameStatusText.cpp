#include "FrameStatusText.h"

#include <QCoreApplication>

#include <cstdint>

#include "document/ProjectDocument.h"

namespace colorizing::app {

QString selectedFramesStatusText(const document::ProjectDocument& document,
                                 const std::vector<std::size_t>& frames)
{
    if (frames.size() < 2) {
        return {};
    }
    uint64_t durationMs = 0;
    for (const std::size_t frame : frames) {
        if (frame < document.frameCount()) {
            durationMs += document.frameDurationMs(frame);
        }
    }
    return QCoreApplication::translate("MainWindow", " | %1 selected, %2 ms")
        .arg(static_cast<qulonglong>(frames.size()))
        .arg(static_cast<qulonglong>(durationMs));
}

}  // namespace colorizing::app
