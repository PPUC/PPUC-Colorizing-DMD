#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "import/FrameDumpReader.h"

namespace colorizing::importing {

// A frame is shown until the next one arrives. Where that cannot be told (the last frame, a
// timecode that goes back, a gap of more than kMaxFrameDurationMs) it gets the default.
constexpr uint32_t kDefaultFrameDurationMs = 30;
constexpr uint32_t kMaxFrameDurationMs = 4000;

constexpr uint32_t kMinShortFrameLimitMs = 5;
constexpr uint32_t kMaxShortFrameLimitMs = 3000;
constexpr uint32_t kMinTransitionalLimitMs = 5;
constexpr uint32_t kMaxTransitionalLimitMs = 100;
constexpr uint32_t kMinColorLimit = 1;
constexpr uint32_t kMaxColorLimit = 16;

// The import filter of the Win32 editor (Filter_Proc).
struct DumpImportOptions {
    // Drop a frame that was shown for less than shortFrameLimitMs. A frame that got the
    // default duration is never dropped by this.
    bool dropShortFrames = false;
    uint32_t shortFrameLimitMs = 15;
    // Drop a frame that has more than colorLimit different shades.
    bool dropColorfulFrames = false;
    uint32_t colorLimit = 4;
    // Only when frames are added to a project. Off: a dump frame is dropped if a frame of
    // the project has the same original, whatever that frame's comparison mask and shape
    // mode are. On: it is dropped if the runtime identifies it as a frame of the project,
    // that is if a frame of the project has the same hash under that frame's own mask and
    // shape mode.
    bool useMasksAndShapeModes = false;
    // Drop transitional frames (isTransitionalFrame()) shown for at most
    // transitionalLimitMs: on WPC machines the ghost of two pictures while the display
    // changes from one to the next. Not a filter of the Win32 editor.
    bool dropTransitionalFrames = false;
    uint32_t transitionalLimitMs = 20;
};

// The limits brought into their ranges.
[[nodiscard]] DumpImportOptions clampedOptions(DumpImportOptions options);

// What became of the frames of a dump. Every frame read is counted once:
// framesRead == transitional + tooShort + tooManyColors + duplicates + identicalInProject +
//               identifiedByMaskOrShape + framesKept.
struct DumpImportCounts {
    std::size_t framesRead = 0;
    std::size_t transitional = 0;
    std::size_t tooShort = 0;
    std::size_t tooManyColors = 0;
    // The same original as an earlier frame of the dump that was kept.
    std::size_t duplicates = 0;
    // The same original as a frame of the project that is compared without a mask and
    // without shape mode (useMasksAndShapeModes) or as any frame of the project (otherwise).
    std::size_t identicalInProject = 0;
    // Identified by the project through a comparison mask or a shape mode.
    std::size_t identifiedByMaskOrShape = 0;
    std::size_t framesKept = 0;
    // Among duplicates and identicalInProject: frames with the hash of the frame they were
    // dropped for and other pixels. The runtime cannot tell such frames apart either.
    std::size_t hashCollisions = 0;

    [[nodiscard]] std::size_t alreadyInProject() const
    {
        return identicalInProject + identifiedByMaskOrShape;
    }
};

struct ImportedFrame {
    document::IndexedFrame original;
    uint32_t durationMs = 0;
};

struct DumpImportResult {
    bool ok = false;
    bool cancelled = false;
    DumpError error;
    // As in the file.
    render::Size dumpFrameSize{0, 0};
    // As stored: 128x16 frames are padded to 128x32.
    render::Size storedFrameSize{0, 0};
    // 16 if any frame of the dump has a shade above 3, otherwise 4.
    uint32_t colorCount = 4;
    DumpImportCounts counts;
    std::optional<std::size_t> truncatedFrameLine;
    // The frames that were kept, in the order of the dump.
    std::vector<ImportedFrame> frames;
};

// Whether a frame of a dump is one the project has already. Holds the hashes of the
// project's frames and refers to the document, which has to stay as it is meanwhile.
class ProjectFrameMatcher {
public:
    enum class Match {
        None,
        Identical,
        MaskOrShape,
    };

