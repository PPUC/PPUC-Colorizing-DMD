# Testing Checklist

What to check by hand after changes; for developers, not part of the handbook in the app.

For drawing changes, test at least:

- SD preview and commit.
- HD preview and commit.
- Undo and redo in the same frame/resolution.
- Switching SD/HD does not interleave undo histories.
- Save, reopen, and verify edits persist.
- With `Mask` active, add and Shift-erase pixels on the original/reference region, undo/redo the mask edits, save, reopen, and verify the mask overlay persists.

For changes to the selection, copy and paste and the helpers of the `Edit` menu, test at least:

- `Ctrl`+drag adds and `Ctrl+Shift`+drag removes, on SD and HD; `Grad Sel` fills the result.
- Copy on one frame, paste on another (moved with the mouse and the arrows, mirrored with `K`
  and `L`), on two selected frames at once, then undo on each frame; the dynamic zone and its set
  colors come along on the dynamic plane.
- `Shift`+click pastes only where the originals match; `Alt`+click pastes the dynamic mask
  alone; SD content is refused on HD and the other way round.
- Auto copy: the count in the dialog, the frames changed, undo on one of them; save, reopen,
  and check with `colorizing_qt_v2_preview_diff` that the native plane is still identical.
- The scrolling fill on a real scrolling (the run of selected frames around the current one).
- `Ctrl+C`, `Ctrl+V` and `Ctrl+A` in a text field and in the frame list still work there.

For changes that read the original frame (dynamic colors, shadows, backgrounds, sprites,
comparison masks, anything on the original/reference region), test a project with 128x32, one
with 256x64 and one with 192x64 original frames. The automated tests and `tools/ui_dump` contain a
constructed 256x64 project each, including frames, a background and sprites without HD data, and
a constructed 192x64 project each, whose snapshot has 96x32 data in use.

