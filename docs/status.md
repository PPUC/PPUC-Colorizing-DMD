# ColorizingDMD Qt Rewrite v2 — Working Status

The editor's working notes: the decisions in force, how things are built, open questions and
known gaps. `handbook/en/` (shown in the app under Help -> Handbook) is the user-facing
behaviour reference; what changed when, and why, is in the git history (commit messages).
`docs/rewrite-plan.md` is the original plan. Settled matters are removed from this file.

## Ground rules decided by the owner

- **Compatibility target is the Win32 editor 3.17.3 only.** Nobody has used a Qt build yet, so
  files written by earlier Qt builds (v2 or the first `qt_playback_wip` attempt) need no support
  and no repair code.
- **libserum is the source of truth for rendering.** The Win32 editor's HD and 256x64 handling is
  to be treated as broken (confirmed by an author) and is never a reference for those.
- **Sprite matching always happens on the original ROM frame at native resolution**, never on an
  upscaled or extra plane. A plane smaller than the native frame makes no sense; libserum does not
  downscale.
- **The legacy source is the tag `v3.17.3`** (cROM header revision 20). The Win32 files were
  removed from this branch on 2026-10-19 (they were a stale 3.15.1 copy). Read legacy code with
  `git show v3.17.3:<file>`.
- **Project file (owner decisions 2026-10-14, 2026-10-23 and 2026-10-26):** the app saves the
  project file (`.cdmd`, see `project/` below) and writes no legacy file (`Export For Earlier
  Editors` was removed with the libserum target); a machine gets the `.cROMc`. The core keeps the
  readers and `writeLegacyCrom()` (the cROM handed to libserum). The writers of `.cRP`, `.cRPX`,
  `.cRZ` and `LegacyProjectFileWriter` moved to `tests/legacy-writers`, deprecated, only for the
  tests that still make legacy projects and for `colorizing_qt_v2_roundtrip`; they are to be removed
  (see "Next steps"). The `.cROM`/`.cRZ`/`.cRP`/`.cRPX` of 3.17.3 and of earlier versions of this
  editor are read and opened as converted (no project path, `m_sourcePath` names their files);
  nothing is written over or renamed. New project data goes into the project file.
- **Projects are made for libserum 2.9 only (owner decision 2026-10-26):** there is no
  per-project libserum target any more. `document/ProjectLimits.h` holds the limits (128 sprite
  slots per frame, 65535 sprites and backgrounds, cROM format version 1, header revision 21);
  a project of the Win32 editor or of libserum 2.8 is opened with its 32 slots per frame filled
  up to 128 (`ProjectDocument::fromSnapshot()`). Machines need libserum 2.9 or later.
- **Memory and save speed** matter: authors use large projects on ordinary machines (8 GB). See
  "Memory, load and save (2026-10-14)".
- **Commits and versions:** see AGENTS.md (small local commits, pushed on the owner's request
  after a version bump of its own). libserum `master` is 2.99.x, the unstable line towards
  3.0.0: its patch version rises with every commit and the cROMc need not stay compatible
  within it; `2.8.x` is the stable branch.

## Repositories and branches

- `ColorizingDMD`, branch `main`. The Win32 editor is at the tag `v3.17.3`.
- `../libserum` (`PPUC/libserum`): `master` (2.99.x) for the editor, `2.8.x` stable. The editor
  links it statically from the sibling checkout (`COLORIZINGDMD_LIBSERUM_DIR`); CI pins the
  commit (`LIBSERUM_SHA` in `.github/workflows/qt-editor.yml`).

## Architecture as it is now

- `core/` (Qt-free static library, links libserum):
  - `document/`: `ProjectDocument`, `ProjectSnapshot`, `EditorMetadata` (all cRP editor data).
    Every frame, comparison mask, background, sprite and palette has a
    `ComponentMeta` (`document/ComponentId.h`: a version 4 UUID, revision, author, time,
    content hash). The document gives an ID to a component without one and a new one to a
    copy (`ensureComponentIds()`, the insert and append methods); undo keeps IDs, snapshots
    and subsets carry them (mask meta in `ProjectSnapshot::comparisonMaskMeta`), and the
    project has an ID of its own (`projectId()`). Only the project file keeps them; a legacy
    load gives new IDs. A save with `ProjectFileOptions::stamp` gives each component whose
    content hash (`contentHash()`, of its entry without META) differs, or that has no revision
    yet, a new revision with the stamp's author and time, and hands those back for
    `adoptComponentMeta()`; a change undone by hand is a new revision too. GoT: +0.6 s.
  - `project/`: the editor's own project file, `<name>.cdmd`
    (`ProjectFile.h` documents the layout): a zip with JSON metadata (`Json.h`, the core's own,
    integers only) and one binary entry per component, references by UUID. Saves stamp
    revisions with `Settings -> Projects -> Your name` (`app/AuthorName.h`); the autosave
    (`<name>(auto).cdmd`, `app/Autosave.h`) writes without a stamp. Round trip: the same cROM byte for byte, for the fixtures of every layout and for
    the real projects (`COLORIZING_TEST_PROJECT=<cROM or cRZ>` runs it on one); the .cRPX
    written from it differs only in bytes the legacy tables carry without meaning (after a
    name's NUL, entries behind the last section or sprite), which the file does not keep. GoT:
    496 MB, saved in 20 s, opened in 12 s (its .cRZ and .cRPX: 546 MB, 34 s). The zip code
    is libserum's miniz (`ZipArchive.h`); it has to come into the editor when libserum 3.0
    drops the cRZ. The extension is an open question of the design document.
  - Packages (`edit/PackageImport.h`): `makePackage()` is a subset
    snapshot (masks with names, backgrounds, sprites, the light effects the frames start;
    no scenes, sections, palettes) written as a project file of kind "package" (`.cdmdpkg`).
    `planPackageImport()` finds known frames by ID, else by libserum's identification (the
    original's hash under a mask of the same pixels, the same shape mode), and reuses masks,
    backgrounds and sprites of equal content. `PackageImportCommand` keeps the document before
    and after (copies that share the blocks) and swaps them; it drops the histories of updated
    frames (`replacedData()`) and names what it adds (`renumberedRecords()`). The UI refreshes
    it as an edit batch. `Edit -> Copy Frames`/`Paste Frames` put a package on the clipboard
    (`app/FramesClipboard.h`, its own mime type, through a temporary file as the zip code
    works on files) for another editor window.
  - Teams (`core/team/`): `TeamState` (document/TeamState.h,
    in the project file as team/state.json and team/common.bin) holds the common revision of
    every component, the common frame order and settings hash, the packages sent and who
    merged them, and the partners' packages merged. Changed = revision differs from the common
    one, so Share and Merge need a saved copy. A save keeps the common version of each changed
    component in team/base from the file it replaces (`ProjectFileOptions::baseSourcePath`;
    the autosave has to pass the project file). A change package (`TeamChanges.h`, file kind
    "changes") is a closure like a package plus team/changes.json (base revisions, deletions,
    order, sections, merged packages). `planMerge()`: Take when this copy did not change it or
    has the package's base, Same, Add, Delete, and three conflicts; `mergeChangePackage()` is a
    `DocumentStateCommand` (shared with the package import) and moves the common state on;
    a sent package is common once every member listed it as merged. Order: the partner's when
    this copy kept the common one, else this copy's with added frames after their predecessor.
    Settings (license, scenes, lights, helper lines, sections) go as one. Claims
    (`Claims.h`, claims.json) are derived from the change set plus reservations; a claim names
    the claimer's saved revision (the frame order and the settings a hash), and blocks only a
    copy whose common revision differs (`blockingClaim()`): a merged change no longer blocks
    before its acknowledgement. An edit not saved yet claims without a revision.
    App side (`app/team/`): `GitHubClient` (REST and device flow over an `HttpTransport`;
    `NetworkTransport` is Qt Network, which the widgets library now links) and `TeamSession`
    (share, pending packages, fetch, claims sync with retry on a changed claims.json) over a
    `TeamRemote`: `GitHubRemote` (claims.json and packages/ in the repository; package names
    "<created ms>-<login>-<id>.cdmdchanges"; a package over 20 MB may go as a file another
    way, the repository then has its notice "<same stem>.cdmdnotice" and the partner adds the
    file to the cache, `addPackageFileToCache()`) or `FolderRemote` (tests). Tested against canned
    GitHub answers only. `TeamController` (Team menu, dialogs in `TeamDialogs.h`) runs the
    steps modally (`runModal`, no cancel: every request has a 30 s timeout). `TeamSync` keeps
    the claims and the pending packages in step in a thread of its own (own network access,
    every 15 s and 2 s after an edit of something new), downloads pending packages into the
    cache (QStandardPaths::CacheLocation/team/<repository>), and answers through invokeMethod;
    early claims: `admitEdit()` notes what an admitted edit touched until the next save. It
    guards edits: `MainWindow::pushToHistory()`
    reverts an edit of a partner's claimed component (a deletion is looked at by undoing and
    redoing it) and keeps the command until the event loop. Undo and redo are not looked at. Token and login in QSettings
    (github/token, plain). The GitHub OAuth app is PPUC's "PPUC Colorizing DMD" (client ID
    the default of CMake `COLORIZINGDMD_GITHUB_CLIENT_ID`, device flow on, tokens without
    expiry: the client does not refresh tokens); the environment variable of that name
    overrides it. Not run against GitHub yet. Packages check `--check-tls`.
  - `legacy/`: cROM/cRP/cRPX/cRZ readers, `LegacyProjectSerializer` (`writeLegacyCrom()`, the
    cROM of format version 1 for libserum), `LegacyCrzArchive` (reads through libserum).
  - `import/`: `FrameDumpReader` (streaming, strict reader of TXT frame dumps) and
    `FrameDumpImport` (durations, the import filter, removal of repeated frames and of frames
    the project has, a new project's snapshot), see "Dump import and same frames" below.
  - `edit/`: `EditCommand` (the undoable edits of record content), `StructureCommand` (adding
    and deleting records, see "Record management" below), `SameFrames` (which frames have
    the same hash under a mask and shape mode, the frames "delete same frames" removes, the
    pixels that differ among frames), `UndoHistories` (scoped stacks with a
    global sequence for chronological undo; drops histories that name renumbered records;
    groups make one user action one step across histories: `app/ActionUndoGroups` opens a group
    at an action's first push and closes it back in the event loop, every MainWindow push goes
    through `pushToHistory()`, and undo/redo refresh through the per-command callback),
    `DrawingTools`, `DrawingGeometry`, `FramePixelSelection`, `ColorModel` (RGB565/RGB888/HSV/hex
    conversions of the color picker) and `ColorSetEdits` (edit colors, color sets, the draw color
    mode, `EditorColorsCommand`, Edit <-> Dyn), see "Colors" below; `PlaneSelection` (the
    selection mask of a plane, stripes, ROM masks), `FrameContentCommand` (one frame's pixels,
    dynamic mask and dynamic sets as one undoable step, with `FrameContentBuilder`),
    `CopyPaste`, `AutoCopy`, `ScrollFill` and `RecolorHelpers`, see "Selection, copy and paste"
    below.
  - `render/`: `CanvasLayout`, `CanvasTypes`, `ProjectGeometry` (the one place that knows the
    plane layouts; `ProjectSnapshot` and `ProjectDocument` carry it).
  - `runtime/`: `RuntimePreview`, the libserum-backed renderer (the only other file that
    includes libserum headers). One instance per process, every call under one mutex, because
    libserum's state is process-global. `ProjectDocument::toSubsetSnapshot()` gives it a
    snapshot of the wanted frames and what they reference, with renumbered IDs. No cROM is
    written: `runtime/SnapshotSource.h` hands a snapshot to libserum's
    `Serum_LoadFromSource()` (libserum 2.99), answering its `fill()` calls with the
    bytes of single block elements (`legacy::CromElements`, the code `writeLegacyCrom()`
    writes every element with). A still is: load that subset, colorize once, dispose.
    `openProject()` (frame tester) and the cROMc export (`Serum_ConcentrateFromSource()`)
    hand over the whole project the same way; the export is libserum's own cROMc of the
    project's cROM, byte for byte (CromcExportTests). A sequence (playback) loads windows of at most 48 played
    frames with the other frames that take part in identifying them, and carries libserum's
    identification state from window to window, see "Runtime playback" below.
    `ProjectDocument::revision()` changes with every edit and lets callers cache results;
    `identificationRevision()` changes only with what frame identification depends on.
  - `runtime/FrameIdentification`: libserum's frame search rule (`Identify_Frame`) stated on the
    document, without libserum: which frames match a ROM frame, which one libserum identifies
    from a given search start, and which frames a window needs. Checked against libserum.
- `document/CompactArray`: the array type of every block of the records (pixel planes, masks,
  color and rotation tables): copy on write, and blocks that hold one value throughout share one
  process-wide buffer per size and value. See "Memory, load and save (2026-10-14)".
- `legacy/LegacyByteSource`: the sequential reader the cROM and cRP parsers read through, from
  memory or streamed from a file; reports progress and honours a cancel flag
  (`LegacyIoProgress` in `LegacyProjectIo.h`).
- `app/`: `MainWindow` (about 5,000 lines, down from 8,266), `CanvasEditController`, the canvas and
  sprite editor widgets, `FrameImageUtils` (the editor's own preview renderer), `RuntimeView`
  (state of the canvas's Runtime view: on/off, plane, playback, cached libserum output),
  `LazyListIcons`
  (frame list thumbnails built for the rows in view), `ListSelection` and `UnsavedChanges` (small
  pieces of MainWindow behaviour kept testable), `PlaneLabels` (the one place that decides
  where the UI says "HD"/"SD" and what a single plane is called), `EditColorKeys` (the keys of the
  edit color slots), ten panels under `panels/` (`EditColorBar` is the row under the toolbar,
  `ColorSetPanel` the `Sets` page), `PasteModeController` (clipboard and paste mode),
`EditHelpersController` (auto copy, scrolling fill, recolor, black to black, dynamic helpers),
`EditMenuActions` (the Edit menu section and its shortcuts), `dialogs/ProjectTaskDialog`
(`runProjectTask()`: open and save on a worker thread with a progress dialog),
`dialogs/SelectionDialogs`
(`AutoSelectDialog`, `AutoCopyDialog`)
  (`SameFramesPanel` is the lower part of the `Mask` page), `panels/RecordListPage` (a list of
  the left browser with `New` and `Delete` above it), `dialogs/DumpImportDialog` and
  `dialogs/ColorPickerDialog`.
- `tests/`: core test executable, widget test executable, `colorizing_qt_v2_roundtrip`
  (loads a real project; the cRP must be byte-identical with the file, the cROM of format
  version 1 written, read back and written again must be byte-identical) and
  `colorizing_qt_v2_preview_diff` (renders every frame of a real project with the editor's
  renderer and with libserum and reports the differing pixels by class; with `--playback` it
  plays the frames through the runtime preview's windows and through the whole project and
  compares every step).
- `core/platform/Utf8Path.h`: paths cross the core as UTF-8 `std::string`; every place that
  opens, renames or removes a file converts with `pathFromUtf8()` (see "Windows, Linux and CI").
- `packaging/`: the scripts that make the release packages (`package-windows.sh`,
  `package-linux.sh`, `package-macos.sh`), the macOS `Info.plist.in` and the Linux desktop
  file. CMake option `COLORIZINGDMD_QT_V2_PACKAGE` (off by default) builds the app the way they
  expect it: called `ColorizingDMD`, an app bundle on macOS, no console window on Windows.
- `tools/ui_dump/`: offscreen harness that drives the real `MainWindow` through about 785 scripted
  states and dumps widget state, document contents and canvas hashes. Used to prove refactors
  change nothing and to review intended changes. Read its README before using it.

## What works

- Load of cROM/cRP/cRZ/cRPX. Projects saved by 3.17.3 read losslessly: their cRP writes back
  byte-identical, and their cROM in format version 1 reads back and writes again identical
  (`colorizing_qt_v2_roundtrip`, 2026-10-26: `afm_113b`, `hook_501`, `frankst1`; before the
  format change byte-identical with the 3.17.3 cROM: also `trn_174h`, `smb3`, `potc_600as`, GoT,
  `empty128`).
- The cROM written recomputes frame hashes and active-frame flags and blanks colorized pixels
  under dynamic masks, as the Win32 editor does. Header revision 21, format version 1.
- Editing: drawing tools, flood fill, palette gradients, canvas selection, comparison masks,
  dynamic masks/colours/shadows, backgrounds, sprites (capture, slots, detection areas, HD),
  palettes, colour rotations, frame settings, image import, PNG export, playback.
- Undo: scoped histories, chronological within a context, covers pixel edits, masks, dynamic
  colours and shadows, palettes, frame settings, HD create/remove for frames and sprites, and
  adding and deleting records.
- Colors (2026-10-14): the 4 or 16 edit colors of the cRP under the toolbar, the 64 color sets on
  the `Sets` page (store, load, rename, deactivate), `Set Replace` drawing (color set replace) for
  points, shapes, flood fill and multi-frame edits on all three layouts and both planes, the
  editor's own color picker everywhere (Win32 `ColorPicker` fields, 8 recent colors), the pipette
  with `Shift`, `Edit > Dyn` / `Dyn > Edit`, keys for the slots. All undoable; untouched color data
  round-trips byte-identical. See "Colors" below.
- Selection, copy and paste (2026-10-14): a pixel selection of the edited plane made with `Sel`,
  `Ctrl`/`Cmd` (and macOS `Control`) + the tools, the fill tool, by color, from the background
  mask, by stripes, inverted and mirrored; copy with the dynamic mask and sets; paste mode with
  mouse, arrows, keypad, `K`/`L` mirror, `Enter`/click stamping on the selected frames, the
  originals-match and dynamic-mask-only variants; auto copy with a count before it runs (hash
  then exact compare); the scrolling auto fill; the recolor wand, black original to black and the
  dynamic helpers. One undoable step per frame. All three layouts. See "Selection, copy and
  paste" below.
- Record management: delete the selected frames, delete the selected frames shorter than N ms,
  add and delete sprites, backgrounds and comparison masks, all undoable, with every reference
  and the cRP tables following. See "Record management" below.
- New project from a TXT frame dump and adding the frames of a dump to the open project, with
  the Win32 import filter and removal of repeated and known frames; a new project is untitled
  until its first save. Same-frame tools on the `Mask` page. See "Dump import and same frames".
- 256x64-original projects: open, edit and save as native HD only (see below).
- 192x64-original projects: open, edit and save on the native 192x64 plane only (see below).
- Runtime view (`View -> Runtime View`, `Ctrl+R`, and a checkable `Runtime` toolbar button
  beside the playback controls): the canvas shows libserum's output for the current frame,
  32-row or 64-row plane, read-only; playback through libserum with its color rotations and with
  frame identification as on a machine. All three layouts. There is a `View` menu now; it holds
  the Runtime view and its two plane choices.
- `File -> Export Current Frame PNG...` writes libserum's output. The separate runtime export and
  the export of the editor's own rendering are gone. A frame libserum renders nothing for is not
  exported and the status bar says why.
- Runtime playback identifies frames like a machine with the whole project loaded. Checked over
  every frame of four real projects against libserum with the full cROM loaded: `afm_113b`
  3047, `hook_501` 3897, `smb3` 4430, `trn_174h` 5211 steps, no step identified or reported
  differently; `frankst1` 11199 steps against the search rule, no difference. A machine
  identifies 8, 26, 3, 48 and 4 of those frames as another frame than the one fed, which the
  view now shows.
- Frames the runtime skips are marked `inactive` in the frame list, with a tool tip, and have a
  filter entry (`Inactive`). The rule is the serializer's (`isLegacyFrameActive`) plus
  libserum's exceptions for an inactive frame with a background or a dynamic zone
  (`legacy::isFrameSkippedAtRuntime`).
- Dynamic data of an ordinary project is edited on the SD plane only, which is where the runtime
  takes it from; see "Dynamic data and the HD plane" below.
- The editor's own renderer gives the same pixels as libserum on the plane with the ROM frame's
  size: `afm_113b` 3047/3047 frames, `trn_174h` 5211/5211, `hook_501` 3897/3897, `smb3`
  4430/4430, `frankst1` sampled (`colorizing_qt_v2_preview_diff`).
- Color rotations: neither dynamic-zone content nor a dynamic shadow rotates, whatever its
  color; background, static and sprite pixels do. That is libserum's rule and the Win32
  editor's (`Predraw_Frame_For_Rotations` skips dynamic pixels, its draw skips `isdynapix`).
  The editor's rotation preview during playback follows it (`composeResolvedFrame()` marks the
  pixels that stand still, `applyColorRotations()` leaves them alone) and is compared with
  `Serum_Rotate` step by step in `EditorMatchesRuntimeTests`; on the real projects, three steps
  on every frame with a rotation: `afm_113b` 61 frames, `trn_174h` 2, no difference. Before, the
  preview rotated every pixel of a listed color, dynamic digits included. The Runtime view
  never rotates by itself: it shows the planes libserum holds after `Serum_Rotate`, during
  playback only.
- On the HD plane of an ordinary project the renderer follows libserum's layers (SD dynamic
  zones and colors, pixels doubled). What is left on `trn_174h` is the edge rounding of
  libserum's Scale2x: 114722 pixels in 4249 of 5211 frames (113940 at zone edges, 782 at sprite
  edges), down from 152166.

