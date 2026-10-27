#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectIo.h"
#include "project/Json.h"

namespace colorizing::project {

// The editor's own project file ("<name>.cdmd"): one zip archive per project. Components
// (frames, comparison masks, backgrounds, sprites, palettes) are identified by their UUID
// (document/ComponentId.h) and refer to each other by it, never by their position; each has
// its own entry, so that a save, a package or a merge can handle one without the others.
// Metadata is JSON (project/Json.h), pictures and masks are binary.
//
// Entries:
//   manifest.json    format, formatVersion (kProjectFormatVersion; a reader refuses a newer
//                    one), editorVersion, projectId, romName, colorCount, libserumTarget
//                    ("2.9"; earlier files may say "2.8", read and ignored), layout
//                    ("ordinary", "native-extra", "native-original"), cromRevision (21, read
//                    and ignored), license (kind, text, attribution, url), colorization (the
//                    cROM's settings: scalingAlgorithm, shadowOffset, scenes) when the project
//                    has them, unsupported (names of entries unsupported/<n>.bin).
//   order.json       the UUIDs of the frames, comparison masks, backgrounds and sprites in
//                    their order, and the sections (name, first frame).
//   frames/<uuid>.bin, masks/<uuid>.bin, backgrounds/<uuid>.bin, sprites/<uuid>.bin
//                    one component each, see below.
//   palettes.json    the 256 palettes (name, 64 RGB565 colors as hex digits, change
//                    information), when the project has them.
//   lights.json      the speaker lights' effects and defaults.
//   team/state.json, team/common.bin
//                    a team project's TeamState (document/TeamState.h): JSON for the
//                    repository, members, snapshot and packages; binary (as component
//                    entries) for the common revisions, per kind uint32 count and pairs of
//                    UUIDs, then the common frame order, then per sent package its
//                    revisions like the common ones.
//   team/base/<folder>/<uuid>.bin
//                    the common version of each component of a team project that changed
//                    since: the entry as it was then, for comparing in a conflict. A save takes
//                    it from the file it replaces (ProjectFileOptions::baseSourcePath).
//   editor.json      the editor's state of the project: edit colors, color sets, draw modes,
//                    image positions, time spent (in all, and per author: authorTimes),
//                    PuP pack, helper lines, saved image zones.
//
// A component entry is a list of fields: char[4] tag, uint32 0, uint64 size, the data (as the
// chunks of a .cRPX); little endian; a reader skips the tags it does not know. Every entry
// starts with META: the UUID (16 bytes, the text's byte order), the revision UUID, int64 the
// time of the change in UTC milliseconds, uint64 the content hash, the author in UTF-8. A
// picture is uint16 width, uint16 height and its pixels (one byte, or a uint16 RGB565 color,
// each). A reference is a UUID; a reference that names no component (kept from a legacy file
// as it was) is stored as the raw number in a field of its own.
//   Frame:      META, ORIG original, COLS colorized SD, COLH colorized HD, INHD inactive HD,
//               SHAP shape mode, CMSK mask (CMSX raw), DYNA dynamic data (fields DMSK, DMSH,
//               DCOS, DCOH, SDDS, SDCS, SDDH, SDCH), BGND background data (BGID or BGIX, BMSK,
//               BMSH), SPRS sprite slots (uint32 count, per slot the sprite's UUID or 16 zero
//               bytes, uint16 raw ID for a slot without one, uint16 minX, minY, maxX, maxY),
//               TRIG trigger, DURA duration, ROTS/ROTH color rotations, SCEN scene frame, TAGS
//               tags, LGHT speaker light actions, OOSD SD-only dynamic sets, IPAL imported
//               palette (192 bytes RGB888, the first frames only), DOFE DOF events (per
//               event uint8 type, uint8 1 with a turn-off value or 0, int32 number, value
//               and turn-off value).
//   Mask:       META, NAME, PICT.
//   Background: META, COLS, COLH, INHD.
//   Sprite:     META, NAME, FLAG (bit 0: has HD), ORIG, COLS, MSKH, COLH, DETD, DETP, DETA
//               (detection dwords, positions, areas), DMSK, DMSH, DCOS, DCOH (dynamic), SHAP,
//               SRCF source frame (SRCX raw), SRCR source rectangle, MIRR mirrors.
inline constexpr uint32_t kProjectFormatVersion = 1;
inline constexpr const char* kProjectFormatName = "PPUC Colorizing DMD project";

// Whether the path ends in ".cdmd" (any case).
[[nodiscard]] bool hasProjectFileExtension(const std::string& path);

// Who saves and when, for the components whose content changed.
struct RevisionStamp {
    std::string author;
    // UTC milliseconds since 1970.
    int64_t modifiedMs = 0;
};

// What a file of this layout holds, as its manifest says ("kind").
enum class FileKind {
    Project,
    // Frames of a project with what they use, for another project (edit/PackageImport.h).
    Package,
    // A team member's changes since the common state (team/ChangePackage.h).
    Changes,
};

struct ProjectFileOptions {
    // Written into the manifest, for people and support.
    std::string editorVersion;
    // The deflate level of the entries: fast by default, as the .cRZ.
    int compressionLevel = 1;
    // Where a team project's save takes the common versions of changed components from
    // (team/base); empty: the file the save replaces.
    std::string baseSourcePath;
    FileKind kind = FileKind::Project;
    // More entries to write, as they are (a change package's team/changes.json).
    std::vector<std::pair<std::string, std::string>> extraEntries;
    // When set, a component whose content is not the one of its revision (a content hash that
    // differs, or no revision yet) is written with a new revision, the stamp's author and time
    // and the new hash. Without it the change information is written as it is.
    std::optional<RevisionStamp> stamp;
};

// The entries of the project file, one after another: name and content. Throws
// std::invalid_argument for a snapshot whose components lack unique IDs (a document's
// snapshot always has them). stamped (may be null) gets the change information of the
// components the options' stamp gave a new revision.
void forEachProjectEntry(
    const document::ProjectSnapshot& snapshot,
    const ProjectFileOptions& options,
    const std::function<void(const std::string& name, const std::string& bytes)>& entry,
    std::vector<document::StampedMeta>* stamped = nullptr);

// The project back from its entries; read(name) returns an entry's content, nothing for an
// entry that is not there. errorMessage says what is missing or damaged. kind (may be null)
// says what the file holds.
[[nodiscard]] std::optional<document::ProjectSnapshot> readProjectEntries(
    const std::function<std::optional<std::string>(const std::string& name)>& read,
    std::string& errorMessage,
    legacy::LegacyIoProgress* progress = nullptr,
    FileKind* kind = nullptr);

// Writes the file beside the target and renames it into place, so that a failed save leaves
// the previous file as it was. progress may be null; done and total count entries. stamped
// (may be null) gets the change information the save gave components, only when it
// succeeds: the document takes it over (ProjectDocument::adoptComponentMeta()).
[[nodiscard]] legacy::LegacySaveResult saveProjectFile(
    const std::string& path,
    const document::ProjectSnapshot& snapshot,
    const ProjectFileOptions& options,
    legacy::LegacyIoProgress* progress = nullptr,
    std::vector<document::StampedMeta>* stamped = nullptr);

// Refuses a package, which is imported instead.
[[nodiscard]] legacy::LegacyLoadResult loadProjectFile(const std::string& path,
                                                       legacy::LegacyIoProgress* progress =
                                                           nullptr);
// A package ("<name>.cdmdpkg"); refuses a project file.
// (The kinds are checked by loadFileOfKind(), which also reads the extra entries asked for.)
[[nodiscard]] legacy::LegacyLoadResult loadPackageFile(const std::string& path,
                                                       legacy::LegacyIoProgress* progress =
                                                           nullptr);
// The pictures of a frame's entry of a project file, without resolving its references: for
// showing a version of the frame (a team project's common version, team/base).
struct FramePictures {
    document::IndexedFrame original;
    document::Rgb565Frame colorizedSd;
    std::optional<document::Rgb565Frame> colorizedHd;
};
// The common version of the frame (with that revision) a team project's file keeps
// (team/base), or the frame's own entry while it has that revision; nothing when the file has
// neither.
[[nodiscard]] std::optional<FramePictures> readCommonFramePictures(
    const std::string& projectPath,
    const document::ComponentId& frameId,
    const document::ComponentId& commonRevision);

// A file of the kind, with the extra entries of the names asked for (nothing for one the
// file lacks); refuses another kind.
struct KindLoadResult {
    legacy::LegacyLoadResult load;
    std::vector<std::optional<std::string>> extraEntries;
};
[[nodiscard]] KindLoadResult loadFileOfKind(const std::string& path,
                                            FileKind kind,
                                            const std::vector<std::string>& extraEntryNames,
                                            legacy::LegacyIoProgress* progress = nullptr);

// The project's settings that are no component, as one text for a hash: license, libserum
// target, scenes and scaling, speaker lights, sections (by frame ID), helper lines.
[[nodiscard]] std::string settingsContent(const document::ProjectSnapshot& snapshot);

// Each author's time (EditorMetadata::authorTimeMs) as a JSON object of milliseconds by name,
// in editor.json and in a change package; reading throws JsonFormatError for a damaged one,
// and gives none for a missing one.
[[nodiscard]] JsonValue authorTimesJson(const std::map<std::string, uint64_t>& times);
[[nodiscard]] std::map<std::string, uint64_t> readAuthorTimes(const JsonValue* value);

// Whether the path ends in ".cdmdpkg" (any case).
[[nodiscard]] bool hasPackageExtension(const std::string& path);

// What File -> Open does off the UI thread with a project file, as
// legacy::loadLegacyProjectDocument() does with the files of earlier editors.
[[nodiscard]] legacy::LegacyDocumentLoadResult loadProjectDocument(
    const std::string& path,
    legacy::LegacyIoProgress* progress = nullptr);

}  // namespace colorizing::project
