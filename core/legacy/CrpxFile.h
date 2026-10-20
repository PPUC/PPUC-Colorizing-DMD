#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::legacy {

class LegacyByteSource;

// The editor file of a project saved by this editor: "<name>.cRPX" beside its .cROM, in place
// of the Win32 editor's "<name>.cRP". The Win32 editor (3.17.3) opens a .cROM with the .cRP of
// the same name only, and reads it without any check, so it never opens a project this editor
// saved; the editor file can be extended without regard to it. The .cROM stays what libserum
// reads.
//
// Layout, little endian:
//   char[8]  "CDMDcRPX"
//   uint32   format version (kCrpxVersion; a reader refuses a newer one)
//   uint32   header size in bytes, from the start of the file (24 for version 1)
//   uint64   size of the cRP part
//   the cRP part: what writeLegacyCrp() writes, as in a .cRP of the Win32 editor, with an
//   empty PuP pack field (the pack is the chunk PUPP; a cRP part with a pack in its field, as
//   earlier versions wrote it, is read, and a PUPP chunk replaces it)
//   chunks up to the end of the file: char[4] tag, uint32 0, uint64 size, the data; a reader
//   skips those it does not know. Written so far:
//     "PUPP" the PuP pack, when the project has one or names one: uint32 has pack (the Win32
//            editor's flag), uint32 name length, the name in UTF-8.
//     "HLIN" the helper lines (document/HelperLine.h), when there are any: uint32 count, then
//            per line uint8 vertical, uint8 plane (0 SD, 1 HD), uint8 visible, uint8 0,
//            uint32 position, uint32 name length, the name in UTF-8.
//     "SCEN" the colorization settings (document/Scene.h), when the project has them, also
//            without scenes: uint8 scaling algorithm, uint8 shadow offset, uint16 0,
//            uint32 scene count, per scene uint16 scene ID, uint16 frame count, uint16 frame
//            duration in ms, uint8 interruptable, immediate start, repeat, frame groups,
//            random, auto start seconds, options, three bytes 0; then uint32 count of scene
//            frames, per frame uint32 frame index, uint16 scene ID, uint8 group, uint8 0,
//            uint16 frame index in the scene, uint16 0.
//     "IZON" the Images window's saved zones (document/ImageZone.h), when there are any:
//            uint32 count, per zone uint8 flags (1: it has an end zone), int8 brightness,
//            int8 contrast, uint8 blur, the start and the end zone as uint32 left, top, width,
//            height in millionths of the picture (the end all 0 without one), uint32 name
//            length, the name in UTF-8.
inline constexpr uint32_t kCrpxVersion = 1;
inline constexpr uint32_t kCrpxHeaderBytes = 24;

// Whether the path ends in ".cRPX" (any case).
[[nodiscard]] bool hasCrpxExtension(const std::string& path);

// Writes the whole .cRPX of the snapshot. Same contract as writeLegacyCrp(); sections are the
// cRP part's, counted from the start of the cRP part.
[[nodiscard]] bool writeCrpx(std::ostream& stream,
                             const document::ProjectSnapshot& snapshot,
                             std::string& errorMessage,
                             LegacySectionOffsets* sections = nullptr,
                             LegacyIoProgress* progress = nullptr);
[[nodiscard]] uint64_t crpxByteCount(const document::ProjectSnapshot& snapshot);

enum class CrpxProbe {
    // No .cRPX header: a .cRP of the Win32 editor, the source is unchanged.
    LegacyCrp,
    // A .cRPX: the source is at the start of the cRP part and ends with it.
    Crpx,
    // A .cRPX that cannot be read; errorMessage says why.
    Invalid,
};

// Looks at the start of an editor file, which may be either kind. For a .cRPX, chunksStart
// (when given) is where its chunks start, after the cRP part.
[[nodiscard]] CrpxProbe probeCrpx(LegacyByteSource& source,
                                  std::string& errorMessage,
                                  uint64_t* chunksStart = nullptr);

// Reads the chunks of a .cRPX from chunksStart to its end into the snapshot (the helper
// lines). Returns false with errorMessage for a damaged chunk; unknown chunks are skipped.
[[nodiscard]] bool readCrpxChunks(LegacyByteSource& source,
                                  uint64_t chunksStart,
                                  document::ProjectSnapshot& snapshot,
                                  std::string& errorMessage);

}  // namespace colorizing::legacy