## Resolution model

A project's `render::ProjectGeometry` is derived from the cROM header on load (original plane
size, extra plane size, the "256x64 original" flag) and written back on save. It says which
stored plane has the ROM frame's size ("native"), which planes are edited, and how plane pixels
map to ROM frame pixels. Exactly three layouts are accepted; any other size combination is
refused on load with a message naming the sizes.

`Resolution::Sd` and `Resolution::Hd` name the stored planes (original and extra), not sizes.

- **128x32 original, optional 256x64 HD** (ordinary projects): sprites and detection on the
  128x32 original; HD is the upscaled plane. Fully supported.
- **128x16 ROMs** are stored padded to 128x32 by the Win32 editor (`hook_501`); they are ordinary
  projects to the Qt editor. A 128x16 dump is imported with that padding (8 black rows above
  and below). **Verified with a real 128x16 dump** (`hook_501 128x16.txt`), see "Dump import
  and same frames". libserum does not pad: `Serum_Colorize` reads `fwidth * fheight` bytes
  (128x32 for such a project), so the host has to hand it the padded frame. DMDext does
  (`Serum.Convert`: `PadSmallFrames`, `CenterFrame`, 8 rows above, as the editor); libdmdutil
  (`807c987`, `DMD.cpp`) passes the 128x16 update buffer as it is, which libserum then reads
  as a frame with the 16 rows at the top and 16 zero rows below (the rest of the zeroed
  `Update::data`), so it would not identify the centred frames of such a project. A host
  matter, not the editor's; reported, not changed.
- **256x64 original** (`is256x64`, revision 20): the 256x64 plane is the only rendered plane, one
  to one with the ROM frame. Sprite originals, detection areas, slot bounds and capture selection
  are native coordinates. Stored SD data is carried through untouched and never shown. There is
  no SD mode and nothing can be removed from the 256x64 plane. A frame renders at runtime only
  if it, its background and all its sprites have their 256x64 data; the editor flags frames
  where that is not so and `Create 256x64 Frame` / `Create 256x64 Sprite` repair them.
  **Verified with synthetic projects only; no real 256x64 project exists yet** (authors were
  waiting for a better editor).
- **192x64 original** (stored as a 192x64 original plane with a 96x32 extra plane): the 192x64
  plane is the only plane shown and edited, one to one with the ROM frame; it is the stored
  original plane, so internally it is `Resolution::Sd` data. Masks, dynamic data, backgrounds,
  rotations, sprites, slot bounds, capture, import and export all use 192x64 coordinates.
  **The 96x32 data is discarded on load** (owner decision): `LegacySnapshotBuilder` and
  `ProjectDocument::fromSnapshot()` drop it, the document holds none (`Resolution::Hd` access
  throws), and a save writes the extra-plane blocks as "no extra data" through
  `document::discardExtraPlane()`: every extra flag 0, colorized frames, backgrounds, dynamic
  colors, shadows, rotations, sprite colors and sprite dynamic colors 0, dynamic masks, sprite
  masks and sprite dynamic masks 255, background masks 1. These are the values the Win32 editor
  gives a new frame, sprite or background without extra resolution (`CopyTXTFrames2Frame`,
  `AddTXTFrames2Frame`, add sprite, `AddBackground`). Block sizes and the header (192x64 with
  96x32) are unchanged; nothing is padded or converted. A second save is byte-identical to the
  first.
  **Verified with one real project (`frankst1`, which has no 96x32 pictures in use, no sprites
  and no backgrounds) and with synthetic projects** that have them. `frankst1` differs from its
  file in two extra-plane blocks after a load and save: `HD dynamic colors` (688808 of 2866944
  bytes; the file has values there) and `HD background masks` (all 34403328 bytes; the file has
  0, the editor writes 1). The Win32 editor has both values for an unused extra background mask:
  1 for frames added from a TXT dump, 0 when it converts an old-format file.

**"HD" and "SD" in the UI** are shown only for the ordinary layout, where HD is an extra
resolution. For the two single-plane layouts the SD/HD buttons, HD badges, the HD counter, the
`HD frames` filter, `(HD)` suffixes and `Remove HD` are hidden, and the plane is called `256x64`
or `192x64`. A 256x64 project keeps the marking of items that lack the data the runtime needs,
worded without "HD": `no picture` (badge, list lines, suffix), `not rendered` (status lines,
filter `Not rendered`), repair `Create 256x64 Frame` / `Create 256x64 Sprite`. A 192x64 project
has no Create or Remove button. Details and the full table are in the Handbook, "SD And HD
Modes".

## Record management

- `ProjectDocument` has a `remove...()` / `insert...()` pair per record kind (frames, sprite,
  background, comparison mask) and `append...()` for the three that can be added. A removal
  hands back what it took out (`RemovedFrames`, `RemovedSprite`, ...): the records, moved and
  not copied, the frame references it cleared, and the cRP tables it changed
  (`RecordTables`: mask names, sections, the four sprite tables) as a whole, so that the insert
  restores every byte, used or not.
- `edit::StructureCommand` moves records between the document and itself. A deletion takes them
  again on every apply, so a redo keeps edits made after an undo. An addition is the same
  class the other way round. Factories: `deleteFrames` (any frame set, for the later
  "delete same frames"), `framesShorterThan`, `addSprite`, `deleteSprite`,
  `addBackgroundFromFrame`, `deleteBackground`, `addComparisonMask`, `deleteComparisonMask`.
- All structure commands are in one history (`structureHistoryKey()`), which is part of every
  undo context, so they are undone in reverse order and each runs against the numbering it was
  made for.
- History invalidation: every `EditCommand` says which records it names
  (`recordReferences()`, pure virtual: the frame, mask, background or sprite it edits and each
  ID it writes, e.g. the old and new background of a `BG ID` edit, the background of a
  `Create HD`). A structure command says from which index on a kind is renumbered
  (`renumberedRecords()`), and `UndoHistories` drops every history that holds a command naming
  such a record, on push, undo and redo. Dropped, not renumbered: what is left needs no proof
  beyond "no remaining command names a record whose number changed". A new undoable command
  that names a record (sprite slot edits, when they become undoable) has to list it in
  `recordReferences()`; a new command that changes the cRP tables in `RecordTables` (sections,
  sprite source data) has to be a structure command or the wholesale restore of those tables
  would undo it silently.