    ProjectFrameMatcher(const document::ProjectDocument& document, bool useMasksAndShapeModes);

    // plainHash is legacyFrameHash(frame, nullptr, false). collision is set when the match
    // is Identical and the matched frame of the project has other pixels.
    [[nodiscard]] Match match(const document::IndexedFrame& frame,
                              uint32_t plainHash,
                              bool* collision = nullptr) const;

private:
    struct Rule {
        // kNoComparisonMaskId: no mask.
        uint8_t maskId = document::kNoComparisonMaskId;
        bool shapeMode = false;
        // Hash under the rule -> one frame of the project that has it.
        std::unordered_map<uint32_t, std::size_t> frames;
    };

    const document::ProjectDocument& m_document;
    // Plain hash -> the first frame of the project that has it.
    std::unordered_map<uint32_t, std::size_t> m_plainFrames;
    std::vector<Rule> m_rules;
};

// Whether frame is a transitional frame between previous and next, the frames of the dump
// before and after it: a mix of the two, as a WPC display shows while it changes from one
// picture to the next (it builds its shades from several sub-frames, some of which still
// come from the old picture). A pixel that is the same in both neighbours is that in the
// frame too; every pixel that goes from shade a to shade c has one and the same shade in
// the frame, whatever its place, and for at least one such (a, c) that shade lies strictly
// between a and c. The frame differs from both neighbours, which differ from each other.
[[nodiscard]] bool isTransitionalFrame(const document::IndexedFrame& previous,
                                       const document::IndexedFrame& frame,
                                       const document::IndexedFrame& next);

// Called now and then with the number of bytes read; returning false cancels the import.
using DumpProgress = std::function<bool(uint64_t bytesRead)>;

// Reads a dump and applies, in this order and as the Win32 editor does: the duration of
// each frame, the filter for transitional frames (this editor's own, compared with the
// frames read before and after it, whatever became of those), the filter for short frames, the filter for colors, the removal of frames
// that repeat an earlier kept frame of the dump, and, when a project is given, the removal
// of frames the project has already. Keeps only the frames that pass.
//
// With a project the dump has to have the size of its original frames, and a dump with 16
// shades cannot go into a project with 4. A dump with 4 into a project with 16 is taken as
// it is: the shades 0 to 3 are valid there.
[[nodiscard]] DumpImportResult importFrameDump(std::istream& stream,
                                               const DumpImportOptions& options,
                                               const document::ProjectDocument* project,
                                               const DumpProgress& progress = {});
// totalBytes, if given, is set to the size of the file before reading starts.
[[nodiscard]] DumpImportResult importFrameDumpFile(const std::string& path,
                                                   const DumpImportOptions& options,
                                                   const document::ProjectDocument* project,
                                                   const DumpProgress& progress = {},
                                                   uint64_t* totalBytes = nullptr);

// The layout a project of that stored frame size gets; nothing for another size.
[[nodiscard]] std::optional<render::ProjectGeometry> geometryForDump(render::Size storedSize);

constexpr std::size_t kMaxRomNameLength = 63;
// The file name up to its first dot, as the Win32 editor takes it, cut to 63 bytes.
[[nodiscard]] std::string romNameFromDumpPath(const std::string& path);
// Why the name cannot be a ROM name; empty if it can. A ROM name has 1 to 63 bytes, is
// printable ASCII and has none of the characters a file name cannot have.
[[nodiscard]] std::string romNameProblem(const std::string& name);

// A new project of the dump's frames: every frame uncolorized (the picture is the original
// in the plain original colors), without masks, sprites and backgrounds, cROM revision 20,
// the cRP as a freshly started Win32 editor leaves it after "New project". Throws
// std::invalid_argument for a name romNameProblem() objects to, a frame size without a
// layout or no frames. Takes the frames out of the result.
[[nodiscard]] document::ProjectSnapshot makeProjectSnapshot(const std::string& romName,
                                                            DumpImportResult&& dump);

// The kept frames as frames of the project, for edit::addFrames(). Takes them out of the
// result.
[[nodiscard]] std::vector<document::FrameRecord> makeProjectFrames(
    const document::ProjectDocument& project,
    DumpImportResult&& dump);

}  // namespace colorizing::importing
