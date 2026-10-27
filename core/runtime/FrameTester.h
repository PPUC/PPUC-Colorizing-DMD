#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::runtime {

// The frame tester feeds ROM frames, one after the other, to libserum with the whole project
// loaded, as a machine does (the Win32 editor's Frame Tester). The frames come from the
// project (the selected frames) or from a frame dump.

struct TesterFrame {
    document::IndexedFrame original;
    // How long the frame is shown when the tester plays.
    uint32_t durationMs = 0;
    // The project frame it was taken from; none for a frame of a dump.
    std::optional<std::size_t> projectFrame;
    // The dump's time of the frame; 0 for a project frame.
    uint32_t timecode = 0;
};

[[nodiscard]] std::vector<TesterFrame> testerFramesOfProject(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& frameIndices);

struct TesterDump {
    std::vector<TesterFrame> frames;
    // Empty when the dump was read; then frames holds every frame of it.
    std::string error;
    // A last frame the dump ends inside of, which is dropped (see FrameDumpReader).
    std::optional<std::size_t> truncatedFrameLine;
    bool cancelled = false;
};

// Called now and then with the bytes read; returning false stops reading.
using TesterDumpProgress = std::function<bool(uint64_t bytesRead)>;

// Every frame of a dump, in its order, repeats included. A frame lasts until the next one's
// time, as the dump import measures it (importing::kDefaultFrameDurationMs when the times do
// not tell). The frames must have the size of the project's ROM frames and no shade the
// project's originals cannot have.
[[nodiscard]] TesterDump readTesterDump(std::istream& stream,
                                        const document::ProjectDocument& document,
                                        const TesterDumpProgress& progress = {});

// Where the project frame's ROM frame is among the tester frames: the first match after
// position `after`, going round to the start (CompareFrameToDump of the Win32 editor). With
// underMask, the pixels the frame's comparison mask covers are not compared.
[[nodiscard]] std::optional<std::size_t> findProjectFrame(const document::ProjectDocument& document,
                                                          std::size_t frameIndex,
                                                          const std::vector<TesterFrame>& frames,
                                                          std::size_t after,
                                                          bool underMask);

}  // namespace colorizing::runtime