- Rules followed, with the Win32 source (`git show v3.17.3:ColorizingDMD.cpp`): `Delete_Frame`
  (4366), `Del_Section_Frame` (5279) and `Delete_Section` (5143), `Delete_Sprite` (5153), the
  `IDC_ADDSPRITE` handler (17957), `AddBackground` (21002), `DeleteBackground` (21079), the
  `IDC_MASKLIST` handler (16360), `IDC_DELFRAME` and `IDC_DELDURFRAME` (16564, 16585).
- Deviations from the Win32 editor, on purpose: a section emptied at the end of the project is
  removed (Win32 keeps it pointing behind the last frame); a table entry vacated by a deleted
  sprite, section or mask is cleared (Win32 leaves a copy of the last entry); a new sprite gets
  "no source rectangle" (`0xFFFF`) and colours 0 (Win32 leaves both as they were in memory); a
  sprite that is in two neighbouring slots of a frame is removed from both (Win32's loop skips
  the second); deleting a comparison mask exists at all; all frames cannot be deleted (Win32
  stops at the last one); every deletion asks first.
- Checked on real projects with a scratch program (not in the repository): delete a spread of
  frames, the most used sprite, background and mask, add one of each, save, reload, compare
  with expectations computed from the loaded file alone (the Win32 rules applied frame by
  frame). `smb3`: 76 frames deleted, 4354 compared, 923 sprite slots removed and 22496
  renumbered, 478 background IDs cleared and 143 renumbered, 625 mask IDs cleared and 930
  renumbered, 0 mismatches; `trn_174h`: 208 frames deleted, 5003 compared, 70 sections became
  65, 0 mismatches; `afm_113b`: 85 deleted, 2962 compared, 68 sections became 66, 0
  mismatches. For all three: the saved files round-trip identical, `preview_diff` reports the
  native plane identical for every frame, libserum loads the complete saved cROM (`smb3`
  played through, no frame unidentified), and undoing all commands serializes to the bytes of
  the original files.
- Memory: a frame deletion keeps the deleted `FrameRecord`s, moved. Nothing is allocated or
  copied (0.6 ms for 11198 frames of `frankst1`, 0.7 ms for the undo), and nothing is released
  until the command leaves the history. Before the compact blocks that was 62.6 KB per frame
  for `frankst1` (669 MB for all frames), 56.9 KB for `smb3`, 89.6 KB for `trn_174h`; with them
  a frame holds what is not uniform in it, mostly its original and colorized pictures: 12 KB for
  a 128x32 frame without HD, 44 KB with HD, 36 KB for a `frankst1` frame (about 400 MB for all
  of `frankst1`). The structure history is capped at 1 GB of held records
  (`UndoHistories::kDefaultStructureMemoryLimit`): beyond it the oldest structure steps are
  dropped (never the newest) and the status bar says so. Spilling held records to a temporary
  file, as the Win32 editor does for its undo buffer, is not built.

## Memory, load and save (2026-10-14)

Measured with a scratch harness (not in the repository):
release build, offscreen `MainWindow` for the app rows, M-series Mac with 16 GB. "Live" is the
heap in use (`malloc_zone_statistics`), "peak" the highest `phys_footprint` sampled every 2 ms.
The footprint of a process on this macOS does not go down when a large block is freed (a test
program that mallocs and frees 450 MB keeps 450 MB of footprint), so a `.cRZ`'s unpacked cROM
stays in the footprint after the open although it is freed.

| Project (cROM) | open time | open peak | live after open | save time (of it cRZ) | save peak | open peak, round-trip tool |
|---|---|---|---|---|---|---|
| `afm_113b` cRZ (267 MB) | 0.96 -> 0.91 s | 1163 -> 352 MB | 198 -> 81 MB | 1.44 -> 1.25 s | +171 -> +4 MB | 1.23 GB -> 337 MB |
| `trn_174h` cRZ (450 MB) | 1.69 -> 1.36 s | 2288 -> 801 MB | 476 -> 351 MB | 6.44 -> 6.36 s (5.6) | +454 -> +7 MB | -> 812 MB |
| `frankst1` cRZ (711 MB) | 3.23 -> 2.07 s | 4232 -> 1169 MB | 711 -> 436 MB | 8.49 -> 8.23 s | +754 -> +16 MB | 4.4 GB -> 1.2 GB |
| GoT-V50 cROM (1.67 GB) | 7.33 -> 1.97 s | 6780 -> 919 MB | 1684 -> 884 MB | 34.1 -> 33.6 s (31) | +1647 -> +20 MB | 2.7 GB -> 920 MB |
| `hook_501` cROM (327 MB) | | | | | | -> 62 MB |
| `smb3` cROM (396 MB) | | | | | | -> 85 MB |

Save peak is what a save adds to the live memory (before: the `ProjectSnapshot` copy). Before,
the load held the file (GoT: 2 GB, read through a growing vector), the parsed data (1.6 GB), the
snapshot built from it by copying (1.65 GB) and the document (copied once more in `MainWindow`)
side by side. A select-all pixel edit costs the same as before (GoT 0.98 s); a select-all
dynamic mask pixel materialises every frame's mask (GoT +67 MB, `frankst1` +134 MB).

What each change does:

- **Streaming load, moves instead of copies** (`LegacyByteSource`, `parseModernLegacyCromFrames
  (LegacyByteSource&)`, `parseLegacyCrpOriginalFrames(LegacyByteSource&, ...)`,
  `buildProjectSnapshot(&&, &&)`, `LegacyProjectFileReader`, `loadLegacyProjectDocument()`):
  a `.cROM` and the `.cRP` are parsed through a 1 MB buffer straight into the records; the
  records are moved into the snapshot and the snapshot into the document. A `.cRZ` is unpacked
  by libserum (`Serum_CRZ_Read`, one buffer of the cROM's size), parsed from that buffer without
  a copy (`CrzContent`) and freed right after. This is the open-peak share: GoT 6.8 -> 0.9 GB.
- **Compact blocks** (`document::CompactArray`): the steady-state share, 26 to 59 % less. Every
  block of the records (`IndexedFrame::pixels`, `Rgb565Frame::pixels`, dynamic colors and
  shadows, rotation tables, sprite dynamic colors) is a `CompactArray<T>`: the interface of a
  `std::vector`, storage behind a `shared_ptr`. A block whose values are all alike (the parser,
  `assign(n, v)`, `makeIndexedFrame(size, fill)`, and every vector adopted) points at one
  process-wide buffer per (size, value), kept for the life of the process (at most 32 MB of them,
  sizes up to 65536 values; beyond that a uniform block is stored like any other). **Defaults stay bit-exact because nothing is assumed**: the block holds the
  value it was given, so a background mask of 0 (`frankst1`, old converted files) and one of 1
  (editor default) are two different shared buffers, and the serializer writes whatever the
  block holds, through the same code as before. Reads never allocate. Any non-const access
  (operator[], `data()`, `begin()`, a resize) first gives the block a buffer of its own; the
  setters of `ProjectDocument` compare through a const view first, so writing a value a pixel
  already has materialises nothing. A block written back to all one value (an undo) stays
  materialised until `compact()`; nothing calls that yet.
- **Save without a copy**: `ProjectDocument::toSnapshot()` is unchanged in code but costs
  nothing now: the snapshot shares every block with the document (copy on write; GoT 20 MB for
  the record shells and the copied sprite slot lists). `writeLegacyCrom`/`writeLegacyCrp` and
  `LegacyProjectFileWriter` are the one save path for Save, Save As and
  `colorizing_qt_v2_roundtrip` (which now loads into a document, saves through `toSnapshot()`
  and compares the bytes as they are produced, holding no file).
  `toSubsetSnapshot()` for the runtime view gets the same benefit (`preview_diff` mean render of
  `smb3` 2.1 -> 1.7 ms).
- **Copy on write and threads**: const access is thread-safe; a copy may be read on another thread
  while the original is written, because a write to a shared block goes to a new buffer. The
  check is `use_count() == 1` followed by an acquire fence (the last other holder's release
  happens before the write); a copy clears the source's cached "exclusive" flag (a relaxed
  atomic). Pointers from const access stay valid until the next non-const access of that array.
- **Save on a worker thread** (`app::runProjectTask`, `ProjectTaskDialog`): the snapshot is taken
  on the UI thread, the Qt-free writer runs on a `std::thread` with a `LegacyIoProgress` (stage,
  bytes done/total, cancel). Decision: an application-modal progress dialog, not a document lock.
  The UI thread first waits up to 300 ms without handling any event (so a quick save is exactly
  the old synchronous call: ui_dump and every existing script and test see no difference), then
  shows the dialog, which blocks all input to the main window; no edit, menu or shortcut can run.
  Even if something did write the document, the snapshot would not see it (copy on write).
  Cancel is honoured between steps and at every 1 MB flush of the writer (an exception inside
  the serializer stops it at once); libserum's compression and unpacking cannot be interrupted,
  a cancel then takes effect when they return. A cancelled save removes the `.saving` files and
  replaces nothing; a cancelled or failed open keeps the project that was open. The writer's
  totals come from `legacyCromByteCount`/`legacyCrpByteCount` (exact, tested).
- **Open on a worker thread**: the same runner runs `loadLegacyProjectDocument()` (reader and
  `fromSnapshot`), the document is moved into `MainWindow` when it is complete.
- **Undo memory**: structure commands report the memory of the records they hold
  (`EditCommand::heldBytes()`, `document::ownedBytes()`); `UndoHistories::push` drops the oldest
  structure steps while the structure history holds more than 1 GB, never the one just pushed,
  and returns how many it dropped; `MainWindow::showStructureEditStatus` appends that to the
  step's status message. Dropping the bottom of a stack only shortens how far undo reaches (the
  remaining steps were made on top of the dropped one; tested: undo reaches exactly the state
  after the dropped step). Only the structure history is limited: the others hold changes.