For changes to SD/HD handling, check in a project with 256x64 original frames that nothing shows,
edits or falls back to its SD data, and in a project with 192x64 original frames that no 96x32
data is shown or kept and that a save writes its extra plane as "no extra data", the same on a
second save. In both, check that the window says "HD" or "SD" nowhere: no toolbar buttons, no
badge, no filter entry, no counter, no `Create HD` / `Remove HD` (see the table in "SD And HD
Modes"), and that an ordinary project looks exactly as before.

For changes to the frame list, check with a large project that opening does not build one
thumbnail per frame, that rows show their picture when scrolled into view (also after jumping
with the scroll bar, `End`, a filter change and during playback), that a thumbnail in view
follows an edit, undo and redo at once, and that a row that was edited while out of view shows
the new picture when it is scrolled back to.

For changes to the editor's renderer (`app/FrameImageUtils`), to the runtime preview or to
libserum, build `colorizing_qt_v2_preview_diff` and run it against real projects. It renders
every frame with the editor's renderer and with libserum and compares the pictures; it writes
nothing:

```text
colorizing_qt_v2_preview_diff <project>.cROM|.cRZ <project>.cRP [first[-last]] [--step N]
                              [--examples N] [--pixels N] [--playback N [--no-whole]]
```

It compares the plane that has the original frame's size and, in an ordinary project, the HD
plane of frames that have HD. The upscaled 64-row plane of frames without HD is not compared.
Per plane it prints how many frames are identical and, for the others, the differing pixels by
class (inside a found sprite, inside a sprite slot's bounds, shadow, the edge rounding of
libserum's upscale in or beside a dynamic zone, anything else in a dynamic zone, background,
static) with example frames. `--pixels N` prints the first N differing pixels of each such frame,
`--step N` takes every Nth frame. The last line gives the mean cost of a runtime render. The
exit status is 0 only if every compared plane is identical.

Expected: `native plane ...: N frames compared, N identical` for every project. An ordinary
project with HD frames that have dynamic content or sprites reports differences on its HD plane,
see "Preview Rendering"; for `trn_174h` that is 4249 of 5211 frames and 114722 pixels, classes
`dynamic, upscale rounding` (113940), `sprite` (527) and `sprite area` (255), and none in the
class `dynamic`. A full run takes 2 to 25 seconds per project; `frankst1` needs about 3.2 GB
to load, so sample it with `--step`.

Color rotations are not part of that tool. The automated tests cover them
(`EditorMatchesRuntimeTests`): for a constructed project of each layout and for the HD plane of
an ordinary one, every frame gets a rotation made of colors it shows, dynamic and shadow colors
among them, libserum's rotation is stepped three times and the editor's rotation preview has to
show the same pixels after each step; one more case is white digits from a dynamic zone, with
their shadow, over rotating stars, on the 32-row plane, the 64-row plane and both. On the real
projects the preview and libserum were compared once by hand in the same way, three steps on
every frame that has a rotation: `afm_113b` 61 frames, `trn_174h` 2, no pixel different
(`hook_501` and `smb3` have no rotation).

`--playback N` checks runtime playback instead of the pictures of single frames. It plays the
frames of the range in order through the runtime preview, in windows of at most N played frames
as the Runtime view does (the editor uses 48), and through libserum with the whole project
loaded as one cROM, and compares every step: the frame identified, whether libserum reported the
picture as unchanged or skipped the frame, and both planes. It also compares what libserum says
it identified with the editor's statement of libserum's search rule, and prints how many windows
were loaded and what they cost. The whole project needs its cROM in memory, about 1.7 to 3.7 GB
for the projects named here; `--no-whole` leaves that comparison out and reports the windows.
Expected for every project: `0 steps identified differently, 0 reported differently` and
`search rule against libserum: 0 differences`. Pictures may differ at single pixels of the
64-row plane, see "Runtime View". Last measured over all frames: `afm_113b` 3047 frames, 8
identified as another frame than the one fed, no difference; `hook_501` 3897, 26, none; `smb3`
4430, 3, pictures in 19 steps (19 pixels of the 64-row plane); `trn_174h` 5211, 48, pictures in
13 steps (26 pixels); `frankst1` 11199 frames with `--no-whole`, 4, search rule without
difference.

For changes to the Runtime view, also check by hand: `Ctrl+R`, the `Runtime` toolbar button and
`View -> Runtime View` on and off in an ordinary project in SD and in HD mode, both runtime planes
on a frame with and without HD, an Inspector edit and undo while the view is on, `Play` with the
view on over a frame with a color rotation and over frames that share an original (the status
line names the frame libserum identified), `File -> Export Current Frame PNG...` with the view on
and off and on an inactive frame, and the same in a project with 256x64 and one with 192x64
original frames.

For changes to colors, edit colors and color sets (`core/edit/ColorModel`,
`core/edit/ColorSetEdits`, `app/panels/EditColorBar`, `app/panels/ColorSetPanel`,
`app/dialogs/ColorPickerDialog`), test:

- In a 4-color and a 16-color project: the number of edit color swatches and their shade strips;
  click, double-click (picker), drop of a palette color, the keys of the slots, and Undo/Redo of
  every edit color change.
- `Set Replace` with `Point`, a shape, `Flood` and on several selected frames, in SD and in HD
  mode, and in a project with 256x64 and one with 192x64 original frames: every pixel takes the
  edit color of its own frame's shade; Undo per frame.
- `Pick` with and without `Shift`.
- `Store`, `Load`, renaming, `Deactivate` and their Undo on the `Color Sets` section; save and reopen.
- The picker: change each field and read the others; the recent colors.
- `Edit > Dyn` on several frames and `Dyn > Edit`, with Undo.
- Open and save a project of the Win32 editor without touching its colors and run
  `colorizing_qt_v2_roundtrip`: the cRP must be `identical`; also after color edits that were all
  undone.

The automated tests cover the conversions for all 65536 RGB565 colors (and RGB888 -> HSV ->
RGB888 for all 16.7 million RGB888 colors), the set operations with undo, the replace color
source on the three layouts and both planes, multi-frame and flood fill in replace mode, the
picker's field sync, the pipette with `Shift`, the keys and the panels; `tools/ui_dump`
has the window's states (`colors_script.inc`).

For changes to dynamic data, check in an ordinary project that `Dynamic` is disabled in HD mode
with its tool tip, that the `Dynamic Sets` section there shows the SD colors with its note, that a color
changed there shows in the HD picture, and that a project with stored HD dynamic data still
round-trips byte-identical.

For changes to adding and deleting records (`core/edit/StructureCommand`, the `remove...` and
`insert...` functions of `ProjectDocument`), test in a project of each layout:

- delete the first, the last, several separate and all but one of the frames; a frame that
  starts a section and all frames of a section; frames below and above the number of imported
  palettes; all frames (refused);
- `Delete Frames Shorter Than...` with a duration no, some and all selected frames are below;
- add a sprite, a background and a mask up to the limits (255, 255, 64); delete the first, a
  middle and the last one, one that many frames use (a sprite in several slots of one frame,
  and in slot 32) and one nothing uses;
- undo and redo each of them, also after editing the record in between and after assigning a
  new record to frames, and check that Undo is offered on every page and frame;
- after each: the frame list (numbers, flags, thumbnails, filter still applied), the three
  browser lists, the inspector pages and the Runtime view show the new numbering; an edit of a
  frame, sprite, background or mask behind the changed one has no stale Undo;
- save, reopen and compare; `colorizing_qt_v2_roundtrip` on the saved files must say
  `identical` twice and `colorizing_qt_v2_preview_diff` must report the native plane identical
  for all frames. Undoing everything and saving a project of the Win32 editor must give back
  its files byte for byte.

The automated tests (`tests/StructureCommandTests.cpp`) do this on constructed projects of the
three layouts, including a load of the complete saved cROM in libserum, and run 20000 random
edits, record operations, undos and redos while checking that no command is ever run against
another record than the one it was made for. `tools/ui_dump` has the window's states
(`structure_script.inc`).

For load/save changes, build the `colorizing_qt_v2_roundtrip` tool and run it against real
projects. It loads a project into a document and serializes it the way `Save` does, comparing
the bytes with the files on disk as they are produced, and reports per block where the result
differs; it writes nothing and holds only the project (plus, for a `.cRZ`, the `.cROM` libserum
unpacks):

```text
colorizing_qt_v2_roundtrip <project>.cROM <project>.cRP
colorizing_qt_v2_roundtrip <project>.cRZ <project>.cRP
```

With a `.cRZ` the comparison is against the `.cROM` stored in the archive.

Projects last saved by the Win32 editor must come back `identical`, cROM and cRP.

A project with 192x64 original frames is the exception for the cROM: its 96x32 data is discarded
on load, so blocks of the extra plane (`extra ... flags` and every block named `HD ...`) may
differ. The tool marks each of them with `(expected: 96x32 data is discarded)`, prints
`identical except the discarded 96x32 extra plane` and still exits with 0. Only the blocks of
the original plane and the whole cRP are expected to be identical; a difference in any other
block is printed without that remark, the result is `DIFFERENT` and the exit status 1. For
`frankst1` the expected output is:

```text
cROM: 710862076 bytes on disk, 710862076 bytes serialized
  HD dynamic colors: 688808 of 2866944 bytes differ, first at offset 519056805 (expected: 96x32 data is discarded)
  HD background masks: 34403328 of 34403328 bytes differ, first at offset 674308540 (expected: 96x32 data is discarded)
  identical except the discarded 96x32 extra plane
cRP: 139931019 bytes on disk, 139931019 bytes serialized
  identical
```

For changes to the dump import (`core/import`, `app/dialogs/DumpImportDialog`), test:

- A new project from a 128x32 dump: the summary before `Create Project`, the frame count, the
  title `(not saved yet)`, the first `Save` asking for the file and the second not asking.
- The filter: a duration limit, a shade limit, and that a changed option needs `Read Dump
  Again`.
- A dump that ends inside a frame (the last frame is left out and named) and a dump with a bad
  line (the message has the line; nothing is imported).
- `Add Frames From Dump...` with the mask option on and off, a dump of another size (refused),
  `Undo` and `Redo` of the addition.
- A large dump (100 MB): the window stays usable while it is read, `Stop` works.
- Save the new project, reopen it, run `colorizing_qt_v2_roundtrip` (identical) and
  `colorizing_qt_v2_preview_diff` (the native plane identical; frames shorter than 15 ms are
  inactive and libserum shows no picture for them).

For changes to the same-frame tools (`core/edit/SameFrames`, `app/panels/SameFramesPanel`),
test on the `Comparison Mask` section: the count follows the frame and a stroke on its mask; `Update Same
List`; each of the three deletions with and without `Keep selected frames`, and its `Undo`;
`Spot Common` with two and with one selected frame; `Common To Mask` and its `Undo`; `Frames
using` and `Select Them` with a frame filter active.

For project-loading changes, test:

- Open via `.cROM` with matching `.cRP`.
- Open via `.cROM` and manually selected `.cRP`.
- Open via `.cRZ` with no `.cROM` next to it, save, and verify `.cROM`, `.cRP` and `.cRZ` are
  there, no `.saving` file is left, and the `.cRZ` unpacks to the bytes of the `.cROM`.
- Open via `.cRZ` while a different `.cROM` of the same name lies next to it: the archive's
  content is what loads.
- Open via `File -> Open Recent`.