Checks: `colorizing_qt_v2_roundtrip` identical for `afm_113b`, `trn_174h`, `hook_501`, `smb3`
and GoT-V50 (cROM and cRP); `frankst1` identical except the known `HD dynamic colors` (688808
bytes) and `HD background masks` (all 34403328 bytes). `colorizing_qt_v2_preview_diff`: native
plane identical, `afm_113b` 3047/3047, `smb3` 4430/4430. Tests: `CompactStorageTests` (shared
uniform buffers, first write materialises, copies share until written, a copy read on a thread
while the original is written, loaded default blocks shared in all three layouts, an edit
materialises only its block and its undo saves the original bytes, uniform blocks of non-default
values (masks of 7 and 0, colors 0xffff, rotations 0x0202) load as shared blocks of those values
and save identically, a shared save snapshot and a fully copied one (the old way, the oracle)
write the same bytes and the byte counts are exact, the streamed load reports every byte and
saves back identically, a truncated file, the byte source across buffer refills, a write
cancelled in the middle, the file writer's cancel and progress, the undo limit);
`ProjectTaskWidgetTests` (a quick task shows no dialog, an exception becomes the message, save
cancelled through the dialog: files and document unchanged and no temporary files; saved
through the dialog; a failing save writes nothing; open cancelled and opened through the
dialog; the dialog's texts and when Cancel is disabled). The tests hold the worker until the
dialog is up (`setProjectTaskWorkerStartHook`), so they do not depend on timing. ui_dump:
baseline of `HEAD`; the tree without the new script `REAL DIFFERENCES: 0` (896 sections, no
intended difference: quick saves and opens show no dialog); with `progress_script.inc`
(`--allow-appended`) `REAL DIFFERENCES: 0`, 5 appended sections (quick save, save cancelled,
saved through the dialog, open cancelled, opened through the dialog).

What remains:

- **A `.cRZ` open still holds the whole unpacked cROM** while it is parsed (libserum's
  `Serum_CRZ_Read` returns one buffer). A libserum function that streams the entry, e.g.
  `Serum_CRZ_ReadStream(path, callback, user)` on `mz_zip_reader_extract_to_callback`, would let
  the editor parse while unpacking with a small buffer: the open peak would be the document plus
  a few MB (`trn_174h` 800 -> about 360 MB, `frankst1` 1.2 GB -> about 450 MB), and unpacking
  and parsing would overlap. Not done: libserum is not to be changed here.
- **Save time is the cRZ compression** (GoT 31 of 34 s, `trn_174h` 5.6 of 6.4 s), now off the UI
  thread with a busy bar. Faster needs libserum: compress while the cROM is written (a streaming
  writer instead of `Serum_CRZ_WriteFromFile`, which reads the written file back), or compress in
  parallel. The level stays (owner decision).
- Opening a project keeps the old one until the new one is complete (both in memory for a
  moment), so that a failed or cancelled open leaves the old one. Releasing it first would lower
  the peak by the old project's size.
- Materialised blocks are never compacted again (`CompactArray::compact()` exists; e.g. after an
  undo or before a save it could be called on frames whose blocks went back to one value).
- Sprite slot tables (`spriteReferences`, 320 B per frame) are plain vectors.
- The peaks above are footprints, which on macOS include freed large blocks; Windows and Linux
  not measured.

## Dump import and same frames

**Format** (checked on `smb3.txt`, 105 MB, 25127 frames, and `serum-3mmy13yy.txt`, 34 MB,
8056 frames and a 19-row rest of a 8057th): a line `0x` + 8 hex digits (upper or lower case),
32 lines of 128 hex digits, a blank line, CRLF throughout. `smb3.txt` uses the shades 0, 3, 7,
B, F, has 2 time steps backwards and 21 gaps over 4000 ms.

**Reader** (`core/import/FrameDumpReader`): streaming over a 64 KB buffer, one frame in memory;
`smb3.txt` in 0.22 s at 1.7 MB peak. Strict: any other line is an error with its line (and
column); frames of mixed sizes are an error; a dump that ends inside its last frame drops that
frame and reports its line. Deviations from `Get_Frames_Ptr_Size_And_Number_Of_Colors`
(`master:ColorizingDMD.cpp` 12130), on purpose: the Win32 parser takes every `x` in the file as
the start of a frame, accepts any letter as a digit (values up to 35), and stops silently at
the first other character, keeping the frames before it.

**Import** (`core/import/FrameDumpImport`), semantics of `CompareFrames` (12261),
`CompareAdditionalFrames` (12332) and `Filter_Proc` (13205):

- Duration = next timecode minus this one; 30 ms (`DEFAULT_FRAME_DURATION`) if the next is
  smaller, more than 4000 ms later (`MAX_FRAME_DURATION`), or there is none. The last complete
  frame before a cut-off frame gets 30 ms too, as in Win32.
- Short frames: dropped if the duration was measured and is below the limit (5..3000). A
  defaulted duration is never dropped, whatever the limit.
- Colors: dropped if the frame has more distinct shades than the limit (1..16), counted on
  the stored frame (a padded 128x16 frame always has shade 0). Checked after the duration.
- Repetitions: by the plain CRC alone, as Win32 does (no pixel compare); the first frame that
  passed the filters stays with its own duration, the durations of the dropped ones are
  discarded. A different picture with the same CRC would be dropped; the runtime could not
  tell it apart either. Such cases are counted (`hashCollisions`; none in the real dumps).
- Against the project, option off: dropped if any project frame has the same plain CRC.
  Option on: project frames with a mask are compared under their mask and shape mode, frames
  in shape mode without a mask in shape mode, the others plainly, which is exactly "libserum
  identifies it" (`ProjectFrameMatcher`, cross-checked with `FrameIdentification::matches`).
- Deviations: a dump of one frame is compared with the project (Win32 returns early for
  `nFrames < 2`); the split of "already in the project" into identical and mask/shape is by
  rule, not by the first matching project frame in file order (the sum is the same); adding
  refuses another frame size and 16 shades into 4 colors (Win32 overwrites the project's size
  and color count from the dump); 4 shades into a 16-color project are taken as they are.
- The project's color count is 16 if any complete frame of the dump has a shade above 3,
  including frames the filter drops, as in Win32.

**New project** (`makeProjectSnapshot`, `document::makeUncolorizedFrameSnapshot`): defaults
from `CopyTXTFrames2Frame` (12496), `Init_cFrame_Palette2` (4099), `InitVariables` (4797):
picture = original in the plain colors on the native plane; shape mode 0; mask ID 255; dynamic
masks 255, dynamic colors and shadows 0; sprite slots 255 with the whole frame as bounds;
trigger 0xFFFFFFFF; rotations 0; background ID 0xFFFF, background masks 1; extra flag 0 and
extra picture 0 (ordinary, 192x64) or flag 1 with the picture (256x64, whose 128x32 plane gets
every second pixel); no masks, sprites, backgrounds; revision 20. cRP: all zero except the
plain colors at the start of palette 0 and as edit colors.
**Not verifiable:** no file of a project freshly created by Win32 exists here. The cRP defaults
assume a freshly started Win32 editor; one that had a project open keeps that project's color
sets, names and drawing settings (its `MycRP` is never cleared). In a 256x64 project the
sprite slot bounds are 255/63 where Win32 writes 127/31 (its `fWidth` is 128 there).

**Add frames**: `edit::addFrames` appends through `insertFrames`; one structure command, undo
removes exactly the added frames and restores the tables.

**Same frames** (`core/edit/SameFrames`): by hash under a rule, cached per rule until
`identificationRevision()` changes. Win32 `CheckSameFrames` (4506) compares pixels; equal up to
CRC collisions. The list is capped at 10000 for display as in Win32 (`MAX_SAME_FRAMES`); the
deletions are not capped (Win32 deletes from its capped list). The deletion loops follow
`IDC_DELSAMEFR` / `IDC_DELSELSAMEFR` / `IDC_DELALLSAMEFR` (16452 ff.), checked against a
literal simulation in the tests. `Common To Mask` (`IDC_COMMON2MASK`, 17134) replaces the mask
of the current frame; it does not need `Spot Common` to be on (Win32 does). `Spot Common` marks
the differing pixels (Win32 tints the common ones).

**UI**: `File -> New Project From Dump...`, `File -> Add Frames From Dump...`, one
`DumpImportDialog`. The dump is read by a `std::thread` that runs the Qt-free import only; it
shares three atomics with the dialog (bytes read, total, stop), a `QTimer` of the dialog shows
progress and notices the end. With a project the worker reads the document, so the dialog is
modal. `waitForAnalysis()` joins without the event loop, for tests and `ui_dump`.

**Real data** (scratch program, not in the repository):

- `hook_501 128x16.txt` (26.9 MB, a real 128x16 dump, checked 2026-10-14): CRLF throughout,
  lower-case timecodes (`0x00140abc`), 12838 frames of 16 rows of 128 digits, shades 0 to 3
  only, no truncated last frame (it ends with a blank line), time never goes back, 2 gaps over
  4000 ms. Reader: 128x16, stored 128x32, every stored frame has its rows 0 to 7 and 24 to 31
  black. 10476 different frames (2362 repetitions, no hash collision).
  - The existing `hook_501` project (3897 frames, 4 colors, 35 masks) stores its originals the
    same way: 3781 frames (one of them all black) have nothing outside rows 8 to 23; the other 116 (frames 217 to 228,
    405 to 427, 1236 to 1251, ... 2352) are 128x32 service menu screens (text from row 2 to 28)
    that came from a 128x32 capture, and one frame is all black. Every one of the 35 masks
    covers all of rows 0 to 7 and 24 to 31 (2048 pixels). 1327 dump frames have exactly the
    original of a project frame.
  - New project: 10476 frames, 4 colors, 128x32 with the 256x64 extra plane (no HD frames),
    durations mostly 16 ms (5771) and 15 ms (3461), none under 15 ms, so no frame is inactive.
    Save 1.3 s (cROM 879 MB, cRP 43 MB), reload serializes to the same bytes,
    `colorizing_qt_v2_roundtrip` identical for cROM and cRP, `preview_diff` native plane
    10476/10476 identical. libserum with the saved cROM identifies all 12838 dump frames, and
    each of the 10476 project frames as itself.
  - Added to the real `hook_501`: option on 9491 already known (66 identical, 9425 through a
    mask or shape mode) and 985 new; option off 564 known and 9912 new; 2362 repetitions
    either way. Per dump frame, against libserum with the unmodified cROM loaded: 11661 of
    12838 identified (90.8 %), exactly the frames the matcher with the option on calls known,
    0 disagreements (with the option off the matcher knows 1327 and libserum 10334 more). With
    the added frames libserum identifies all 12838 in both cases, and undoing the addition
    serializes to the bytes of the original file.

- `smb3.txt`: 25127 frames, 21671 different (3456 repetitions); import 0.4 s, 90 MB peak.
  `serum-3mmy13yy.txt`: 8056 frames, 3421 different; 0.12 s.
- `smb3.txt` added to the real `smb3` project (4430 frames, 24 masks): option off 383 known,
  21288 new; option on 9173 known (24 identical, 9149 through a mask or shape mode), 12498
  new; 1.0 s. All 25127 dump frames fed to libserum with the unmodified cROM loaded: 11469
  identified and 13658 not, the same frames the matcher says, no disagreement.
- New project from `smb3.txt`: 21671 frames, cROM 1.85 GB (serialized in memory only, to stay
  within the scratch limit); libserum loads it and identifies all 21671 as themselves, 102 of
  them inactive (shorter than 15 ms). From the first 10000 dump frames, on disk: 8925 frames,
  save 3.8 s, reload serializes to the same bytes, `colorizing_qt_v2_roundtrip` identical for
  cROM, cRZ and cRP, `preview_diff` native plane identical (36 inactive frames have no
  picture in libserum).
- `afm_113b`, frames 0, 16, 500, 1200, 2900 under their own rule and under no mask and masks
  0 to 2 with and without shape mode (45 cases): the same-frame list equals the frames
  libserum identifies with a colorization of that frame alone.

## Colors

Win32 reference
`git show v3.17.3:ColorizingDMD.cpp` (3.17.3).

What the Win32 editor does and what the Qt editor does:

- **Edit colors** (`MycRP.acEditColorsS[16]`, `IDC_COL1..16`, toolbar handler 16188): a click
  selects slot `noColSel` (the draw color is always an edit color there) and opens the palette
  popup (`Choose_Color_Palette`, `PalProc` 14011/14081), where a click sets the edit color
  (`SA_ACCOLORS` undo). Qt: the row of `EditColorBar`, as many swatches as the project has colors,
  each with the strip of its shade; click selects the slot and makes its color the draw color;
  double click / context menu opens the picker; a palette color dropped on a swatch sets it. The
  draw color stays free (favorites, palette, pipette) and a slot is "selected" only while the draw
  color came from it; a selected slot's draw color follows its edit color.
- **Draw color mode** (`MycRP.DrawColMode`, `IDC_1COLMODE` / `IDC_4COLMODE` / `IDC_GRADMODE(B)`,
  16241 ff.): 0 single, 1 set replace, 2 gradient; saved in the cRP, the loader turns 2 into 0
  (12038). Qt: `edit::DrawColorMode` (single, replace) is app state taken from `drawColorMode` on
  open (1 -> replace, anything else single; `frankst1` is stored in mode 1 and opens in it) and
  written back only when the user changes the mode, so a stored 2 survives an untouched save
  (byte identity) while it reads as 0. Not undoable, not dirtying. Mode 2 is the `Grad` tool.
  `drawMode` and `fillMode` (Win32 tool and Fill checkbox) are not used: the Qt tools differ
  (separate filled tools), so their meaning does not match; they are written back unchanged.
- **Set replace pixels** (`ConvertSurfaceToFrame` 5417 for shapes and fills, the point handler
  22701 / 23106, Alt-click "same color" 23291): `acEditColorsS[oFrames[...]]`; in HD
  `oFrames[y/2][x/2]` (ordinary), `isDel` (Shift) writes `originalcolors[...]`. Qt:
  `edit::colorSetReplaceColorSource(document, frame, plane)` maps a plane pixel through
  `ProjectGeometry::toRomFrame()` (libserum's layout, not the Win32 256x64 code, which indexes a
  256x64 `oFrames` with 128x32 coordinates). Deviations: the Win32 point tool uses the current
  frame's shades for every selected frame (`acFrame`), the shapes each frame's own; Qt uses each
  frame's own everywhere. Shift does not erase frame pixels in Qt (unchanged). Backgrounds (no
  Win32 equivalent) take the current frame's shades. Sprite painting ignores the mode, as
  `ConvertSurfaceToSprite` does. The Alt-click "recolor all pixels of this color" is the
  `Recolor` helper now (see "Selection, copy and paste"), in both modes.
- **Flood fill region** (`drawfill` / `floodfill` 6265, 6328): the region is the current frame's
  connected pixels of the clicked color and dynamic set, whatever the mode; the color comes later
  per frame. Qt keeps its region rule (current frame, clicked color; the dynamic mask is not part
  of it, as before) and colors per frame. The single-color early return when the area has the
  draw color already does not apply in replace mode.
- **Selection fills**: the only selection fill is `Grad Sel` (palette gradient), which like the
  Win32 gradient mode ignores the color mode. The Win32 editor has no fill of its copy mask with
  the draw color either (its shapes are drawn, the mask is only copied); none was added.
- **Color sets** (`ColSet_Proc` 14617, `activeColSet`, `ColSets`, `nameColSet`, `preColSet`,
  `acColSet`): "Set" stores the first `noColors` edit colors and activates; a click on an active
  set loads `noColors` colors (`SA_ACCOLORS`); the name field of an active set renames it
  (`strcpy_s`, `SA_COLSETS`); scrolling sets `preColSet`; `acColSet` is only loaded and saved,
  never changed. Qt: `ColorSetPanel` (`Store`, `Load` / double click, name field, `Deactivate`,
  which Win32 lacks); `firstDisplayedColorSet` follows user scrolling only (clamped to 56 as
  the Win32 dialog), without dirtying; `currentColorSet` is never written (selects the row on
  open). A rename zero-fills the 64-byte entry (Win32 `strcpy_s` leaves the bytes after the NUL;
  only renamed entries are affected).
- **Picker** (`ColPickProc` 3498, `ColorPicker` 3811): HSV square/bar, RGB888 hex, R8/G8/B8,
  RGB565 decimal, R5/G6/B5, H/S/V as integers, 8 `previousPickedColors` (not persisted).
  Qt: `ColorPickerDialog`, same fields; H/S/V with one decimal (with integers 10,000+ RGB565
  colors do not survive re-entry; Win32's truncating `hsv_to_rgb888` drifts too); recent colors
  in `QSettings` (`colorPicker/recentColors`). Used for palette, dynamic, shadow, rotation,
  favorite (new context menu) and edit colors; `QColorDialog` is gone.
- **Pipette** (`Color_Pipette`, 22930 ff.): picks the composed color (background, dynamic,
  static); without Shift it writes that color into the selected edit color (undoable), with
  Shift it selects the last slot whose color matches. Qt: picks the stored colorized pixel as
  before and sets the draw color; with Shift selects the last matching slot (or says that none
  matches). Deviation: no write into the edit color without Shift.
- **Edit <-> Dyn** (`IDC_COLTODYNA` 16891, `IDC_DYNATOCOL` 16910): copy `noColors` colors into
  `acDynaSet` of all selected frames / from the current frame, on the edited plane. Qt: on
  `dynamicPlane()`, one `DynamicColorsEditCommand` per changed frame (new: several colors of one
  set in one step, same history as `DynamicColorEditCommand`), `Dyn > Edit` one
  `EditorColorsCommand`.
- **Keys** (`SCKeyState` 21571, 22198 ff.): scan codes 2-5, 16-19, 30-33, 44-47 while the frame
  window has focus in colorization mode; letters only for 16 colors and without Ctrl/Alt/Shift.
  Qt: same physical places while the frame canvas has the focus (`EditColorKeys`): macOS virtual
  key codes, Windows scan codes, XKB key codes on X11/Wayland, otherwise Qt keys read as QWERTY.
  On macOS a real key event is told from a synthetic one by its native modifier flags (the virtual
  key of `A` is 0); only checked on macOS with synthetic events. No conflicts: the editor has no
  single-key shortcuts except `Delete`/`Backspace` on the frame list. Not configurable yet.

Undo: one history `EditKind::EditorColors` (index 0) for edit colors and color sets together,
`EditorColorsCommand` holds the four tables before and after (6.4 KB per step) and names no
record, so structure commands never drop it. It is part of the default, `Dynamic` and sprite
canvas contexts (all places edit colors are used), not of the comparison and background mask
contexts. The Win32 editor has two records (`SA_ACCOLORS`, `SA_COLSETS`) in its one global undo.

Real projects (scratch programs, not in the repository):

- `afm_113b` (4 colors): edit colors `0000 5140 a280 fbe0` (the plain ramp); set 0 active named
  "150" (a grey ramp), sets 1 to 17 active but all zero, names "", "9" to "17" (leftovers of the
  `%02i` numbers), 7 entries with bytes after the NUL; set 18 inactive with a name. Mode 0.
- `hook_501` (4 colors): 13 real sets, named "White", "Red", "Green Dark", "Green Light", "Blue",
  "Cyan", "Yellow", "Gold", "Brown", "Maroon", "Skin", "11", "12" with 4 colors each. Mode 0.
- `trn_174h` (16 colors): edit colors the plain 16-step ramp; sets 0 to 3 with 16 colors (a
  modified ramp, red, grey, blue-grey), 4 to 12 active and zero, 13 named "13". Mode 0.
- `smb3` (16 colors): no set used, plain ramp. Mode 0.
- `frankst1` (4 colors, 192x64): sets 0 to 8 active and zero; mode **1** (opens in `Set
  Replace`).
- All have `drawMode` 2, `fillMode` 1, `currentColorSet` 0, `firstDisplayedColorSet` 0.
- For all five: the cRP serialized from the loaded document is byte-identical to the file;
  after an edit color change, store, two renames, load and deactivate it differs, and after
  undoing all of them it is byte-identical again.

ui_dump (2026-10-14): baseline of `HEAD` (808 sections, deterministic); the tree with the colors
UI but without `colors_script.inc` against it with `--ignore-geometry`: added widget lines (the
row under the toolbar, the `Sets` page), the tab list with `Sets`, scroll bars of some pages no
longer visible and the tab bar's arrow enabled (the window is taller), the row's buttons in the
focus chain after `Runtime` (8 focus chain sections), the `keys=` list of 709 sprite state lines
with `12/0/sd/00` appended, nothing else (no document, status, thumbnail or private-state line);
that dump as the baseline and the full script with `--allow-appended`: `REAL DIFFERENCES: 0`, 42
appended sections.

## Selection, copy and paste

Win32 reference
`git show v3.17.3:ColorizingDMD.cpp` (3.17.3); line numbers are of that file.

**Selection** (`Copy_Mask`; `Add_Surface_To_Copy` 4647, `GetSelectionSize` 10141, mouse handling
23132 ff. and 23460, W filter 22784): Ctrl+left with the point tool marks pixels, with the shape
tools and the fill tool (`drawfill`) on release; Shift removes; holding W keeps pixels whose
colorized color is 0 out (the check is on `cFrames`, the colorized frame, not on the original).
Ctrl+A / Ctrl+D (21881, 21904) select all / none, Ctrl+B (21934) copies the frame's background
mask, Ctrl+K / Ctrl+L (22002) mirror, `IDC_INVERTSEL2` (16950) inverts, `IDC_DOAUTOSEL` (17573)
makes stripes (offset 0..256, selected and skipped 1..256; `IDC_VERTSEL` checked gives columns),
Alt+Ctrl+click with the fill tool (23216) adds every pixel of the clicked stored color that is
not dynamic. All record `SA_COPYMASK` undo steps. Qt: `edit::PlaneSelection`, held by
`CanvasEditController` (the existing `Sel` selection, generalised: its pixels are a mask now; the
gradient box is the drawn shape or the selection's bounds). Same gestures; `Ctrl` is Qt's
ControlModifier (Cmd on macOS) and MetaModifier (the macOS Control key) works too; `W` is tracked
on the canvas and there is a menu toggle `Select Non-Black Pixels Only`. Deviations, on purpose:
the selection is dropped on a change of plane, target or background, and on a change of frame
unless the frames are stepped with Left/Right or several are selected (issue #31; Win32 keeps
one global mask, which authors found in the way of a paste); it is not undoable (Win32 records it);
select by color compares the composed colors the canvas shows, dynamic pixels included (Win32:
stored colors, dynamic pixels skipped) and is also in the menu (`Select By Color`, then a
click). Selection editing is off in the mask modes and in sprite capture (Win32: colorization
mode only).

**Copy** (`IDC_COPY` 16629): refused when the selection was made on the other plane
(`Copy_Content`); fills `Paste_Mask`, `Paste_ColN` (colors), `Paste_Colo` (original shades) and
`Paste_Dyna` (dynamic mask) cut to the bounding box. The Win32 HD branches index `Copy_Colo` with
mixed widths (16680 ff.); Qt maps every plane pixel through `ProjectGeometry::toRomFrame`. Qt:
`edit::copySelection` -> `CopyBuffer`; dynamic data only on `dynamicPlane()`; the colors and
shadow of the dynamic sets used are taken at copy time (Win32 `CopyDynaCol` 3021 reads the
source frame at paste time). A PNG of the colors goes to the system clipboard.

**Paste** (`IDC_PASTE` 16754, Ctrl+V 22050, stamping 22990, Enter 22192, keypad 21679 ff.,
`Draw_Paste_Over` 6506): the box follows the mouse, centred; a click stamps on every selected
frame and ends paste mode; Alt = dynamic mask only (not mirrored in Win32), Shift = only where
`Paste_Colo` equals the target's original (`condpaste`), otherwise colors + mask + `CopyDynaCol`;
frames without HD skipped in HD mode; K/L mirror only when held as paste starts. Qt:
`PasteModeController` + `edit::makePasteChanges`; starts at the copied place; arrows (Shift =
10) and keypad 2/4/6/8 nudge; K/L toggle while pasting; Enter/click stamp, Esc cancels; variants
from the menu or Shift/Alt at the stamp; the dynamic-only variant is mirrored; one
`FrameContentCommand` per changed frame. Its history is the frame's pixel history of the plane,
or in the `Dynamic` mode its dynamic mask history, so the step is undone where it was made (the
undo contexts are unchanged). Pasting refuses another plane and another project layout with a
message (Win32 checks only at copy time). The clipboard is emptied when a project is opened.

**Auto copy** (`IDC_AUTOCOPY` 17379, `AutoCopy` 9832, `ConvertMaskToSourceReso` 9790): masked
CRC32 (`crc32_fast_mask`) of every frame's original over the selection in ROM pixels; every
other frame with the current frame's CRC gets `cFrames` and `DynaMasks` of the selected pixels
(HD: `Copy_ColN` / `Copy_Dyna` from selection time). No undo, no question, all frames; the
256x64 and HD branches are not followed. Qt (`edit::scanAutoCopy`, `makeAutoCopyChanges`): the
same hash finds the candidates, then every candidate is compared pixel by pixel; the colors of
the selection's plane from the current frame, the dynamic mask only on the dynamic plane, no
dynamic set colors (as Win32); frames without the plane are skipped and counted; a dialog shows
the counts first and offers "selected frames only". Tested with a constructed CRC32 collision
(Gaussian elimination over GF(2): a 20-pixel difference of shades 0..3 whose CRC is that of
zeros), which the hash takes and the compare drops.

**Scrolling auto fill** (`IDC_SCROLLFILL` 17297, `AutoFillScrolling` 8020, `CopyRows` 7949,
`CopyCols` 7983): well defined and implemented completely: the run of selected frames around the
current one; for each frame between, the first match in the order camera down, up, right, left,
smallest shift first, of whole rows/columns (the frame's lines 0.. are the reference's lines
shift.., or the reverse); the shared rows/columns (colors and dynamic mask) are copied from the
first frame, then from the last. Deviations: lines are compared exactly instead of by CRC; the
shift is multiplied by the plane's scale (Win32's 256x64 branch doubles it on the native plane);
the dynamic mask moves only on the dynamic plane; frames without the plane take no part; a
question with the counts comes first; undoable per frame. As in Win32: one shared line at the
largest shift is a match.

**Recolor helpers**: the wand (Alt+click with the fill tool, 23264: every pixel of the clicked
stored color on each selected frame gets the draw color, or in set replace the edit color of its
shade; refused on a dynamic pixel), Ctrl+O (21958; here `Ctrl+Shift+O`, as Ctrl+O is Open; the
ROM pixel through `toRomFrame`, not `ValuePlus2x2`), Ctrl+E (22056) and Alt+D (22111): mask 255
and the plain color (`MycRP.Palette`, which is `plainOriginalColors`) under the removed dynamic
pixels; Alt+A (22164): the active set. Deviations: black to black and the dynamic helpers work on
the selection when there is one on the plane, else on the whole frame (Win32: whole frame); the
dynamic ones only on `dynamicPlane()`.

**Shortcuts** (`EditMenuActions`): Copy `Ctrl+C`, Paste `Ctrl+V`, Paste Where Originals Match
`Ctrl+Shift+V`, Paste Dynamic Mask Only `Ctrl+Alt+V`, Select All `Ctrl+A`, Select None
`Ctrl+Shift+A` and `Ctrl+D`, Invert `Ctrl+Shift+I` (a plain `I` cannot be a menu shortcut: a
native menu bar takes it from every widget), Selection From Background Mask `Ctrl+B`, mirror
`Ctrl+K` / `Ctrl+L`, Black Original To Black `Ctrl+Shift+O`, Remove Dynamic Zones `Ctrl+E` and
`Alt+D`, Assign Dynamic Set `Alt+A`; Select By Color, Auto Select..., Auto Copy..., Scrolling
Auto Fill... and Recolor... have no key. Ctrl is Cmd on macOS, Alt is Option. Copy, Paste and
Select All go to a focused line edit, spin box, text edit or item view first
(`forwardStandardKey`), so `Ctrl+A` in the frame list still selects all frames. No shortcut of
the window is shared (the ui_dump selection script prints them). Checked with synthetic events
and offscreen only.

**Real projects** (scratch program, not in the repository; auto copy applied as the dialog
would, then saved to scratch, reloaded and checked):

- `afm_113b`: frame 0, rows 0..7: 309 frames match, 307 change (a blank area); frame 500, rows
  24..31: 1 match, 1 changes; frame 1200, 64x16 at 32,8: 4 match, 0 change (already colored that
  way); frame 16 left half and frame 2900 whole frame: none. 308 steps, no CRC collision, scans
  of 8 to 25 ms. The saved project re-serializes to its bytes, `colorizing_qt_v2_roundtrip`
  identical (cROM, cRP), `preview_diff` native plane 3047/3047 identical; undoing every step
  gives the original bytes.
- `smb3`: frame 2000, 16x8 at 112,24: 20 match, 5 change; frame 2000, 16x8 at 56,12: 202 match,
  165 change; frame 1000, 16x8 at 112,0: 34 match, 33 change; 203 steps, no collision, scans of
  about 5 ms. Same checks: round trip identical, `preview_diff` 4430/4430 identical, undo back
  to the original bytes. Larger areas of `smb3` frames match nothing; 16x8 corner blocks match
  up to 1986 frames, mostly black ones.

**ui_dump**: baseline of `HEAD` (850 sections, deterministic); the working tree without the new
script: `REAL DIFFERENCES: 0` (the new entries are menu actions, not widgets, so no intermediate
baseline was needed); with `selection_script.inc` appended and `--allow-appended`: `REAL
DIFFERENCES: 0`, 45 appended sections.

## Dynamic data and the HD plane (ordinary projects)

libserum renders the 64-row picture of a 128x32 colorization in layers (`Colorize_Framev2`,
`layerMode = upscaleExtraFromOriginal`, which `ConfigureRequestedOutputMode` sets whenever the
content is 32 rows high and 64 rows are requested): the HD frame and HD background natively,
the dynamic content at 128x32 from the SD mask and SD colors, upscaled once. `frameHasDynamicExtra`
is `!layerMode && ...`, so the HD dynamic mask and colors are never read. Owner decision: the
editor does not offer them.

- `ProjectDocument::dynamicPlane()` is the plane dynamic data is edited on, the native plane.
  The four dynamic setters throw `std::logic_error` for the other plane, so no edit command can
  be applied to stored HD dynamic data and none reaches an undo history. Reading stays possible.
- HD mode: the `Dynamic` button is disabled with a tool tip, switching to HD switches the mode
  off, and the `Dyn` page shows and edits the SD colors and shadows with a note. Chosen over
  drawing the SD zones doubled over the HD frame, read-only: an overlay that looks paintable and
  is not misleads more than a disabled button with its reason, and the HD picture already shows
  what the zones do.
- `Create HD` has no "dynamic mask and colors" option any more and leaves the frame's stored HD
  dynamic data as it is (mask 255, colors and shadows 0 for a frame that never had any).
  `FrameHdParts::dynamic`, `FrameHdState::dynamic` and `upscaleDynamicMaskToHd()` are gone.
- Stored HD dynamic data is loaded and written back unchanged.
- The HD picture of an ordinary project is given to libserum as it is: the rule that writes
  colorized pixels under a dynamic mask as 0 holds for the plane of the ROM frame only (owner
  decision 2026-10-26). A stored HD mask, which nobody sees, blacked out HD pixels before.
- **Not ignored by libserum: HD dynamic shadows.** In layer mode a set without an SD shadow
  direction takes direction and color from the HD shadow tables (`CheckDynaShadow`,
  `GenerateExtraPlaneShadows`). The renderer does the same; the `Dyn` page edits the SD shadow
  only. See open decision 3.
- HD color rotations and HD background masks are used by libserum in layer mode (the HD pass
  reads `colorrotations_v2_extra` and `backgroundmask_extra`) and are unchanged in the editor.

## Runtime playback

libserum identifies by order: the search starts at the frame identified last and walks the
frames in file order; the first (mask, shape mode) pair it meets that has a frame with the
incoming frame's hash decides, and of those frames it takes the first at or after the place it
met the pair (`Identify_Frame`, `SelectFrameIdInWrapOrder`). So frames that do not match take
part too, by deciding which pair is met first, and loading the played frames as one subset in
project order is not enough.

The rule implemented (`FrameIdentification::candidates`, `RuntimePreview::openSequence`): a
window holds (1) the search start and every frame, in any pair, that matches the original of a
played frame, and (2) for each of those and each of their pairs the first frame of that pair at
or after it; in project order, rotated to begin with the search start. From window to window
the state is carried by that rotation and by feeding the last identified ROM frame once more.
libserum does not say which frame it identified when it reports "same frame" for a frame whose
picture is already on display, so the search start is then taken from `FrameIdentification`,
which the tests and `colorizing_qt_v2_preview_diff --playback` compare with libserum wherever
libserum does report it.

Cost: windows of 49 to 86 frames on average (160 at most), 3 to 8 MB of cROM, 15 to 100 ms to
load; building the search rule of a project once per change of masks takes 16 to 30 ms, 270 ms
for `frankst1`. Memory stays that of a window.

Background scenes (owner decision 2026-10-26): a frame that starts one needs no background of
its own. libserum lets a scene through the background mask only of a frame with a background
(which it then takes for a placeholder), so `runtime::addSceneBackgroundPlaceholders()` gives
such frames a black one in every snapshot handed to libserum (`SnapshotSource`); the project
keeps none. The canvas shows the scene's first frame through the mask
(`ProjectDocument::backgroundPicture()`), upscaled on HD as libserum does.

## Open decisions (waiting for the owner)

Only questions without an answer are listed; each says what the editor does now. An answered
one is removed.

### Formats and libserum

1. **Unused extra background mask of a 192x64 project.** The 96x32 data is discarded on load
   (decided). Which value should an unused extra background mask get on save: 1 (what the editor
   writes now, as Win32 does for new frames) or 0 (what Win32 writes when it converts an
   old-format file, and what `frankst1` has)?
2. **256x64 monochrome trigger and repaired backgrounds.** libserum reads the monochrome palette
   trigger's palette from the HD dynamic colour set, and a background repaired by
   `Create 256x64 Frame` becomes black. Are both intended?
3. **HD dynamic shadows of ordinary projects.** libserum falls back to a set's HD shadow
   direction and color when the set has no SD direction; the editor shows the result but cannot
   edit or switch it off (only replace it with an SD shadow). Which: (a) leave it, (b) show the
   fallback on the `Dyn` page, or (c) copy the HD shadow into the SD table when the frame is
   first edited?
5. **The 15 ms rule.** A frame shorter than 15 ms that is not colorized is saved inactive (as
   Win32); such frames are marked `inactive`. Keep the rule?
6. **`trn_174h` HD plane.** The canvas differs from libserum in 114,722 pixels of 4,249 frames
   (re-measured 2026-10-22), all edge rounding of upscaled dynamic text; only the Runtime view
   shows it exactly. Should the canvas (a) stay as it is, (b) say that the Runtime view is exact
   for upscaled dynamic content, or (c) reproduce libserum's upscaling?

### Behaviour to confirm with the authors

7. **Sprites, monochrome and HD:**
   - The sprite bounds fields are disabled for an empty slot, so a rectangle capture into an
     empty slot needs `Assign` or `Select Capture` first. Enable them for an empty slot?
   - Switching monochrome off on a multi-selection clears the trigger only on frames that carry
     the monochrome trigger, not other triggers. Right?
   - Undoing `Create 256x64 Frame` or `Remove 256x64 Frame` also discards the HD pixel edits made
     after it. Acceptable?
   - With `Dynamic` active, Undo does not reach the frame settings; `Undo Last Edit Anywhere`
     (`Ctrl/Cmd+Alt+Z`) does. Enough, or should the `Dynamic` context include them?
8. **Records:**
   - Deleting a comparison mask that frames use asks first, naming the frames, which are then
     compared without a mask. Since `Using the selected asset` lists those frames, the deletion
     could be refused while the mask is in use instead. Ask or refuse?
   - A section left empty at the end of the project is removed. Right?
   - `Delete` and `Backspace` delete the selected frames (after the question) while the frame
     list has the focus. Right?
   - `Delete Frames Shorter Than...` proposes 15 ms. Right?
   - A new background takes the stored colorized frame (Win32 `AddBackground`); `Frame ->
     Background` copies the composed picture (with sprites and dynamic colors). Keep both?
9. **Dump import and same frames:**
   - A bad line anywhere but in the last frame refuses the whole import (the damaged last frame of
     an interrupted recording is left out). Win32 silently keeps the frames before the bad line.
     Keep refusing?
   - A 4-shade dump is accepted into a 16-color project. Right?
   - `Common To Mask` writes into the mask assigned to the current frame and replaces its content
     (as Win32), not into the mask selected in the browser. Right?
   - Same-frame deletions are not capped at 10000 per frame (Win32 is). Right?
   - The same frames are worked out only while the `Mask` page is shown. Right?
   - A new project made from a dump is proposed for saving beside the dump. Right?
10. **Colors:**
    - The draw color is free (Win32: always one of the edit colors, which the pipette writes
      into). Keep it free?
    - Set replace uses each selected frame's own shades, also for `Point` (Win32: the current
      frame's); a background in set replace takes the current frame's shades. Right?
    - A color mode change is not a project change (not undoable, does not mark the project
      modified), and a stored mode 2 is kept until the mode is changed. Right?
    - A renamed color set's name entry is zero filled. Right?
    - The color keys work only while the canvas has the focus. Right?
11. **Selection, copy and paste:**
    - The selection has its own undo (`Undo Selection`, `Alt+Z`, 50 steps) instead of being
      in the project's Undo (Win32 records it there). Enough?
    - Select by color takes the composed colors, including dynamic pixels. Right?
    - Paste ends after one stamp (as Win32) unless `Edit -> Stay In Paste Mode` is checked
      (off at first). Right default?
    - The paste of a dynamic mask alone is mirrored with the selection. Right?
    - Dynamic data is never copied, pasted, auto-copied or scrolled on the HD plane of an
      ordinary project, since the SD zones would need their own placement. Right?
    - Black-to-black and the dynamic helpers act on the selection when there is one (Win32: on
      the whole frame). Right?
    - `Black Original To Black` is `Ctrl+Shift+O`. Right?

### Answers of one author (2026-10-22)

The owner relayed the answers of one author (Enthusiast) to a questionnaire built from the
items above; other authors may see it differently, so the items stay open. Numbers are those of
the author's questionnaire; unanswered ones are left out.

As the editor does it now (confirmed by this author):

- Set Replace uses each selected frame's own shades for all tools (4); the mode switch (6) and
  the edit color keys (7) stay as they are.
- Select by color (10), the helpers on the selection (13), `Ctrl+Shift+O` (14).
- Deleting a used comparison mask after the question (17); `Delete` in the frame list (21);
  15 ms for `Delete Frames Shorter Than...` (22); new background against `Frame -> Background`
  (23).
- Monochrome off on several frames (26), undo of Create/Remove HD (27), frame settings not
  undone in `Dynamic` (28), sections after deleting the last frames (29).
- HD pixels under hidden HD zones saved as 0 (32, item 4: keep the rule); the owner dropped
  the rule for the HD plane of ordinary projects on 2026-10-26 (GoT-V79 frame 112 lost its HD
  border under a stale HD mask).
- A dump with a broken line refused (39), a 4-shade dump in a 16-color project (40), known
  frames left out by default (41), the first save of a new project asks where (42).

Asked for a change:

- Colors (item 10): the draw color is always an edit color (1); `Pick` writes into the selected
  edit color (2) and takes the color as seen, i.e. the composed picture (3). All three as Win32.
- The 15 ms rule (item 5): a limit per project instead of the fixed 15 ms (24). It would be
  editor data (a `.cRPX` chunk); the cROM is unchanged.
- `trn_174h` edges (item 6): the canvas should show the smoothed edges of upscaled content
  (33), i.e. reproduce libserum's upscaling.

### Newer work

12. **Linux packages, postponed (owner, 2026-10-22).** A user reported that the Linux package
    does not install because of libraries (not reproduced; no distribution or message known).
    The AppImage is built on Ubuntu 24.04, so it needs glibc 2.39 and FUSE 2, and only the
    offscreen platform is tested. The proposal stands for later: build it on Ubuntu 22.04 with
    the official Qt and add an xcb smoke test, with the user's distribution and error text.

## Known bugs and gaps in the editor (not fixed)

- The mask, background and sprite lists of the left sidebar still build every icon at once and
  rebuild all of them on many updates. Their counts are small, so this was left as it is.
- Adding or deleting a record rebuilds the three browser lists and, for frames, the frame list
  (texts of all rows; thumbnails stay lazy). The frame list then scrolls to the frame selected
  after the deletion.
- libserum keeps the 256x64 dynamic data of sprite N only if frame N has 256x64 data
  (the flag array of the frames is indexed with the sprite number). In a 256x64 project the
  picture of a frame with sprites can therefore change when frames before it are deleted.
  Seen in the tests; a libserum matter.
- Error texts that come from `ProjectDocument` exceptions and reach the status bar were only
  partly reworded for single-plane projects ("256x64 frame is not available"); other core
  messages may still say "HD" there.
- **The editing canvas differs from libserum on the HD plane of an ordinary project at the edges
  of upscaled content.** libserum renders dynamic zones and sprites without HD art at 128x32 and
  upscales them with Scale2x; the editor doubles pixels. Measured on `trn_174h`: 4249 of 5211 HD
  frames differ, 113940 pixels at the edges of dynamic zones (27 per frame), 782 at sprite
  edges. Frames without HD have no 64-row picture in the editor at all. A frame with HD whose
  background has no HD image is, by libserum's code, rendered like one without HD; the editor
  shows the HD frame (no real project has that case). The Runtime view shows the real picture.
- libserum looks the dynamic pixels of a sprite up in the frame's rotations
  (`Colorize_Spritev2`), unlike the dynamic pixels of a frame, and the editor's preview follows
  it. Whether that is intended is not known.
- libserum leaves the rotation entry of a pixel alone where it keeps or takes the picture of a
  background scene (`suppressFrameBackgroundImage`, `applySceneBackground`). Not reachable from
  the editor, which loads no scenes; not looked into.
- In a 256x64 project a frame whose background or sprite lacks 256x64 data is drawn without it
  by the editing canvas (to stay editable); libserum shows nothing for the frame.
- Runtime view: no `.pup.csv` scenes and no `scaling.txt` (libserum's memory load reads neither,
  so the 64-row plane always uses the default Scale2x and shadow offset); the trigger libserum
  reports is not shown. A still is rendered from a cROM of that frame alone; the status line
  says when a machine coming from the frame before would identify its original as another
  frame. In playback, what libserum keeps from frames before a window besides the
  identification (a monochrome switch, the phase of running rotations) starts again with the
  window.
- Runtime render cost per frame (subset, serialize, load, colorize; release build, M-series
  Mac): `hook_501` 0.24 ms, `afm_113b` 0.60 ms, `trn_174h` 1.8 ms, `smb3` 2.1 ms, `frankst1`
  2.0 ms mean; the load is 70 to 90 percent of it. Frames with many sprites cost about 10 ms
  (`smb3` frames 2200 to 2700, 3.8 MB cROM each). The view renders at paint time and only when
  the document revision, the frame or the plane changed, so repaints are free and bursts of
  changes coalesce; no timer and no thread.
- Not undoable: palette names. Sprite slots, capture, detection areas, sprite names and colour
  rotations are undoable through `edit/StateCommands` (state before/after; capture is one
  `CommandGroup` in the frame's history). The source rectangle and mirror flags of a sprite
  (`SpriteRect`, `SpriteRectMirror`) are not written by capture: CDMD writes them relative to its
  clipboard and reads them only in commented-out code; `Sprite_Col_From_Frame` is written.
  As in CDMD, a capture keeps the sprite's dynamic mask.
- `ProjectDocument::createEmpty` is still not a complete new-project path (tests only); new
  projects come from a dump (`importing::makeProjectSnapshot`).
- A new project of a large dump is as heavy as its originals and colorized pictures: its other
  blocks are uniform and shared (see "Memory, load and save"). 21671 frames of `smb3.txt` were
  1.3 GB in memory before that; not measured again (about 12 KB per frame expected, 260 MB).
- The same-frame combo box and `Frames using` list are refilled on every refresh (up to 10000
  entries); `Update Same List` runs on the UI thread with a wait cursor and cannot be stopped.
- `ProjectFrameMatcher` hashes a dump frame once per (mask, shape mode) pair of the project
  with the plain `legacyFrameHash`; fast enough for `smb3` (1 s), not tuned.
- Dump frames cannot be imported from other formats (the Win32 editor has none either).
- The Windows and Linux builds come from CI and are not tried by hand on real machines (see
  "Next steps"). The ui_dump private-access trick is not expected to link with MSVC.
- The edit color keys are tested with synthetic events only; the physical-key tables for macOS,
  Windows and X11/Wayland follow the platforms' documented codes and were not tried on a real
  keyboard.

## Feature parity still missing against 3.17.3

- The palette popup on an edit color click, the palette's Ctrl/Alt right-click (pipette into a
  palette color, edit color into a palette color), the composed-color pipette and the pipette
  writing into the selected edit color (asked for by one author, see the answers above).
- Black and white of the whole project (CDMD `IDC_BAW`, not undoable there; its rotation loop
  also greys the length and delay values of slots 1 to 3).
- Images window: the AI image generation.
- PuP tester: PUP's sound, and triggers from the Runtime view / frame tester identification
  (it sends the shown frame's trigger). See "VPX plugins".

## SortingCDump (2026-10-15)

Owner: authors use Zedrummer's SortingCDump (`../SortingCDump`, Win32) with CDMD 3.17.3;
the goal is to make it unnecessary. It opens and saves the cROM/cRP itself (no hand-off
format). What it does, and the state here:

- Move selected frames before a frame: done (`edit::moveFrames`, `FrameMoveCommand`,
  `ProjectDocument::reorderFrames`; drag and drop, Alt+Up/Down, Move Frames To...).
- Delete frames, with sections and sprite sources re-indexed: done. Not done: its check that
  lists frames a section starts at or a sprite was taken from, and offers deleting only the
  others.
- Sections: add/rename at a frame, delete, reorder, jump: done. Not done: alphabetical sort.
- Import TXT dumps with filters (minimum duration, maximum colors, exact duplicates, duplicates
  of project frames under their masks/shape mode): the dump import has its own filters; compare
  them with SortingCDump's before saying it is covered.
- `.cDump` import (binary: nFrames, romname[64], w, h, noColors, then per frame timestamp,
  original, colorized, 64-color palette): not done; where the files come from is unknown.
- Its strip of original and colorized frames with number, duration and section labels.

## Editor plugins (2026-10-15)

Editor API for plugins on the VPX plugin bus: `plugin-api/ColorizingDMDPlugin.h` (C, versioned
table, message `ColorizingDMD.GetEditorAPI:1`, events OnProjectChanged/OnSelectionChanged).
Host side `app/vpx/EditorApi.{h,cpp}` (installed when the bus opens, released before
Shutdown), `EditorBackend` implemented by `app/EditorPlugins.{h,cpp}` (loads the plugins chosen
under Settings -> Plugins, pumps the bus every 20 ms while one is loaded, polls revision and
selection for the events, Plugins menu, settings under `plugins/<id>/`). v1 reads only.
v2 (2026-10-19, for the retheming template of the AI assistant): edit batches, `BeginEdit` ..
`EndEdit`, with ResetFramePixels (a fill color, or the plain original colors, except a keep
mask; HD follows the ROM pixel), SetDynamicZones, SetFrameBackground, DeleteBackground, ClearColorRotations, and
GetPlainColors. Core: `edit/EditBatch.{h,cpp}`, `EditBatchCommand` in the structure history
(like SceneEditCommand; drops the other histories of the data its steps edit), refreshed by
`MainWindow::refreshAfterEditBatch()`. A batch still open after the plugins' messages ran is
recorded by `EditorPlugins::tick()`. Tests: `tests/EditBatchTests.cpp` and the API part of
`EditorPluginsWidgetTests.cpp`. Tests: `tests/EditorPluginsWidgetTests.cpp` call the
table as a plugin does. The AI assistant plugin that uses it lives in its own repository
(`../ColorizingDMD-AI-Assist`, local, not on GitHub yet) for a closed tester group; never ship
it in the editor's packages.

## Translations (2026-10-15)

Owner: German and French (the owner reads both), drafted here; Qt Linguist workflow.

- UI: texts in `tr()` (`Q_DECLARE_TR_FUNCTIONS(Class)`, no moc in the project) or
  `QCoreApplication::translate("Class", ...)` outside classes. `qt_add_translations` on the app
  target (`app/CMakeLists.txt`), `.ts` in `translations/`, `.qm` and Qt's
  `qtbase_de/fr.qm` built in under `:/i18n`. Target `update_translations` runs lupdate
  (`-locations none`, so line moves don't churn the .ts). LinguistTools is optional for dev
  builds, required for packages; CI installs it on all platforms.
- `app/Translations.{h,cpp}`: language setting `appearance/language` (empty = system),
  `installTranslations()` in main.cpp before the first window, `currentLanguage()` also picks
  the handbook language. Settings -> Appearance -> Language, effective at the next start.
- Tests load no translator: tr() returns the English source, which tests and ui_dump compare.
- `translations/README.md`: workflow and the glossary (de/fr terms) — keep it in sync.
- Drafts for de and fr, accepted by the owner as they are (2026-10-15): all marked finished,
  corrected later where needed; new texts are drafted and marked finished likewise. Merged from per-language JSON by a scratch script.
- Keys: the handbook writes `Ctrl/Cmd` for Qt's Ctrl (Cmd on macOS), plain `Ctrl` only for
  Win32 CDMD. UI texts that name keys use `KeyNames.h` (native shortcut text, Cmd/Option on
  macOS, translatable "Ctrl" -> "Strg").
- Not done: core (Qt-free) error texts reach the UI in English; handbook translations
  (`handbook/de`, `handbook/fr`) and a check for chapters older than their English source.

## VPX plugins (2026-10-14)

The editor hosts VPX plugins on libvpxhost's bus (github.com/PPUC/libvpxhost, shared with ppuc;
checkout in `PPUC_stack/libvpxhost`), so DMD output, PuP and video use the same plugins as VPX
and ppuc. Owner decisions: the plugin route, a library shared with ppuc in PPUC_stack, SDK
instability is no obstacle (the owner works with the plugin developers).

- `app/vpx/VpxHost.{h,cpp}` (target `colorizing_qt_v2_vpx`, the only C++20 target and the only
  one that sees the plugin SDK): `vpx::Host`, one bus per process (created on first use, kept
  until exit), plugin folder lookup (`COLORIZINGDMD_VPX_PLUGIN_DIR`, `plugins` beside the exe,
  `../PlugIns/vpx` in the bundle, `<build>/vpx-plugins`); `vpx::DisplaySource`, a picture
  offered as an SRGB565 `DisplaySrcId` (three buffers, reserved for 256x64, so a pointer a
  plugin thread got stays valid).
- `app/DmdOutput` loads the `DMDUtil` plugin with the settings of the DMD Output dialog as
  plugin settings and offers the picture as a display. No direct libdmdutil link any more;
  "display found / 64 rows" are gone (the plugin does not say), the status bar shows the
  plugin's or libdmdutil's last log line instead. `DmdOutputSettings::picture` (owner's
  request 2026-10-15): the canvas plane, native or extra, for ordinary projects; the Runtime
  view then renders both planes in one call (`RuntimeView::setOtherRows`, `PlaneRequest::Both`),
  the frame tester sends the plane asked for.
- Build: `-DCOLORIZINGDMD_LIBVPXHOST_DIR` and `-DCOLORIZINGDMD_VPINBALL_DIR` (PPUC's vpinball
  fork; only `plugins/plugins`, `plugins/dmdutil` and a few `third-party/include` headers are
  used). With `COLORIZINGDMD_DMDUTIL_PREFIX` too, `app/vpx/CMakeLists.txt` builds
  `plugins/dmdutil/DMDUtilPlugin.cpp` against the staged libdmdutil into
  `<build>/vpx-plugins/dmdutil` with its `plugin.cfg` and libdmdutil's libraries (rpath
  `@loader_path` / `$ORIGIN`; Windows: the loader adds the folder to the DLL path). CI pins
  `LIBVPXHOST_SHA` and `VPINBALL_SHA` to ppuc's pins.
- The plugin needs macOS 13.3 (std::format), so the macOS deployment target is 13.3.
- Packages ship `vpx-plugins` (`plugins/` beside the exe, `Contents/PlugIns/vpx`,
  `usr/bin/plugins` in the AppImage), and each packaging script runs the packaged app with the
  hidden option `--check-plugins`, which loads every plugin and fails if one does not load.
- Tests: `DmdOutputWidgetTests` loads the real plugin from the build and waits until the
  plugin's thread fetches the picture (128x32, then 256x64).
- Verified locally on macOS (build, tests, `--check-plugins` in a bundle layout). Windows and
  Linux only in CI.
- Owner decisions (restated 2026-10-15): the PuP tester uses VPX's PUP plugin (`plugins/pup`,
  the new Pin Up Player) on the editor's bus, shipped with the editor as a built-in plugin like
  DMDUtil; and the editor's own video clip import (Images window) uses the ffmpeg libraries the
  PUP plugin brings (avformat, avcodec, avutil, swscale, swresample), not a second ffmpeg.
- PUP plugin dependencies: SDL3, SDL3_image, SDL3_ttf, ffmpeg, libpupdmd. ppuc builds them
  with vpinball's own dependency scripts and packages `plugins/pup` with ffmpeg and SDL3_ttf
  beside the plugin, SDL3/SDL3_image/libpupdmd beside the executable
  (`ppuc/platforms/config.sh`, `ppuc_build_vpinball_media_plugins`; versions from vpinball's
  pins, `ppuc_vpinball_pin`). vpinball's `make/CMakeLists_plugin_PUP.txt` builds it for
  macOS, Linux and `windows-mingw` (not for plain `windows` without MSVC); ppuc does not build
  it on Windows. Hosting it also needs a renderer for PUP's screens and a game/controller on
  the bus (see ppuc's `src/MediaPluginHost.cpp`).
- Built here (2026-10-15): `packaging/build-media-libs.sh <vpinball> <platform> <arch>
  <prefix>` builds SDL3, SDL3_image, SDL3_ttf and ffmpeg at vpinball's pins into a prefix
  (ffmpeg: no avdevice/avfilter, `--disable-autodetect --enable-zlib` so nothing from the build
  machine leaks in; Windows `--build-suffix=64` as PUP's LibAv.h loads it; work tree
  `<prefix>-work`, skipped when `media-libs.txt` matches the pins). `build-libdmdutil.sh`
  also stages `pupdmd.h` (and on Windows the import libraries). With
  `-DCOLORIZINGDMD_MEDIA_PREFIX`, `app/vpx/CMakeLists.txt` compiles `plugins/pup/*.cpp` from the
  vpinball checkout (not vpinball's own CMake, which skips non-MSVC Windows) into
  `<build>/vpx-plugins/pup` with plugin.cfg, the media libraries and libpupdmd (copied keeping
  symbolic links, `CopyLibraries.cmake`). Listed as built in under Settings -> Plugins ("PuP
  tester"). Verified on macOS: `--check-plugins` loads it. CI builds and caches the prefix
  (key: vpinball SHA + script hash).
- Movie clip import (2026-10-15): `File -> Import Movie Clip Into Selected Frames...`,
  `dialogs/MovieImportDialog` (start position, crop zone drawn on the picture, kept to the
  plane's proportions by default, the clip following the frames' durations as CDMD's or a fixed
  5-3000 ms interval), `MovieImport.{h,cpp}` (times, crop), `MainWindow::importMovieIntoSelectedFrames`
  sharing `commitImportedFramePixels` with the image import (one undo step per frame). Not yet:
  into a background, CDMD's "copy into the selection" (we fill the whole plane), crop zone
  presets (the cRP's image positions). Tests: `tests/VideoClipWidgetTests.cpp` (a GIF from
  GifWriter read back; the dialog on it).
- PuP tester (2026-10-15): `app/vpx/PupHost.{h,cpp}` hosts the PUP plugin as a machine would:
  a controller `pinmame::<game>` on the bus (CtrlItemProvider), triggers sent as DMD-event
  source messages `D<id>` (the "Serum" namespace DMD-event API PUP listens to), VPXPluginAPI
  stubs (textures, table info, game time) and the ancillary renderer message: PUP's Topper,
  Backglass and ScoreView (DMD) windows draw through a VPXRenderContext2D into a QImage.
  `start(pupFolder, game)`, `trigger`, `process` (on the GUI thread), `render(Screen, QImage&)`.
  `app/PupTesterWindow.{h,cpp}` (View -> PuP Tester...): PUP folder (setting `pup/folder`), game
  (project's PuP pack name, else ROM name), Start/Stop, the three screens at 30 fps, "Follow the
  editor" (MainWindow::selectFrame passes each shown frame's trigger), manual trigger, list of
  sent triggers. Stopped on project change. Handbook chapter 37. Tests:
  `tests/PupHostWidgetTests.cpp` (a one-trigger pack, D5 turns the backglass red; the window on
  it). Not yet: audio (no audio output hosted), triggers from the Runtime view.
- PuP pack name in UTF-8 (2026-10-15, owner: "We break backward compatibility anyway"): the
  Win32 field is `wchar_t[256]`, 2 bytes on Windows, 4 on macOS/Linux (the first Qt attempt
  wrote 4-byte characters). `EditorMetadata::pupPackName` is now a UTF-8 `std::string`; the
  .cRP reader converts the UTF-16LE field (`pupPackNameFromLegacyBytes`), a plain .cRP gets it
  back as UTF-16 (`legacyPupPackNameBytes`, cut to 255 units), so Win32 projects still
  round-trip identical (checked hook_501, smb3; none of the fixtures names a pack). The .cRPX
  writes its cRP part with an empty field (`writeCrpxCrpPart`) and the pack as chunk `PUPP`
  (uint32 has pack, uint32 length, UTF-8); a .cRPX of earlier builds with the name in the field
  still loads. No UI to edit the name yet (the PuP Tester's game field is not saved).
- `app/VideoClip.{h,cpp}`: the movie clip import's reader; loads avutil, swscale, avcodec,
  avformat at run time with QLibrary from `<plugin folder>/pup` (names: `libavX.N.dylib`,
  `libavX.so.N`, `avX64-N.dll`), so the editor starts without them.

## Windows, Linux and CI

`.github/workflows/qt-editor.yml` builds, tests and packages the editor on every push and pull
request (any branch; before the move to the repository root, path filters kept changes of the
Win32 sources out) and on `workflow_dispatch`.

| Job | Runner | Toolchain and Qt | Package |
|---|---|---|---|
| Windows x86_64 | `windows-2025` | MSYS2 UCRT64: GCC 16.2, Qt 6.11.2 (`qt6-base`) | zip: exe, `windeployqt6`, every DLL from `/ucrt64/bin` it needs (import tables read with `objdump -p`; `ntldd -R` hung on Qt DLLs) |
| Linux x86_64 | `ubuntu-24.04` | GCC 13.3, Ubuntu's Qt 6.4.2 | AppImage (linuxdeploy + plugin-qt), needs glibc 2.39 |
| Linux aarch64 | `ubuntu-24.04-arm` | as above | AppImage |
| macOS arm64 (Apple silicon only; Intel dropped 2026-10-14) | `macos-15` (arm64) | Apple clang, official Qt 6.11.3 (install-qt-action, Qt Base only), macOS 13.3 or later | zip of an ad-hoc signed `ColorizingDMD.app` (macdeployqt) |

- Each job checks out libserum (`PPUC/libserum`, pinned `LIBSERUM_SHA`, now `c8d8166`) beside
  the editor, so the default `COLORIZINGDMD_LIBSERUM_DIR` finds it, and runs libserum's own
  `platforms/<platform>/<arch>/external.sh` for `FrameUtil.h` (libframeutil at the
  `LIBFRAMEUTIL_SHA` libserum pins, now `711ab21`). libserum-accel is not used.
- Release build with the tests, `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`, ccache (cached per job),
  `ctest` with `QT_QPA_PLATFORM=offscreen`.
- Each packaging script checks its result and starts the packaged app once (offscreen; the
  offscreen platform plugin is added to a throwaway copy on Windows and macOS, and shipped in
  the AppImage). The Windows script also checks with `objdump -p` that, with MSYS2 off PATH, the
  exe needs nothing outside the folder and Windows.
- Packages are uploaded per job as `ColorizingDMD-<platform>-<arch>-<short commit>.<zip|AppImage>`.
  A tag `v<version>` (it must match `project(... VERSION ...)` in `CMakeLists.txt`,
  now 0.1.0) also creates a draft pre-release with all packages; publishing is by hand.
- Actions are pinned to commit hashes (with the version in a comment), linuxdeploy
  (`1-alpha-20251107-1`) and linuxdeploy-plugin-qt (`1-alpha-20250213-1`) to releases with
  SHA-256 checks.
- Why Ubuntu's Qt on Linux: no download, the same GCC/Qt pair that was verified locally, and it
  keeps the editor building from distribution packages on the current LTS (6.4 is the oldest
  Qt it is built with). The price is an old Qt in the AppImage and the glibc 2.39 floor;
  switching the Linux jobs to install-qt-action on `ubuntu-22.04` would give a newer Qt and an
  older glibc floor.

Verified locally (2026-10-14):

- Ubuntu 24.04 aarch64 container (Apple `container`): GCC 13.3.0, Qt 6.4.2, CMake 3.28.3,
  libserum cloned at the pin with `external.sh`. Warning-free with `-Wall -Wextra` and
  `-Werror`; both test executables pass; `package-linux.sh` builds an AppImage that starts.
- Fedora 44 aarch64 container: GCC 16.2.1 and Qt 6.11.2, the versions MSYS2 has now. One GCC 16
  warning (`-Wdangling-reference`, a false positive in `PreviewDiffTool.cpp`) fixed; then
  warning-free, both test executables pass.
- macOS: the usual Homebrew build (Qt 6.11.2) warning-free, tests pass; the CI configuration
  (official Qt 6.11.3, universal, macOS 13) warning-free with `-Werror`, tests pass for arm64
  and under Rosetta for x86_64, `package-macos.sh` makes a signed bundle that starts.

Not verified: anything on Windows (no MinGW toolchain here): the MinGW build, the tests there,
`package-windows.sh` (`windeployqt6`, `objdump`, the DLL collection) and the non-ASCII path
tests on a Windows file system. The workflow itself (only linted with actionlint and
shellcheck). Linux x86_64 (only aarch64 was built locally).

Portability fixes made for this:

- Paths: `std::ifstream`/`std::ofstream`/`std::filesystem` were given the UTF-8 `std::string`
  from `QString::toStdString()`. On Windows that is read in the ANSI code page, so projects or
  dumps in folders with other characters would not open or save. Now converted with
  `platform::pathFromUtf8()` in `LegacyProjectFileReader` (cROM, cRP), `FrameDumpImport`
  (dump file) and `LegacyProjectFileWriter` (temporary files, rename, removal; the temporary
  paths go to libserum as UTF-8 instead of `path::string()`). libserum's `Serum_CRZ_*` already
  take UTF-8 (miniz converts with `MultiByteToWideChar` under MSVC and MinGW-w64).
- The reader/writer tests and the dump dialog test now use temporary names with a character
  outside code page 1252 (U+03A9), so a Windows run catches a regression; the reader tests no
  longer use `<unistd.h>`/`getpid()`.
- `.gitattributes` keeps `*.sh` at LF on Windows checkouts (the packaging script
  runs in MSYS2 bash).

Reviewed for Windows/MinGW and left unchanged:

- `std::filesystem::rename` replaces an existing target on Windows too (MSVC and libstdc++ both
  use `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`). Still, a target held open by another program
  without delete sharing makes it fail; then the save can be half done (cROM replaced, cRP not),
  as on any platform when the second rename fails.
- `colorizing_qt_v2_roundtrip` and `colorizing_qt_v2_preview_diff` take paths from `argv`,
  which is ANSI on Windows: manual tools, ASCII paths only there.
- `MainWindow.cpp` has about 250 sections in an ELF object, far from the COFF limit; no
  `-Wa,-mbig-obj`.
- No `ssize_t`, `printf` formats, `long` sizes, POSIX headers or non-trivial globals across
  translation units; include spelling is checked by the case-sensitive Linux build; test dumps
  are built in code (CRLF included), not read from checked-in files.
- libserum: the editor already passes `PLATFORM=win-mingw` (libserum's own MinGW setting,
  built by its CI); it needs no change to build with MinGW. Observed for later: on Windows its
  first API call installs a process-wide `SetUnhandledExceptionFilter` that does not chain, so
  in the editor any crash, not only one in libserum, is reported as a libserum fatal error
  (through `OutputDebugString`) before the normal crash handling.

## Next steps

1. Try the packages on real machines: the Windows zip on a PC without MSYS2, the AppImage on
   other distributions (see open decision 12), the macOS zip on a Mac that never ran it
   (ad-hoc signed only). Check the edit color keys with real keyboards (Windows scan codes, XKB
   key codes, AZERTY) and the Ctrl shortcuts and Ctrl+click selection on Windows and Linux.
   Later: Developer ID signing and notarisation for macOS, Wayland plugins in the AppImage.
2. Fix the editor bugs listed above and keep shrinking `MainWindow`.
3. Import Image on an HD frame could offer `Only on the SD Picture` for the dynamic sets its
   picture covers.
4. Ghost frames of WPC dumps (the import leaves them out): propagate a key frame's colors
   through an animation, shade-adjusted and with motion, then offer it to the AI assistant.
5. After the authors confirm feature parity with 3.17.3 (owner decision 2026-10-19): the
   persistent runtime instance below. Later, by decision of the owner: a new project format.
6. Remove the deprecated legacy writers (`tests/legacy-writers`, owner 2026-10-26): the tests
   that make legacy projects with them work from fixed bytes and the real projects instead,
   and `colorizing_qt_v2_roundtrip` checks the readers without the cRP writer.
7. Teams: remove packages (and notices) from the repository once every partner merged them,
   after a grace period (owner, 2026-10-23: later). Nothing removes them yet, so packages/
   only grows.

## Persistent runtime instance (deferred)

Asked 2026-10-19: work on a cROMc directly instead of the cROM, so libserum's loaded
colorization serves the frame tester at once and needs less memory. Discussed with the owner;
the cROMc does not become the project format:
- The cROMc is not a lossless store of the project: libserum normalizes on load (shape-mode
  sprite detection words and originals, sentinel values moved into sidecar flags), keeps planes
  by load flags, and never had editor-only data (inactive HD pixels kept for byte-identical
  Win32 round trips). Every runtime change would have to stay reversible.
- It is a read format: LZ4-packed, deduplicated payloads; a pixel edit unpacks into mutable
  storage, a save rewrites the whole archive. Undo, autosave from a snapshot on a thread and
  subset loads for the Runtime view still need the editor model; libserum holds one colorization
  per process in globals.
- Concentrate versions follow runtime needs (unreleased versions need not stay compatible), a
  project file must.
- It holds hashes, not originals: the .cRPX stays anyway.

The plan instead (implement only after the authors' confirmation): the document stays the
source of truth; one libserum instance stays loaded for the session (the frame tester, the
Runtime view) and is patched from the edits:
- frame content (colours, dynamic zones and colours, background masks, sprite slots, rotations,
  triggers): that frame's vectors and their sidecars (frameHasDynamic, colour rotation lookup,
  sprite candidate tables);
- identification (original, comparison mask, shape mode): also the frame's hash and its normal
  bucket / signature entries;
- scenes: rebuild the scene lookups (3.6 s on GoT-V79);
- adding, deleting, moving frames (IDs shift everywhere): a full reload on a thread before the
  tester is needed; incremental renumbering only if that proves too slow;
- what changed comes from the undo commands' targets and the document revision; whatever cannot
  be patched falls back to a reload;
- test: after any series of patches, libserum's state equals a fresh full load of the same
  project, on every fixture.
The patch API lives in libserum (editor-facing, separate from runtime performance work).

## Test material (read-only, never commit)

- `/Volumes/data/CDMD_3.17.3_projects/`: `afm_113b`, `trn_174h` (cRZ + cRP), `hook_501`
  (cROM + cRP, 128x16 padded), `smb3` (cROM + cRP), `frankst1` (cRZ + cRP, native 192x64,
  11199 frames; the cROM in the archive is 711 MB).
- Ghostbusters: `/Volumes/data/Downloads/spagb_100.cROMc` and frame dumps (`spagb_100.txt` in
  several places, plus `Serum Frame Dump-20261012T204757Z-1-001.zip` with 20 dumps).
- Older local projects under `/Volumes/data/Serum/` were rewritten by earlier Qt builds and are
  not trustworthy.

## Building

The editor needs CMake 3.25 or later (libserum's minimum), Ninja, a C++20 compiler, Qt 6.4 or
later (Qt Widgets only) and a libserum checkout with `FrameUtil.h`. For VPX plugins and DMD
output also libvpxhost, a vpinball checkout and libdmdutil (see "VPX plugins"). libserum is expected beside
the ColorizingDMD checkout (`../libserum`), or anywhere with `-DCOLORIZINGDMD_LIBSERUM_DIR=`:

```sh
git clone https://github.com/PPUC/libserum.git
git -C libserum checkout c8d8166323f437044d8bb3aaa6d0eb91885ada94   # LIBSERUM_SHA of the workflow
(cd libserum && ./platforms/<platform>/<arch>/external.sh)           # fetches FrameUtil.h
```

macOS (Homebrew: `brew install qt cmake ninja`):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Linux (Ubuntu 24.04: `sudo apt install qt6-base-dev qt6-base-dev-tools libgl-dev cmake ninja-build g++`):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Windows: MSYS2, UCRT64 shell
(`pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools,qt6-translations} zip`), then the same three
commands as on Linux. The UCRT64 shell has Qt's DLLs on PATH, so the tests and the app run
from the build directory.

Packages: configure with `-DCOLORIZINGDMD_QT_V2_PACKAGE=ON` (macOS release packages also with
`-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3` and the official
Qt), build, then `packaging/package-<windows|linux|macos>.sh <build dir> <output>`;
the comment at the top of each script says what it needs. `.github/workflows/qt-editor.yml`
does exactly this.

## How to build and check

```sh
cmake -S . -B <build dir> -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build <build dir>
(cd <build dir> && QT_QPA_PLATFORM=offscreen ctest --output-on-failure)
<build dir>/tests/colorizing_qt_v2_roundtrip <project>.cRZ <project>.cRP   # must print "identical" twice
<build dir>/tests/colorizing_qt_v2_preview_diff <project>.cRZ <project>.cRP   # native plane identical
<build dir>/tests/colorizing_qt_v2_preview_diff <project>.cRZ <project>.cRP --playback 48   # playback like the whole project
tools/ui_dump/run.sh baseline   # before a change
tools/ui_dump/run.sh after      # after it (--allow-appended for new states)
```

The build is warning-free with `-Wall -Wextra` and must stay so.
