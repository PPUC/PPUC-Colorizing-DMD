# ColorizingDMD Qt Rewrite v2 Handbook

This handbook describes the implemented behavior of `qt_rewrite_v2`.
Update it in the same change set whenever a user-visible feature changes.

## Project Files

The Qt rewrite edits legacy Serum editor projects directly:

- `.cROM` stores colorized frame data and runtime project data.
- `.cRP` stores original/reference frames and editor-side metadata such as sprite names and frame durations.
- `.cRZ` is a zip archive holding the `.cROM`, in one entry named `<ROM name>.cRom`. It is the
  form in which a colorization is usually handed around, together with the `.cRP`. The editor
  opens it directly and writes it on every save.
- `.cROMc` is not edited directly.
- A frame dump (`.txt`) is what a project starts from and what frames are added from: the
  frames a ROM showed, as text. It is only read, see "Creating A Project From A Dump".

Saving writes complete `.cROM` and `.cRP` files from the in-memory project in the legacy file
layout, plus the `.cRZ` archive of that `.cROM`. Opening is verified with cROM header revision 19 and 20 projects. Revision 20 projects are
saved as revision 20, everything older as revision 19, so a save never moves a project to a
revision an older libserum cannot read.

A project file stores two planes of colorized data, an "original" and an "extra" one. Which sizes
they have, and which of them has the size of the ROM's own frame, depends on the project. The
editor opens three layouts and refuses every other combination of sizes with a message that names
the sizes it found:

| ROM frame | Original plane | Extra plane | Shown and edited |
| --- | --- | --- | --- |
| 128x32 (ordinary project) | 128x32, the editor's SD | 256x64, the editor's HD | both |
| 256x64 (revision 20 flag "256x64 original") | 128x32, kept unchanged | 256x64 | the 256x64 plane only |
| 192x64 | 192x64 | 96x32, discarded | the 192x64 plane only |

A plane that is smaller than the ROM frame is never shown or edited. The 128x32 data of a 256x64
project is loaded and written back as it is. The 96x32 data of a 192x64 project is discarded when
the project is opened and written as "no extra data" on save. See "Projects With 256x64 Original
Frames" and "Projects With 192x64 Original Frames".

"SD" and "HD" are names the editor uses only in ordinary projects, where HD is an extra, upscaled
resolution next to SD. A project of one of the other two layouts has one plane, and the editor
shows no SD or HD wording, badge, counter, filter or button for it; where the plane needs a name
it is called by its size, `256x64` or `192x64`. Everywhere else this handbook describes ordinary
projects, where SD means 128x32 and HD means 256x64.

The in-memory project model currently carries structured comparison-mask data from `.cROM`:

- Per-frame shape comparison mode.
- Per-frame comparison mask ID.
- Comparison mask bitmaps, in the size of the original frames (128x32, 256x64 or 192x64, see the
  table above).

It also carries structured dynamic-mask data from `.cROM`:

- Per-frame SD and HD dynamic mask bitmaps.
- Per-frame SD and HD dynamic color sets.
- Per-frame SD and HD color rotation slots.

It also carries structured background data from `.cROM`:

- Shared SD and HD background images.
- Per-frame background assignments.
- Per-frame SD and HD background masks.

## Opening Projects

Use `File -> Open Project...` and select a `.cROM` or a `.cRZ` file.

If the open project has unsaved changes, `Open Project...`, an entry of `Open Recent`, `New
Project From Dump...`, `Quit` and closing the window ask first: `Save` writes the project (a new
project asks where to first) and goes on only if it was written, `Discard Changes` goes on
without saving, `Cancel` (or Escape) stays with the project as it is. A project without unsaved
changes is closed without a question.

The app looks for a matching `.cRP` next to it with the same base name. If it cannot find that file, it asks for the `.cRP` path.

Opening a `.cRZ` reads the colorization from the archive; nothing is unpacked to disk. The
archive's first file entry is taken, whatever it is named. If a `.cROM` with the same base name
lies next to the `.cRZ`, the file that was selected is the one that is loaded; the other is not
looked at.

A project opened from `<name>.cRZ` is from then on the project `<name>.cROM` in the same
folder: `Save` creates that `.cROM` next to the archive (replacing one that is already there) and
rewrites the archive, and `Save As...` proposes `<name>_copy.cROM`.

After a successful open, the project is added to `File -> Open Recent`.

Opening runs in the background. A project that opens within a moment (about 0.3 s) just
appears; a larger one brings up the `Opening Project` window with what is being done
(`Unpacking the .cRZ archive...`, then `Reading the project: N of M MB` with a bar) and a
`Cancel` button. While it is up the main window takes no input. `Cancel` (or Escape, or closing
the small window) stops the open: the project that was open stays open, unchanged, and the
status bar says `Open cancelled`. Unpacking a `.cRZ` is done by libserum in one step that cannot
be interrupted; a `Cancel` pressed then takes effect when it ends (`Cancelling when this step
ends...`). The project that was open stays in memory until the new one is complete, so for a
moment both are held. If the open fails, the project that was open stays as well.

Memory: the editor keeps a project in memory in a compact form. The parts of a frame that hold
one value throughout, which in real projects are most of them (the dynamic mask of a frame
without dynamic zones, the background masks of a frame without a background, the 64-row picture
of a frame without HD, rotation tables, unused dynamic colors), take no memory until they are
edited. What a project needs is roughly its originals and its colorized pictures:

| Project | `.cROM` | In memory after opening | Most while opening |
| --- | --- | --- | --- |
| `afm_113b` (3047 frames, `.cRZ`) | 267 MB | 81 MB | 350 MB |
| `trn_174h` (5211 frames, `.cRZ`) | 450 MB | 350 MB | 800 MB |
| `frankst1` (11199 frames at 192x64, `.cRZ`) | 711 MB | 440 MB | 1.2 GB |
| GoT (19042 frames, `.cROM`) | 1.67 GB | 0.88 GB | 0.92 GB |

A `.cROM` is read straight into the project; a `.cRZ` is first unpacked by libserum into memory
as a whole, which adds the size of its `.cROM` while it is being read. On a machine with 8 GB a
project with a `.cROM` of 2 GB opens comfortably; for the largest projects open the `.cROM`
rather than the `.cRZ`. Saving needs only a little more than the project itself (see "Saving").

Opening a project always starts in the frame view: `Edit Sprite` and `Select Capture` are switched
off, a pending sprite capture selection is dropped, and the mask browser selection starts again at
the first frame's mask.
Recent-project entries show the full path of the file that was opened (`.cROM` or `.cRZ`) and
internally remember that path and the `.cRP` path. If that file no longer exists when the entry
is used, the file with the same base name and the other of the two extensions is opened instead,
so an entry keeps working whether only the archive or only the loose `.cROM` is left.

## Creating A Project From A Dump

`File -> New Project From Dump...` makes a new project of the frames of a frame dump, as
`New project` of the Win32 editor does.

### The dump

A frame dump is a text file. Each frame is

- a line with the time the frame arrived, in milliseconds, as a hexadecimal number: `0x` and up
  to 8 digits (`0x29D89DCF`). `0X` and a bare `x` are accepted too;
- one line per row of the frame, one hexadecimal digit per pixel: the pixel's shade, `0` to `F`,
  upper or lower case.

Blank lines may stand between frames. Lines end with LF or CRLF; blanks around a line and a
byte order mark at the start of the file are ignored.

All frames of a dump have the size of its first frame. Four sizes are accepted:

| Dump | Project |
| --- | --- |
| 128x32 | ordinary project, 128x32 with an optional 256x64 HD |
| 128x16 | ordinary project; each frame is stored in the middle of a 128x32 frame, with 8 black rows above and below |
| 192x64 | project with 192x64 original frames |
| 256x64 | project with 256x64 original frames; every frame gets the 256x64 picture the runtime needs |

The project has 16 original colors if any frame of the dump has a shade above 3, otherwise 4.

Checked with a real 128x16 dump (`hook_501`, 12838 frames, 10476 different, shades 0 to 3): the
new project is a 4-color 128x32 project whose frames have the 16 dump rows in rows 8 to 23; it
saves, reloads to the same bytes, and libserum identifies every frame of the dump. The Win32
editor stored the existing `hook_501` project the same way (every one of its masks covers rows 0
to 7 and 24 to 31). The hosts that pad a 128x16 frame for libserum center it in the same way
(DMDext: `CenterFrame`, 8 rows above); libserum itself does not pad.
Frames that the filter leaves out count for that too.

The file is read strictly. Anything that is neither a time line, a row of the dump's width nor
blank is an error, and so is a frame with more or fewer rows than the first one (frames of
different sizes in one dump are not supported). The message names the line, and the column for a
character that is no hexadecimal digit, and nothing is imported. The one exception is a dump that
ends inside its last frame, which is what an interrupted recording leaves: that frame is left
out and the dialog says so with its line. (The Win32 editor stops silently at the first
character it cannot read and takes the frames before it.)

Reading does not load the file into memory; a dump of 100 MB is read in well under a second and
only the frames that are kept are held.

### The dialog

After the file was chosen, the dialog reads it at once and shows

- what it found: the frame size, 4 or 16 colors and the number of frames;
- what would be done: `N read, a duplicates, b too short, c with too many colors, e for the
  project`.

Nothing is changed before `Create Project` is pressed.

- `ROM name` is proposed from the file name up to its first dot. It is the name the runtime
  looks the colorization up by and the name `Save` proposes for the files. It has 1 to 63
  characters, unaccented, and none of `/ \ : * ? " < > |`; while it is not usable the line
  below it says why and `Create Project` is disabled.
- `Leave out frames shown for less than N ms` (5 to 3000). A frame is shown until the next
  frame of the dump arrives. A frame whose time cannot be told is taken as 30 ms and is never
  left out by this: the last frame, a frame followed by an earlier time, and a frame followed
  by a gap of more than 4000 ms.
- `Leave out frames with more than N shades` (1 to 16): frames with many shades are mostly
  fades between two pictures.
- Repeated frames are always left out: of frames with the same picture the first one that
  passes the filter stays, with its own duration; the time of the repetitions is not added.
- `Read Dump Again` reads the file with the options as they are now. Changing an option makes
  the shown result stale: the dialog says so and `Create Project` stays disabled until the dump
  was read again.
- `Stop` ends the reading; so does closing the dialog. A bar shows how far it is.
- `Choose...` takes another file.

The filter is applied in this order, as in the Win32 editor: duration, shades, repetition. A
frame counted as too short is not counted for its shades.

### The new project

- Every frame is uncolorized: its picture is the original in the plain original colors (a ramp
  from black to the brightest color), no comparison mask, no shape mode, no dynamic zone, no
  background, no sprite, no trigger, no color rotation.
- No comparison masks, sprites, backgrounds, sections or color sets. Palette 0 starts with the
  plain original colors, which are also the edit colors.
- The file header is revision 20.
- The project has no files yet: the window title says `<ROM name> (not saved yet)`, and the
  first `Save` asks where to (see "Saving"). It is not in `Open Recent` before that.
- A frame shorter than 15 ms is marked `inactive` in the frame list until it is colorized, as
  in every project (see "Frame List"). The runtime identifies it and shows nothing new for it.

If the open project has unsaved changes, the editor asks before it is replaced (Save, Discard
Changes or Cancel, see "Opening Projects").

Checked with libserum: a project made from a dump loads, and every frame of the dump that was
kept is identified as its own frame (the dump of `smb3`, 25127 frames, of which 21671 differ:
all 21671).

Not verified: there is no file of a project freshly created by the Win32 editor to compare
with. What a new project holds was taken from the Win32 source (`CopyTXTFrames2Frame`,
`Init_cFrame_Palette2`, `InitVariables`) and assumes a freshly started Win32 editor, whose
`.cRP` fields are all zero; a Win32 editor that had another project open before keeps that
project's color sets, mask names and drawing settings in the new one. One value differs on
purpose: in a project with 256x64 frames the unused bounds of a frame's sprite slots are the
whole 256x64 frame, where the Win32 editor writes 128x32.

## Adding Frames From A Dump

`File -> Add Frames From Dump...` appends the frames of a dump that the open project does not
have yet. The dialog is the one of "Creating A Project From A Dump", without the ROM name and
with one more option:

- `Leave out frames the project identifies with its masks and shape modes`, on by default.
  - On: a frame of the dump is left out if the runtime would identify it as a frame of the
    project, that is if some frame of the project has the same hash under that frame's own
    comparison mask and shape mode. This is exactly libserum's test; checked on `smb3` by
    feeding all 25127 frames of its dump to libserum with the project loaded: the 11469 frames
    the editor calls known are the ones libserum identifies, the other 13658 are not. Checked
    the same way on `hook_501` with its real 128x16 dump: 11661 of 12838 dump frames known and
    identified by libserum, 1177 not, no disagreement; of the 10476 different frames 9491 are
    left out as known and 985 are added.
  - Off: a frame is left out only if a frame of the project has exactly the same original,
    whatever that frame's mask is.

The summary then reads `N read, a duplicates, b too short, c with too many colors, d already in
the project (x identical, y identified through a mask or shape mode), e to add`.

The dump has to fit the project:

- its frames must have the size of the project's original frames (a 128x16 dump fits a 128x32
  project);
- a dump with shades above 3 cannot be added to a project with 4 original colors. A dump
  without them is added to a project with 16 colors as it is; the shades 0 to 3 are valid
  there.

Otherwise the dialog says why and offers nothing to add. (The Win32 editor does not check and
damages the project.)

The new frames are appended behind the last frame, in the order of the dump, uncolorized like
the frames of a new project. Sections and sprite source frames stay where they are. Adding is
one step for `Undo`, which removes exactly the added frames again; see "Undo And Redo". The
added frames are selected afterwards.

The document must not change while the dialog is open, which is why the dialog is modal;
playback is stopped when it opens.

## Saving

Use `File -> Save` or the standard Save shortcut.

A new project (see "Creating A Project From A Dump") has no files until it is saved for the
first time. `Save` and `Save As...` then ask for the `.cROM` to write, proposing `<ROM
name>.cROM` in the folder of the dump; the name the runtime looks for is the ROM name. If the
question is cancelled nothing is written and the project stays unsaved. After the first save the
window title shows the file name instead of `<ROM name> (not saved yet)`, the project is in
`File -> Open Recent`, and `Save` writes to those files without asking.

Every save writes three files with the same base name into the project folder: `<name>.cROM`,
`<name>.cRP` and `<name>.cRZ`. The archive holds exactly the bytes of the `.cROM` just written,
deflate-compressed, in one entry named `<ROM name>.cRom` (the ROM name stored in the project,
which need not be the file name), as the Win32 editor writes it. An archive that already exists
under another spelling of the extension (`.crz`) is the one that is replaced.

Use `File -> Save As...` to save the project under a new name. The app asks for a target `.cROM`
path and writes the `.cROM`, a `.cRP` and a `.cRZ` with the same base name there.

Use `File -> Export Current Frame PNG...` to export the current frame as a PNG. The picture is
libserum's output for the frame, the one a machine shows, not the editor's own rendering; see
"Runtime View" for how it is made. Which plane is exported:

- with the Runtime view on, the plane it shows;
- otherwise the plane of the editing mode: the 32-row plane in SD mode and the 64-row plane in HD
  mode. The 64-row plane exists for every frame; for a frame without HD it is libserum's upscale
  of the 128x32 picture;
- the one plane of a project with 256x64 or 192x64 original frames.

The proposed file name ends in `_sd` or `_hd`, or in `_256x64` or `_192x64`. `DMD Shape` and
`DMD Size` apply. The assigned background is always part of the picture: `Show BG` is a setting
of the editing canvas and has no effect on the export.

If libserum has no picture for the frame, nothing is written and no file dialog comes up. The
status bar says why: `Frame N is not exported:` followed by the reason the Runtime view gives for
the frame (the frame is inactive and skipped, it lacks its 256x64 data, or libserum's own error
text). The editor's own rendering is never written in its place: a file would then show what no
machine shows. There is no separate export of the editor's rendering any more. It served no
purpose the runtime export does not: the frames libserum renders nothing for are frames with
nothing colorized to show (an inactive frame is one that is not colorized, a 256x64 frame without
its data is black), and the image of a background is exported by `Export BG`.

Use `File -> Import Image Into Selected Frames...` or the Frame topic's `Import Image` button to
replace selected frame pixels from a PNG/BMP/JPEG. The image is scaled to the active SD/HD DMD size.
In HD mode, selected frames without HD data are skipped. The import is undoable per frame.

Save behavior:

- Saving runs in the background. A save that is done within a moment (about 0.3 s) shows
  nothing; a longer one brings up the `Saving Project` window: `Writing the .cROM and .cRP: N of
  M MB` with a bar, then `Packing the .cRZ archive (this step cannot be interrupted)...` with a
  busy bar, then `Replacing the project files...`. While it is up the main window takes no
  input at all: nothing can be edited, no menu or shortcut works, so the files hold exactly the
  project as it was when `Save` was chosen. The project is saved from its blocks as they are in
  memory, without a copy of them, so a save needs little memory beyond the project.
- `Cancel` (or Escape, or closing the small window) stops the save: the temporary files are
  removed, the project files on disk stay exactly as they were, the project keeps its unsaved
  changes, and the status bar says `Save cancelled: the project files on disk are unchanged`.
  The packing of the archive is libserum's and cannot be interrupted; a `Cancel` pressed then
  takes effect when it ends (`Cancelling when this step ends...`), and nothing is replaced.
  Once the files are being replaced, `Cancel` is disabled.
- All three files are written completely next to their targets first (as `<file>.saving`) and
  then swapped in, so a failed or interrupted save does not leave a truncated project behind. If
  any of them cannot be written, including the archive, the save fails as a whole, the temporary
  files are removed and the existing files stay as they were.
- Writing the archive compresses the whole `.cROM` at libserum's default level and takes most
  of the time of a save: for a 450 MB `.cROM` about 0.7 s for the `.cROM` and `.cRP` and 5.6 s
  for the archive; for a 1.67 GB `.cROM` about 2 s and 31 s.
- The `.cRZ` is a plain zip without zip64 as long as the `.cROM` is smaller than 4 GiB. It is not
  byte-identical to the Win32 editor's archive of the same project (other deflate settings, no
  timestamp extra field); the `.cROM` inside is.
- A ROM name containing `/`, `\` or `:` cannot be used as an archive entry name; saving such a
  project fails with a message.
- An unedited project saves back byte-identical to what the Win32 editor wrote. This is checked
  against real projects with the `colorizing_qt_v2_roundtrip` tool (see Testing Checklist). The
  one exception is the 96x32 data of a project with 192x64 original frames, which is discarded:
  the blocks of the extra plane are written as "no extra data" and differ from the file if the
  file held anything else there. All other blocks and the `.cRP` are identical.
- Frame identification hashes are recalculated from the original frame, its comparison mask, and
  its shape mode.
- The "active frame" flag is recalculated: a frame shorter than 15 ms stays inactive until it is
  colorized or gets dynamic content. The frame list marks the frames the runtime skips for that
  reason, see "Frame List".
- Colorized pixels covered by a dynamic mask are written as 0, as the Win32 editor does. The
  in-memory frame is not changed, so those pixels show their old color until the project is
  reopened. On the HD plane of an ordinary project this is the stored HD dynamic mask, which the
  runtime does not use and the editor does not show (see "Dynamic Masks"): an HD pixel painted
  under it is still written as 0, and is black on a machine wherever no SD dynamic zone covers
  it. The Runtime view shows that; the editing canvas shows the painted color.
- Editor metadata the app does not expose yet (color sets, mask names, sprite source
  frames, image crop presets, imported palettes, project time, PuP pack) is carried through.
  Deleting frames, sprites and comparison masks keeps the parts of it that are numbered by
  them in step, see "Adding And Deleting Records".
- The numbers of frames, comparison masks, sprites and backgrounds in the file headers are
  those of the project as it is; every block is written for exactly these records.
- HD pixels that the Win32 editor left in a frame or background whose HD is switched off are
  carried through until HD is created or removed for it here.
- Marks the document clean after successful save.

Files written by earlier Qt editor builds are not supported; only projects saved by the Win32
editor (currently 3.17.3) or by this serializer are.

### Exporting the .cROMc

`File -> Export .cROMc...` writes the file a machine loads, `<rom>.cROMc`, proposed beside the
project. libserum makes it, exactly as it does on a machine when it finds the `.cROM` alone:

- The project is taken as it is in the editor, saved or not. It is written as `<rom>.cROM`
  into a temporary folder together with the `<rom>.pup.csv` and `scaling.txt` of the
  project's folder, if they are there (in any case of the letters), and libserum loads it with
  `.cROMc` generation on. PUP scenes and the scaling settings are therefore in the `.cROMc`,
  as they would be on the machine. The status bar names the files that went in.
- Writing the `.cROM` shows its progress and can be cancelled; libserum's part cannot be
  interrupted and takes from about a second to half a minute for large HD projects.
- An existing file is replaced only once the new one is complete. A target name other than
  `<rom>.cROMc` is allowed; the status bar reminds that libserum looks for `<rom>.cROMc`.
- The Runtime view is switched off for the export and on again afterwards; playback stops.
- On a desktop, libserum loads a `<rom>.cROMc` it finds in the colorization folder instead of
  the `<rom>.cROM` beside it, without comparing their dates. An outdated `.cROMc` there hides
  later changes of the project: export again or remove it.

For an unedited project the result is byte for byte the `.cROMc` libserum generates from the
project's files (checked on `afm_113b`, `hook_501`, `trn_174h`, `smb3` and the 192x64
`frankst1`).

### Autosave and recovery

Every 10 minutes, as in CDMD, a project with changes that were not autosaved yet is written to
`<name>(auto).cROM` and `<name>(auto).cRP` beside its files (`afm_113b(auto).cROM` for
`afm_113b.cROM`; for a project opened from its `.cRZ`, beside the `.cRZ`). A new project that
was never saved is autosaved under its ROM name in the editor's own folder (on Windows
`%LOCALAPPDATA%\ColorizingDMD\ColorizingDMDQtV2\autosave`, on macOS
`~/Library/Application Support/ColorizingDMD/ColorizingDMDQtV2/autosave`, on Linux
`~/.local/share/ColorizingDMD/ColorizingDMDQtV2/autosave`).

- The files are written in the background while you go on working; they show the project as it
  was when the autosave started. The status bar says `Autosaved to <file> at <time>`, or why
  it failed. Nothing is written while a dialog is open or the project is unchanged.
- An autosave never replaces the project files and adds no `.cRZ`. An autosave stopped by
  quitting leaves the earlier autosave files as they were.
- Opening a project whose autosave files are both newer than its files asks first:
  `Open Autosave` opens the autosave as that project with unsaved changes (`Save` then writes it
  to the project files), `Open Project` opens the project files. The autosave files are kept
  either way, as CDMD keeps them.
- After `Discard Changes` the autosave holding those changes is not offered again; the files
  stay. A new project's autosave is deleted when its changes are discarded or when it is saved
  for the first time.
- At start the editor offers the newest autosave of a new project that was never saved:
  `Open` (as a new project with unsaved changes), `Delete` or `Later`.
- Opening an `(auto)` file directly works as for any project; its autosave then goes to the same
  files.

## Playback

The top toolbar contains editor-only playback controls:

- `Play` advances through the currently visible frame list.
- `Pause` stops playback.
- `Loop` restarts at the first visible frame after the last visible frame.
- `cRP` uses the per-frame durations loaded from the `.cRP` file.
- The interval field controls the fallback frame advance interval in milliseconds.

Playback respects the current frame filter in the left `Frames` tab. When `cRP` timing is enabled,
frames with missing, zero, or unusually large durations use the fallback interval.

During playback, the central preview advances color-rotation slots for the displayed frame using
each slot's stored delay, counted from the start of playback. Which pixels rotate follows libserum,
see "Color Rotations". This is an editor preview; libserum remains the runtime source of truth, and
its own rotation, with its own clock, is what playback in the Runtime view shows.

With the Runtime view on, playback is done by libserum instead: the frames are fed to it in the
order of the list, with the same timings, libserum rotates the colors, and it identifies every
frame as it would on a machine, which depends on the frames before it. See "Runtime View".

Rotation timing is not a visual preference. The per-slot delay stored in the project controls both
runtime behavior and editor playback, so future rotation-editing UI must keep timing edits explicit
and must be validated against libserum playback behavior.

## Frame List

The left frame list shows one thumbnail per loaded frame plus compact metadata.

### Sections and triggers

Above the frame list:

- The section box shows the section of the current frame: the one with the latest first frame
  at or before it (`- None -` before the first section). Choosing a section goes to its first
  frame. The row of a frame that starts a section shows the name, for example `3 [Game]`.
- `Name` starts a section at the current frame; on a frame that already starts one the button
  reads `Rename` and renames it. A name has 1 to 31 characters, is not `- None -` and is not
  the name of another section; otherwise nothing changes and the status bar says why. A
  project holds at most 512 sections.
- `Delete` deletes the section of the current frame, after a question. Its frames stay and
  belong to the section before it then.
- `Order` opens the list in its own order, which need not be the order of the first frames
  (as in CDMD): `Up` and `Down` move the selected section, `A-Z` sorts by name. Each step is
  applied at once.
- Every change of the sections is an undo step of its own (`Undo section change`), reachable
  from every view. Deleting frames moves the first frames (see "Deleting frames") and drops the
  section undo steps made before.
- The trigger box lists every frame with a trigger ID as `Fr: <frame> / TID: <id>`, sorted by
  ID and then frame, as CDMD's trigger list; it shows the current frame's entry when it has
  one. Choosing an entry goes to the frame. A frame hidden by the frame filter is not shown;
  the status bar says so.

Sections are editor data of the `.cRP`; the runtime does not use them.

Thumbnails are drawn when their rows come into view, not when the project is opened: a row that
was never scrolled to shows an empty dark box of the thumbnail's size, which is replaced by the
picture as soon as the row is in view (rows just above and below the visible part are prepared as
well). Rows have the same height with and without a picture, so scrolling, selecting, filtering
and keyboard navigation do not depend on it. A thumbnail that was drawn is kept and redrawn when
its frame changes; nothing has to be done to refresh it. Opening a large project therefore no
longer waits for one thumbnail per frame.

Frame items show the project frame number, the loaded cRP duration, and compact flags such as
`HD`, `Dyn`, assigned `BG`, and sprite assignment count when present.
`Dyn` means that the frame has a dynamic zone the runtime renders: at least one pixel of its
dynamic mask on the plane libserum takes dynamic zones from (the 128x32 plane of an ordinary
project, the 256x64 or 192x64 plane of the other layouts). Every frame of a loaded project carries
dynamic data, mostly all "not dynamic", so the flag does not just say that the data exists. It
follows dynamic mask edits, their undo and redo, and so does the frame's thumbnail.
Selecting a frame updates the central canvas and the frame status line.
Whenever the editor picks the current frame itself (after deleting or adding frames, after undo
and redo of such a step, after a filter change, during playback, `Go to frame`), that frame
becomes the only selected one; an earlier multi-selection does not stay behind.

A frame with a reference the runtime cannot follow has the line `ref. problem`, and its tool tip
and the frame status line's tool tip say which: a comparison mask, background or sprite ID that
names no record, or an empty sprite slot with used slots behind it (see "Sprites"). Only loaded
projects can have these, since the Win32 editor writes what it holds; the editor loads and saves
them unchanged and never makes new ones. None of the five 3.17.3 test projects has one.

A frame the runtime skips has the line `inactive`, and its tool tip and the tool tip of the
frame status line say `inactive: shorter than 15 ms and not colorized — the runtime skips it`.
These are the frames a save flags as inactive (shorter than 15 ms, every pixel still the plain
color of the original, no dynamic zone; the rule of the serializer, which is the Win32 editor's)
that have no assigned background either: libserum identifies such a frame and renders nothing
for it, so a machine keeps the picture it shows. An inactive frame with a background is rendered
by libserum and is not marked. The marker follows edits: painting the frame, giving it a dynamic
zone or a background, or a duration of 15 ms or more removes it.

The filter above the frame list can show all frames, only frames with HD data (`HD frames`),
frames with an assigned background, frames with a dynamic zone (the frames marked `Dyn`), frames
with sprite assignments, or the frames marked `inactive` (`Inactive`).
Editing still targets the real project frame number shown in the item label, not the visible row
number.

In a project with 256x64 or 192x64 original frames there is no `HD` flag in the items, no blue
`HD` badge on the thumbnails and no `HD frames` filter entry, and the project status line above
the list leaves out the HD count (`<rom>: 5 frames, 2 backgrounds, 3 sprites`). The frame status
line names the plane by its size: `Frame <n> | 256x64 | <duration> ms`.
In a project with 256x64 original frames the filter has the entry `Not rendered` instead, and
frames the runtime shows nothing for are marked in the list; see "Projects With 256x64 Original
Frames".

The frame list supports native multi-selection. Frame drawing, comparison-mask drawing,
dynamic-mask drawing, background-mask drawing, and all frame settings (comparison mask ID, shape
mode, background ID, trigger ID, switch-to-monochrome, duration) apply to all selected frames where
the operation is valid. Comparison-mask edits are grouped by assigned mask ID, so a shared mask is
edited once even if multiple selected frames use it. Undo/redo history is still stored per frame,
per resolution, or per shared mask as appropriate.

When an edit cannot apply to some of the selected frames, those frames are skipped and the others
are edited. The status bar then reports both counts, for example
`Assigned background 1 on 3 frame(s), 1 skipped (no background data)`. Frames that already have the
requested value are neither changed nor counted.

The selected frames are deleted with `Edit -> Delete Frames...`, with `Delete Frames...` in the
context menu of the frame list, or with the `Delete` or `Backspace` key while the frame list has
the focus. `Delete Frames Shorter Than...` (Edit menu and context menu) deletes those of the
selected frames that are shorter than a duration. See "Adding And Deleting Records".

## Adding And Deleting Records

Frames can be deleted; sprites, backgrounds and comparison masks can be added and deleted. Every
number that refers to such a record follows, in the project and in the `.cRP`, and every one of
these operations can be undone (see "Undo And Redo").

Where:

| Operation | Where |
| --- | --- |
| Delete the selected frames | `Edit -> Delete Frames...`; the frame list's context menu; `Delete` or `Backspace` in the frame list |
| Delete the selected frames that are shorter than N ms | `Edit -> Delete Frames Shorter Than...`; the frame list's context menu |
| Add the frames of a dump | `File -> Add Frames From Dump...`, see "Adding Frames From A Dump" |
| Delete frames that are the same as others | the `Mask` page of the Inspector, see "Same Frames And Common Pixels" |
| New comparison mask, delete the selected one | `New` and `Delete` above the list on the `Masks` page of the left browser |
| New background from the current frame, delete the selected one | `New` and `Delete` on the `Backgrounds` page |
| New sprite, delete the selected one | `New` and `Delete` on the `Sprites` page |

Limits, which are the file format's and the Win32 editor's: at least 1 frame, at most 64
comparison masks, 255 sprites and 255 backgrounds. `New` is disabled at the limit, `Delete`
while nothing is selected, the two frame entries while no frame is selected or the project has
one frame only.

Every deletion asks first. The question names what goes and what follows from it (how many
frames remain, how many frames use the record), `Cancel` is the default button, and the status
bar reports the result with its counts, for example `Deleted 3 frame(s), 12 remain` or
`Deleted background 1, 4 frame(s) have no background now; 6 background(s) remain`. Adding asks
nothing, except for the name of a new sprite.

After an operation, and after its undo or redo, the selection is on the new or restored
record, or on the record that took the place of a deleted one (the last one if the deleted one
was the last). Restored frames are all selected. The frame filter stays as it is: if the frame
that would be selected is filtered out, the next listed frame is. Playback stops, a canvas
selection and a pending free gradient are dropped, and after a change of the frames the sprite
capture selection as well.

### Deleting frames

Everything a frame has goes with it: original, colorized frame, dynamic data, background mask,
rotations, sprite slots, mask and background assignment, trigger and duration. The frames behind
it move up, so frame numbers change. Comparison masks, backgrounds and sprites stay, also those
no remaining frame uses.

All selected frames cannot be deleted: the status bar says
`Not deleted: all N frame(s) would go, and a project keeps at least one frame`.

`Delete Frames Shorter Than...` asks for a duration in milliseconds (preset: 15, the duration
below which an uncolorized frame is inactive) and deletes those of the selected frames whose
duration is lower. A frame of exactly that duration stays. The question then says how many of
the selected frames that is; if it is none the status bar says so and nothing is asked.

What follows a deleted frame in the `.cRP`, by the Win32 editor's rules:

- its imported palette, if the project has imported palettes for it;
- sections: a section whose first frame is deleted starts at the next remaining frame. If
  another section starts between its old first frame and that frame (the section has no frame
  left), it is removed. First frames behind a deleted frame move up. One case differs from the
  Win32 editor: a section whose frames reach to the end of the project and are all deleted is
  removed too, where the Win32 editor keeps it with a first frame behind the last frame;
- sprite source frames: the `.cRP` records for each sprite the frame it was taken from and the
  rectangle. A source frame number behind a deleted frame moves up; a sprite whose source frame
  is deleted loses its source rectangle (the first value is set to `0xFFFF`), as in the Win32
  editor.

### Sprites

`New` asks for a name and adds an empty sprite behind the last one: no pixel, no detection
area, shape mode off, no dynamic content. A name has 1 to 31 characters and must not be the
name of another sprite; otherwise nothing is added and the status bar says why. The new sprite
is selected. `Capture` then gives it its picture. The `.cRP` records the current frame as its
source frame and no source rectangle.

What a new sprite has besides, by project:

| Project | New sprite |
| --- | --- |
| ordinary | no HD (as in the Win32 editor); `Create HD Sprite` adds it |
| 256x64 original | its 256x64 data, empty, because the runtime renders a frame only if every sprite assigned to it has it |
| 192x64 original | no extra-plane data, like every sprite there |

`Delete` removes the sprite from the sprite slots of every frame. The slots behind it close up,
so that the remaining sprites of the frame are still looked for (the runtime's list ends at the
first empty slot); the emptied slot at the end keeps the bounds of the last slot. Sprites with a
higher number get a number one lower, in every frame and in the `.cRP` tables (names, source
frames, source rectangles and mirror flags).

### Backgrounds

`New` adds a background with the colorized picture of the current frame, as it is stored: not
the composed picture with the frame's own background, dynamic content and sprites. No frame is
assigned to it.

| Project | New background |
| --- | --- |
| ordinary | the SD picture; no HD image (as in the Win32 editor) |
| 256x64 original | the 256x64 picture, and needs a current frame that has one: otherwise `Not added: frame N has no 256x64 frame to make a background of` |
| 192x64 original | the 192x64 picture |

`Delete` takes the background away from the frames that use it (they have no background then)
and gives backgrounds with a higher number a number one lower in every frame.

### Comparison masks

`New` adds an empty mask (nothing masked) behind the last one and selects it. No frame uses it:
`Assign Mask` assigns it. The mask has the name its slot has in the `.cRP`.

`Delete` deletes the selected mask. If frames use it, the question says how many and which, and
that they will be compared without a mask, which changes how the runtime identifies them; they
are set to `None`. Masks with a higher number get a number one lower in every frame, and the
mask names of the `.cRP` move with them. (The Win32 editor has no way to delete a mask. There
the 64 mask slots always exist and the number of masks in the file is the highest assigned
number plus one. The Qt editor writes the masks the project has; a frame never refers to a mask
that is not written.)

## SD And HD Modes

Use the `SD` and `HD` toolbar buttons to switch the active editing resolution.

SD and HD exist in ordinary projects only (128x32 original frames with an optional 256x64 HD
plane). What the window shows for each kind of project:

| | Ordinary project | 256x64 original frames | 192x64 original frames |
| --- | --- | --- | --- |
| `SD` and `HD` toolbar buttons | shown | hidden | hidden |
| Frame list item | `HD` line for frames with HD | no `HD` line; `no picture`, `BG: no picture`, `Spr: no picture` for what is missing | no `HD` line |
| Thumbnail badge | blue `HD` | red `no picture` on frames that are not rendered | none |
| Frame filter | `HD frames` | `Not rendered` | neither |
| Project status line | `..., <n> HD, ...` | no HD count; `, <n> not rendered at runtime` | no HD count |
| Frame status line | `SD 128x32 \| HD 256x64` or `HD unavailable` | `256x64`, and `\| not rendered` | `192x64` |
| Frame page buttons | `Create HD`, `Remove HD` | `Create 256x64 Frame` | none |
| The same with `Edit Sprite` on | `Create HD Sprite`, `Remove HD Sprite` | `Create 256x64 Sprite` | none |
| Sprite and background lists | `(HD)` | `(no picture)` on records without 256x64 data | no suffix |
| Status texts, export file names | `SD`, `HD`, `_sd`, `_hd` | `256x64`, `_256x64` | `192x64`, `_192x64` |

Buttons and entries that do not apply are hidden, not disabled, and take no room: the toolbar
starts with `Mask` in a project without SD and HD, and the `Frame` page ends after `Import Image`
in a 192x64 project. While no project is loaded the window shows the ordinary set, disabled.

Rules:

- SD edits target the colorized frame pixels of the original plane, 128x32 in an ordinary
  project.
- HD edits target the colorized frame pixels of the extra plane, 256x64 in an ordinary project.
- A project with 256x64 original frames has one plane, the 256x64 one. See "Projects With 256x64
  Original Frames".
- A project with 192x64 original frames has one plane, the 192x64 one. See "Projects With 192x64
  Original Frames".
- The rest of this section is about ordinary projects.
- If a frame has no HD data, the canvas falls back to SD display/editing.
- Canvas hit testing maps mouse coordinates explicitly to SD or HD pixel coordinates.
- The Inspector `Create HD` button creates HD data for the current frame by scaling the SD colorized frame to 2x.
- When creating HD, a dialog asks whether to also upscale the frame background mask and the assigned background image.
- `Create HD` does not create HD dynamic data and the dialog does not offer it: the runtime renders
  the dynamic zones of the SD plane into the HD picture and does not read an HD dynamic mask. The
  frame's stored HD dynamic mask, colors and shadows are written as they are: for a frame that
  never had any that is a mask of 255 everywhere (no zone) and colors and shadows of 0, which is
  what the Win32 editor gives a frame without HD.
- In HD mode the `Dynamic` toolbar button is disabled, and the `Dyn` page shows and edits the
  dynamic colors and shadows of the SD plane. See "Dynamic Masks".
- The Inspector `Remove HD` button removes HD data from the current frame and switches the canvas back to SD when needed.
- `Create HD` and `Remove HD` can be undone. Undo puts back exactly what was there before: the
  frame's HD pixels (or their absence) and, for `Create HD`, every related item the dialog upscaled,
  including the previous HD image of the assigned background. See "Undo And Redo".

TODO: offer selectable scaling algorithms for HD creation instead of always using nearest-neighbor 2x scaling.

## Projects With 256x64 Original Frames

Most projects are made from 128x32 ROM frames. A revision 20 project can instead be made from
256x64 ROM frames (cROM header flag "256x64 original"). The frame status line of such a project
reads `Frame <n> | 256x64 | ...`.

The editor does not call anything in such a project "HD" or "SD": there is one plane, `256x64`.
This section still says "HD plane" and "HD data" where it describes the stored data, because
that is what the file format and libserum call the 256x64 plane.

The rule, which is libserum's: everything happens on the ROM frame at its own resolution.
Sprites are detected on the 256x64 original, the picture is rendered at 256x64, nothing is scaled
down, and a plane smaller than the ROM frame does not exist. So such a project has exactly one
rendered plane, the 256x64 one the file format calls "HD" or "extra". It lines up with the ROM
frame pixel for pixel: plane pixel (x, y) belongs to original pixel (x, y).

| Original | SD plane | HD plane |
| --- | --- | --- |
| 128x32 (ordinary project) | pixel (x, y) is original (x, y) | pixel (x, y) is original (x/2, y/2) |
| 256x64 | does not exist | pixel (x, y) is original (x, y) |

### What is edited

Everything the runtime uses, all of it on the 256x64 grid:

- the original frames and the comparison masks (frame identification hashes the whole 256x64
  original with the 256x64 comparison mask),
- per frame: the HD colorized frame, the HD dynamic mask, dynamic colors and shadows, the HD color
  rotations, the HD background mask, trigger, duration, mask and shape settings,
- the HD background images,
- sprites: the sprite original (what is detected), the detection areas, the shape mode, the HD
  sprite mask (what is drawn) and the HD sprite colors,
- per frame: the sprite slots and their bounds.

The canvas, the thumbnails, the frame list, playback, PNG export, image import, `Frame -> BG`,
`BG -> Frame`, background editing, the `Dynamic` and `BG` mask modes and the rotation and dynamic
pages all work on that plane.

### What is ignored

The file format still has room for 128x32 "SD" data, and the Win32 editor fills it: an SD
colorized frame, SD dynamic mask, colors and shadows, SD rotations and an SD background mask per
frame, SD background images, SD sprite colors and SD sprite dynamic data. libserum loads these
and never reads them. The editor does the same:

- They are loaded and written back unchanged, with their usual sizes, and never shown or edited.
  The file layout does not change.
- Nothing is derived from them: no upscaling of SD data into HD, no SD fallback for a missing HD
  item.
- Where the editor creates a sprite picture itself (`Capture`), it resets that sprite's unused SD
  data to neutral content: SD sprite colors all 0, SD sprite dynamic mask all 255 ("not dynamic"),
  SD sprite dynamic colors all 0. `Create HD` leaves the SD data of the frame, background or
  sprite as it is.
- On save a frame counts as colorized ("active") if its HD frame differs from the plain original
  colors or it has HD dynamic content. The SD frame does not count.

### No SD mode

The `SD` and `HD` toolbar buttons are hidden: there is nothing to switch. Every place where an
ordinary project falls back to SD when HD data is missing instead reports the missing 256x64
data in the status bar and does nothing (PNG export of a frame or background, `Frame -> BG`,
`BG -> Frame`, `Edit BG`, sprite `Capture`). The messages name the data by its size, for example
`Frame 4 has no 256x64 frame: there is no picture to export`.

### The 256x64 data is required everywhere

libserum renders a frame of such a project only if the frame has HD data, its assigned
background (if any) has HD data and every sprite assigned to it has HD data. Otherwise it still
identifies the frame and reports its trigger, but produces no picture: the display keeps what it
showed before. There is nothing it could fall back to.

Therefore:

- There is no `Remove` button, neither for a frame nor for a sprite: the ordinary project's
  `Remove HD` and `Remove HD Sprite` are hidden.
- A project in which that data is missing still opens. Each affected frame is flagged:
  - a red `no picture` badge on its thumbnail,
  - `no picture`, `BG: no picture` and `Spr: no picture` lines in the frame list item, with the
    full reason (which background, which sprites) as the item's tool tip, for example
    `Not rendered at runtime: background 1 has no 256x64 image; sprite 1 without 256x64 picture`,
  - `| not rendered` at the end of the frame status line, with the reason as its tool tip,
  - the project status line counts them: `..., 3 not rendered at runtime`,
  - the `Not rendered` filter of the frame list shows only these frames,
  - the sprite page lists an assigned sprite without its 256x64 data as
    `no picture: frame not rendered`, and the sprite and background lists, the background
    selector and the `Selected:` lines mark such records as `(no picture)`.
- A frame without its 256x64 frame is drawn as a black plane with the note `No 256x64 frame: the
  runtime shows nothing for this frame. Use Create 256x64 Frame.`, and cannot be painted on. Its
  original/reference region, comparison mask and settings can still be edited.
- Assigning a background or a sprite without 256x64 data to a frame is allowed and flags the
  frame the same way; `Assign` says so in the status bar.

Repairing:

- `Create 256x64 Frame` on the `Frame` page (the ordinary project's `Create HD`) is enabled for a
  frame that lacks something. For a frame without its 256x64 frame it creates that frame from the
  256x64 original, each pixel in the plain color of its original color index: the same picture an
  uncolorized frame has. It does not ask anything and upscales nothing; the frame's dynamic mask,
  dynamic colors, background mask and rotations stay as they are stored.
- `Create 256x64 Frame` is also enabled when only the assigned background lacks its 256x64
  image, and in both cases gives that background a black 256x64 image. An existing background
  image is never replaced. `Import Image` on the `BG` page is the other way to give a background
  its image.
- `Create 256x64 Sprite` (in the same place while `Edit Sprite` is on) creates the 256x64 sprite
  from the sprite original: its mask gets the shape of the sprite original, its colors are the
  plain colors of its color indices.
- All three can be undone.
- A button is disabled when there is nothing to create.

A sprite added with `New` has its 256x64 data from the start (empty), and a background added
with `New` takes the 256x64 picture of the current frame, so neither makes a frame "not
rendered". `New` on the `Backgrounds` page needs a current frame with its 256x64 frame. See
"Adding And Deleting Records".

### Sprites

Sprite coordinates are pixels of the 256x64 original. That holds for the sprite original, the
detection areas, the detection dwords and for the `Min X`/`Min Y`/`Max X`/`Max Y` bounds of a
frame's sprite slot, which range over 0..255 and 0..63 here.

- A sprite is looked for on the 256x64 original, at every position inside the slot bounds, odd
  rows and columns included.
- A sprite that is found is drawn one to one where it was found: HD sprite pixel (i, j) lands on
  plane pixel (found x + i, found y + j). The HD sprite mask says which pixels are drawn, the HD
  sprite colors with what. Both line up with the sprite original pixel for pixel, and so does
  the sprite editor: the colorized sprite on top is as large as the original sprite below it.
  (In an ordinary project the HD sprite is twice the size of its original.)
- `Show Bounds` draws the bounds over the original as they are.
- `Select Capture` marks single pixels of the 256x64 original.
- `Capture` takes the marked pixels, or the slot bounds, of the 256x64 original as the sprite
  original, the HD colors of the same plane pixels as the HD sprite colors, and sets the HD
  sprite mask to the captured shape. The sprite has HD data afterwards. `Capture` needs a frame
  with HD data.

### On a 128x32 display

There is no 128x32 version of such a colorization. A host that asks libserum for 32-row output
only gets no picture (frames are still identified and triggers reported); a host that asks for
32-row output "with fallback", for 64-row output or for both gets the 256x64 picture. Whether and
how that is shown on a 128x32 panel is the host's business, not the colorization's.

### What the other parts of the editor do differently

- The original/reference region shows all 256x64 original pixels, in the same on-screen size as
  for a 128x32 original. One original pixel is exactly as large as one pixel of the plane above
  it.
- The comparison mask overlay and all comparison mask tools work on the 256x64 pixels of that
  region.

### Status

Verified with constructed projects only (the automated tests and `tools/ui_dump`); no real
project with 256x64 original frames was available. The model follows libserum (branch
`native_hd`, "The ROM frame, and `is256x64` projects" in its `AGENTS.md`), which is the source of
truth. The Win32 editor's handling of such projects is not a reference: it treats the 128x32
data as a live plane, which the runtime does not.

Where the editor's picture can still differ from libserum's for these projects:

- A frame with HD data whose background or sprite lacks HD data is still drawn, without that
  background or sprite, so that it can be edited. The runtime draws nothing for it. The frame is
  flagged as described above.
- The Runtime view shows libserum's own picture of such a frame, that is its message that there
  is none, see "Runtime View".

## Projects With 192x64 Original Frames

Some ROMs deliver 192x64 frames. The Win32 editor stores such a project with the 192x64 ROM frame
as its original plane and a half-size 96x32 plane as its extra plane. The frame status line of
such a project reads `Frame <n> | 192x64 | ...`.

The same rule as for 256x64 projects applies: everything happens on the ROM frame at its own
resolution, and a plane smaller than the ROM frame is not a plane to edit. So the project has one
plane in the editor, the 192x64 one. It is the ROM frame's own grid: plane pixel (x, y) belongs
to original pixel (x, y). The editor calls it `192x64`, never SD or HD.

### What is edited

Everything on the 192x64 plane, with coordinates 0..191 and 0..63:

- the original frames and the comparison masks (frame identification hashes the whole 192x64
  original with the 192x64 comparison mask),
- per frame: the colorized frame, the dynamic mask, dynamic colors and shadows, the color
  rotations, the background mask, trigger, duration, mask and shape settings,
- the background images,
- sprites: the sprite original, the sprite colors, the detection areas; per frame the sprite
  slots and their `Min X`/`Min Y`/`Max X`/`Max Y` bounds, which range over 0..191 and 0..63.

The canvas, the thumbnails, the frame list, playback, PNG export, image import (the image is
scaled to 192x64), `Frame -> BG`, `BG -> Frame`, background editing, the `Mask`, `Dynamic` and
`BG` modes, `Select Capture`, `Capture` and `Show Bounds` all work on that plane. A sprite is
looked for on the 192x64 original and drawn one to one where it was found. The sprite editor shows
the same 256x64 sprite cell as in every project.

On the canvas a 192x64 pixel is as large as a pixel of an HD frame or of a 256x64 original; the
frame is 3:1 instead of 4:1, and the thumbnails keep that shape.

On save a frame counts as colorized ("active") if its 192x64 frame differs from the plain
original colors or it has dynamic content on the 192x64 plane, or if it lasts 15 ms or longer.

### The 96x32 data is discarded

The file has room for a full set of 96x32 data: a colorized frame, dynamic mask, dynamic colors
and shadows, rotations and a background mask per frame, background images, sprite masks, sprite
colors and sprite dynamic data, and the flags that say which frames, backgrounds and sprites
have such data.

The editor drops all of it when the project is opened and holds no 96x32 data. On save the
blocks of the extra plane are written as "no extra data", in the same sizes as before, so the
file layout and the header (192x64 with 96x32) do not change and nothing is padded or converted:

| Extra-plane data | Written as |
| --- | --- |
| "has extra data" flag of every frame, background and sprite | 0 |
| colorized frames, background images | 0 |
| dynamic masks | 255 (not dynamic) |
| dynamic colors, dynamic shadow directions and colors | 0 |
| color rotations | 0 (no rotation) |
| background masks | 1 |
| sprite masks | 255 (not part of the sprite) |
| sprite colors | 0 |
| sprite dynamic masks | 255 (not dynamic) |
| sprite dynamic colors | 0 |

These are the values the Win32 editor gives a new frame, background or sprite that has no extra
resolution.

What this means:

- A project that never had 96x32 pictures loses nothing visible. Its file can still change on
  the first save, because the Win32 editor leaves other values in the unused blocks (for example
  dynamic colors it copied to both planes, or background masks of 0 in a project converted from
  an old file format).
- A project that had 96x32 pictures in use (frames, backgrounds or sprites flagged as having
  extra data) loses them with the first save from this editor. They cannot be brought back from
  the saved file; keep a copy of the original files if they matter. The 192x64 picture is not
  affected.
- Saving twice writes the same bytes: after the first save the extra plane is already "no extra
  data".
- Everything on the 192x64 plane, and the whole `.cRP`, is written back as it was loaded or
  edited.

A new sprite or background (`New` on the `Sprites` and `Backgrounds` pages) has 192x64 data only
and is written with "no extra data" like every other record.

### What is not there

- No `SD` and `HD` toolbar buttons.
- No `Create HD`, `Remove HD`, `Create HD Sprite` or `Remove HD Sprite` button, and therefore no
  `Create HD` dialog.
- No `HD` flag, badge, filter entry or counter, and no `(HD)` suffix in the sprite and
  background lists.
- Status texts and proposed file names say `192x64` where they say `SD` or `HD` in ordinary
  projects.

### Status

Verified with one real project saved by the Win32 editor 3.17.3 (`frankst1`: 11199 frames, no
sprites, no backgrounds, no 96x32 pictures in use), and with constructed projects in the
automated tests and `tools/ui_dump` that also have sprites, backgrounds and 96x32 data in use.
`frankst1` loads, shows and saves; a load and save changes two blocks of its extra plane, the
dynamic colors (the file has values there) and the background masks (the file has 0, the editor
writes 1), and nothing else.

Nothing is padded or converted to another geometry.

The editor's picture of the 192x64 plane is the same as libserum's, see "Preview Rendering";
the Runtime view shows libserum's own.

## Canvas

The central canvas shows:

- The active colorized frame region.
- The original/reference frame region below it. It always shows every pixel of the original
  frame and has the same on-screen size as the SD frame: 128x32 pixels, or 256x64 pixels of
  half the size in a project with 256x64 original frames, where the colorized region above it is
  always the 256x64 plane. In a project with 192x64 original frames both regions are 192x64
  pixels of that half size, so they are three quarters as wide.

The canvas does not draw text labels or white frame outlines over the image data.

Drawing previews are transient overlays. They do not modify project data until mouse release or click commit.

The original/reference region shows the ROM frame's shades on an amber ramp from dark to
bright: 4 steps in a project with 4 original colors, 16 in one with 16 (shades 0, 5, 10 and 15 of
16 have the colors of shades 0 to 3 of 4); the frame thumbnails and the sprite editor's original
use the same colors.

Use the `Mask` toolbar toggle to show the current frame's assigned comparison mask over the original/reference region.
When `Mask` is active, clicks on the original/reference region add pixels to the assigned comparison mask.

Use the `Dynamic` toolbar toggle to show and edit dynamic-mask pixels over the colorized frame.
Dynamic zones belong to the plane that has the size of the original frame; in an ordinary project
that is the SD plane, and the toggle is disabled in HD mode. See "Dynamic Masks".

Use the `BG` toolbar toggle to show and edit the current frame's background-mask pixels over the active colorized frame.
The background-mask overlay and edits follow the active SD/HD mode.

The left sidebar is the visual component browser:

- `Frames` shows frame thumbnails and the frame filter.
- `Masks` shows comparison-mask thumbnails; selecting one selects that mask in the Inspector `Mask`
  topic for assignment. Selecting does not change any frame, and the selection stays when another
  frame becomes current.
- `Backgrounds` shows background thumbnails; selecting one selects the same background in the Inspector for assignment or editing.
- `Sprites` shows sprite thumbnails; selecting one selects the same sprite in the Inspector.

Frame metadata, dynamic colors, and sprite controls live in selectable topics in the right-side Inspector:

- `Frame` contains frame/background assignment, trigger IDs, DMD render settings, and the HD frame
  actions (`Create 256x64 Frame` alone in a project with 256x64 original frames, none in one with
  192x64 original frames).
- `Mask` contains comparison-mask assignment, comparison mode, and a shortcut into mask editing.
- `Background` contains the selected background ID, assignment, and edit shortcut.
- `Dynamic` contains the dynamic set selector and dynamic color swatches. In HD mode of an
  ordinary project it shows the SD plane's colors and shadows, with a note saying so.
- `Rotation` contains color rotation slots for the current frame and active SD/HD mode.
- `Palette` contains the selected cRP palette name and all 64 palette colors.
- `Sprites` contains sprite selection, naming, assignment, bounds, detection areas, and sprite editing.

The `Frame` topic exposes:

- `Target` selects whether ordinary drawing tools edit the current frame or the selected background image.
  It is enabled whenever the project has a background to edit, that is, whenever a background is
  selected.
- `BG ID` assigns the shared background for all selected frames. Selected frames without background
  data are skipped.
- `Trigger ID` edits the per-frame legacy trigger ID for all selected frames. `None` means the frame
  triggers nothing and is stored as `0xFFFFFFFF`, the value the cROM uses for it; `0` is an ordinary
  trigger ID. New frames start with `None`.
- `Duration` edits the per-frame `.cRP` duration for all selected frames.
- `Switch mono` writes the legacy switch-to-monochrome trigger ID `65432` on all selected frames and
  disables manual trigger-ID entry while checked. Unchecking it sets `Trigger ID` to `None` on the
  selected frames that carry the monochrome trigger; selected frames with an ordinary trigger ID
  keep it. To get an earlier trigger ID back, use Undo.
- `BG ID`, `Trigger ID`, `Duration`, and `Switch mono` edits can be undone, see "Undo And Redo".
- `DMD Shape` selects square pixels or round dot pixels for the main canvas and PNG export.
- `DMD Size` sets the relative pixel fill size in 10% steps. Values below `100%` leave black spacing between pixels, which makes the DMD grid visible.
- `Show BG` renders the assigned background layer into the colorized frame preview and is enabled
  by default.
- `Import Image` replaces selected frame pixels from an image file in the active SD/HD mode.

`DMD Shape` and `DMD Size` are view/export settings only. They do not change project data or the cROM/cRP file format.

The `Background` topic exposes:

- Backgrounds are selected visually from the left sidebar.
- The tab shows the selected background ID.
- `Assign BG` assigns the selected background to all selected frames.
- `Edit BG` switches ordinary drawing tools to the selected background image. If the sprite editor
  is open, it is closed first (`Edit Sprite` is switched off).
- `Frame -> BG` replaces the selected background image with the current frame image in the active SD/HD mode.
- `BG -> Frame` copies the selected background image into all selected frames in the active SD/HD mode.
- `Import Image` replaces the selected background image from a PNG/BMP/JPEG, scaled to the active SD/HD DMD size.
  In HD mode, a background without HD data gets its HD image from the import; undoing the import
  removes that HD image again.
- `Export BG` writes the selected background image as a PNG using the current DMD dot/square render settings.

## Runtime View

`View -> Runtime View` (`Ctrl+R`) shows the current frame on the canvas as libserum renders it,
in place of the editable layers. The checkable `Runtime` button in the toolbar, right of the
playback controls, is the same switch and shows whether the view is on. The picture is libserum's own output, not the editor's
rendering: the editor writes a small cROM that holds this frame and what it uses (its comparison
mask, its background, the sprites in its sprite list), loads it into libserum from memory, feeds
it the frame's original and shows the plane that comes back. Nothing is written to disk.

While the view is on:

- A line above the picture says `Runtime view: libserum output, N rows. Editing is off.`
  (`N rows, playing` during playback), the frame status line reads
  `Frame N | Runtime view, N rows | editing off`, and the drawing tool buttons, `SD`, `HD`,
  `Mask`, `Dynamic` and `BG` are disabled.
- The canvas takes no clicks or strokes, and no overlay is drawn: no comparison mask, dynamic
  mask, background mask, sprite bounds, selection or tool preview. The original/reference region
  below the picture stays.
- The Inspector keeps working. A change of the document shows in the picture at once: an edit
  made in the Inspector, undo and redo.
- `Show BG` has no effect: libserum always renders the assigned background.
- Opening the sprite canvas (`Edit Sprite`) switches the view off.

Switching the view off returns to editing exactly as it was: the same mode, tool, overlays and
selection.

Which plane is shown:

- In an ordinary project `View -> Runtime Plane: 32 Rows` (`Ctrl+3`) and `Runtime Plane: 64 Rows`
  (`Ctrl+6`) choose between what a display with 32 rows and one with 64 rows gets. When the view
  is switched on it starts with the plane of the editing mode, 32 rows in SD and 64 rows in HD.
  The 64-row plane exists for every frame: for a frame without HD it is libserum's upscale of
  the 128x32 picture, which the editor cannot show in any other way.
- A project with 256x64 or 192x64 original frames has one plane and no choice; the two menu
  entries are hidden.

When libserum has no picture, the reason is shown in its place:

- `libserum identifies this frame and shows no picture for it.` In a project with 256x64
  original frames this is a frame without its 256x64 data, or with a background or sprite
  without it.
- `libserum identifies this frame and skips it: it is inactive (shorter than 15 ms and not
  colorized), so a machine keeps the picture it shows.` These are the frames the frame list marks
  `inactive`, see "Frame List". The frame status line ends in `| inactive` for them.
- `libserum does not identify this frame.` No frame of the project matches the original.
- `libserum: ...` with libserum's own error text, if the frame could not be loaded.

Playback with the view on is done by libserum. `Play` feeds the frames of the list to it in list
order, from the current one on, with the usual timings; libserum keeps its state from one frame
to the next as it does on a machine, and its color rotations run on libserum's own clock: the
editor calls libserum's rotation step when libserum says the next one is due and shows the
planes libserum then holds. It does not rotate anything itself in this view. Outside playback
the view is a still; nothing rotates in it.
Switching the view or its plane stops playback.

**Frame identification depends on order.** libserum does not recognise a frame by its picture
alone. Every frame has a comparison mask and a shape mode, and its hash is taken from its
original under that mask. Masks are shared, so several frames can have the same hash: frames
that differ only where their mask covers them, or a frame of one mask that looks like a frame of
another under the other's mask. For such an original libserum's answer depends on what it
identified before:

- its search starts at the frame it identified last (the first frame of the project after
  loading) and walks through the frames in project order, wrapping around at the end;
- the first time it meets a mask and shape mode it hashes the incoming frame with them and looks
  for frames of that mask and mode with this hash; the first mask and mode that has one decides;
- of those frames it takes the first at or after the place where it met that mask and mode.

Playback in the Runtime view follows this exactly as a machine with the whole project loaded
does, starting like a machine that has just loaded the project. So of several frames that match
an original it shows the one a machine shows after the frames played before, which need not be
the frame selected in the list. Where the two differ the view says so: the frame status line
ends in `| identified as frame N`, the line above the picture adds
`Note: libserum identifies this ROM frame as frame N.`, and the tool tip of the status line
explains it. An author who sees this has two frames a machine cannot tell apart in this order,
which usually means one of them needs another comparison mask.

Outside playback the view shows the selected frame rendered on its own, whatever a machine would
make of its original. If a machine coming from the frame before it in the project (from the
first frame, for the first one) would identify the original as another frame, the status line
ends in `| after frame M: frame N` and the line above the picture adds
`Note: after frame M a machine identifies this ROM frame as frame N.` The picture stays that of
the selected frame. This hint is computed from the search rule above and costs no rendering.

How playback is loaded: the project is never loaded into libserum as a whole. The editor loads a
window: the next frames to play, at most 48, together with every other frame of the project that
takes part in identifying them. These are the frames that match the original of a played frame
(the ones the search can end at and start from) and, for each of them and each of their masks
and shape modes, the first frame with that mask and mode at or after it (the frames at which the
search meets the mask). With them the search runs in the window as it does in the whole project.
A window usually holds a few frames more than it plays; it stops growing at 160 frames, and
then plays fewer than 48. When playback goes past a window the next one is loaded and libserum's
identification state is carried over: the new window starts with the frame identified last, and
the original fed last is fed once more, unseen. Loading a window takes 15 to 100 ms on the real
projects measured (about 3 to 14 MB each); a whole project of 11199 frames plays in 234 windows.

Limits of that:

- What libserum keeps from frames before a window other than the identification, namely a
  monochrome switch set off by an earlier frame and the phase of running color rotations, starts
  again with the window.
- On the 64-row plane libserum leaves single pixels as an earlier frame painted them (seen at
  one or two pixels in 19 of 4430 played frames of `smb3` and 13 of 5211 of `trn_174h`). Which
  earlier frame that was differs between a window and a machine that has played the whole
  project. The 32-row plane was identical in every played frame.

What the view does not show:

- Scenes from a `.pup.csv` and the settings of a `scaling.txt`: only the cROM is loaded, so the
  64-row plane uses libserum's default upscaling (Scale2x) and shadow offset.
- Triggers, and what a trigger does on a machine. A frame with `Switch mono` is shown colorized.

What the editing canvas shows, by comparison: it is the editor's own rendering of the layers
being edited. On the plane that has the size of the original frame it follows libserum's rules
and gives the same pixels (see "Preview Rendering" for how that is checked). On the HD plane of
an ordinary project it follows them too, with pixels doubled where libserum upscales with
rounded edges; see the known differences there.

## Frame Tester

`View -> Frame Tester...` (`Ctrl+T`) feeds ROM frames, one after the other, to libserum with
the whole project loaded, as on a machine (CDMD's Frame Tester). The project is taken as it is
in the editor, saved or not.

- The frames are the frames selected in the frame list when the tester opens
  (`Selected Frames`), or every frame of a frame dump (`Load Dump...`, a TXT dump as for
  `New Project From Dump`, repeats included; a 128x16 dump is taken centred in 128x32 frames).
  A dump must have the project's frame size and no shade the project's frames cannot have.
  A frame of a dump lasts until the next one, as the dump import measures it.
- The tester shows libserum's 32-row and 64-row planes (a project with 256x64 or 192x64
  original frames has the 64-row one only) and below them the ROM frame that was fed. The line
  under the pictures gives the position, the project frame or the dump time, the duration, and
  libserum's answer: `identified as frame N`, `(inactive: the display keeps its picture)`, `not
  identified`, `libserum keeps the picture on display`, and a trigger it reported. Color
  rotations run.
- The slider and the position box go to a frame; `Play` and the backwards `Play` step through
  the frames with their durations at the chosen speed (1/4x to 32x); `Loop` starts again at the
  other end.
- libserum carries its state from one fed frame to the next, also when the position jumps: the
  answer for a frame can depend on what was fed before it, as on a machine.
- `Next Unidentified` feeds the frames after the current one until libserum does not identify
  one and stops there; if every frame up to the end is identified, it stays where it was.
- `Find Frame` looks for a project frame: among the selected frames, or in the dump from the
  frame after the current one on, going round, comparing the ROM frames pixel by pixel. With
  `Under its mask` the pixels the frame's comparison mask covers are left out.
- `Add To Project` (dump only) adds the dump frame behind the project's frames, uncolorized,
  with its duration; it is an undoable step of the project. libserum then loads the project
  again.
- `Show In Editor` closes the tester and goes to the frame libserum identified (or to the
  tested project frame).
- The Runtime view is switched off while the tester is open and on again afterwards. The tester
  holds the project's `.cROM` in memory while it is open (400 MB for `smb3`); loading it takes
  about a second.

Not there: output to a real DMD (ZeDMD) and the PuP tester.

## Colors

### Draw color, favorites and edit colors

The active draw color is what `Point`, the shapes and `Flood` paint with in single color mode.
It is shown in the read-only box at the end of the favorite swatches of the toolbar. It is set
by a click on a favorite, a palette color, an edit color or with the `Pick` tool.

- **Favorites**: four toolbar swatches (red `0xf800`, green `0x07e0`, blue `0x001f`, black
  `0x0000`). A click makes one the draw color; drag a palette color onto one, or use its context
  menu `Edit Color...`, to change it. Favorites are not saved in the project.
- **Edit colors**: the row under the toolbar shows the project's edit colors, the 16 colors the
  `.cRP` keeps (`acEditColorsS` of the Win32 editor). A project with 4 colors shows 4 swatches,
  one with 16 colors 16: one per shade of the original frames. Under each swatch a strip in the
  shade's own color (the amber ramp of the original frame) shows which shade the edit color
  stands for in color set replace mode.
  - Click a swatch to select it: its edit color becomes the draw color, and stays the draw color
    when the edit color changes (undo, a loaded color set, `Dyn > Edit`). Choosing another draw
    color (favorite, palette, `Pick`) leaves no swatch selected.
  - Double-click a swatch, or use its context menu `Edit Color...`, to change the edit color in
    the color picker. Drag a palette color onto a swatch to set it.
  - The tool tip names the color, the shade it replaces and its key.
- Edit colors are project data: they are saved in the `.cRP`, and every change is undoable (see
  "Undo And Redo").

### Draw color modes

The two buttons at the start of the row choose how `Point`, the shapes, `Flood` and multi-frame
edits color the frame (the Win32 editor's `IDC_1COLMODE` / `IDC_4COLMODE`):

- `1 Color`: every painted pixel takes the draw color.
- `Set Replace` (color set replace, the Win32 "4 color mode"): every painted pixel takes the edit
  color of **its own original shade**: a pixel whose original frame pixel has shade 2 becomes edit
  color 2. In a 16-color project shades 0 to 15 take edit colors 0 to 15.
  - On several selected frames each frame uses its own original frame, so the same stroke can
    give each frame other colors. (The Win32 editor's `Point` tool takes the shades of the current
    frame for all selected frames, its shapes and fills each frame's own; here all tools take each
    frame's own.)
  - On the HD plane of an ordinary project an HD pixel takes the shade of the original pixel it
    shows (the one at half its coordinates). On the 256x64 plane of a project with 256x64
    originals and on the 192x64 plane of a project with 192x64 originals the plane lines up with
    the original frame pixel for pixel.
  - `Flood` fills the same area as in `1 Color` mode: the pixels of the current frame connected to
    the clicked one that have its color (the Win32 fill does not depend on the mode either). Each
    selected frame then gets its own shades' edit colors in that area. Unlike `1 Color`, a fill
    on an area that already has the draw color still runs.
  - With Inspector `Target` set to `Background`, the background takes the edit colors of the
    current frame's shades (a background has no original frame of its own).
  - The `Grad` tool and `Grad Sel` always paint palette gradients; the Win32 editor's third mode
    (gradient) is that tool here. The sprite canvas always paints with the draw color, as in the
    Win32 editor. The preview while dragging shows the shape in the draw color.
  - `Shift` does not erase frame pixels in either mode.

The mode is stored in the `.cRP` (`DrawColMode`), as the Win32 editor stores it: a project saved
in color set replace mode opens in it. A stored `2` (the Win32 gradient mode) opens in `1 Color`
mode, as the Win32 loader does, and is kept in the file until the mode is changed. Changing the
mode is not an edit: it is neither undoable nor does it mark the project as changed; the next
save writes it.

### Keys for the edit colors

While the frame canvas has the keyboard focus (click it), keys select an edit color slot. The keys
are chosen by their place on the keyboard, as in the Win32 editor:

| Slots | QWERTY | AZERTY (Win32 names) |
|---|---|---|
| 0-3 | `1` `2` `3` `4` | `1` `2` `3` `4` |
| 4-7 | `Q` `W` `E` `R` | `A` `Z` `E` `R` |
| 8-11 | `A` `S` `D` `F` | `Q` `S` `D` `F` |
| 12-15 | `Z` `X` `C` `V` | `W` `X` `C` `V` |

The letters work in 16-color projects only, and not with `Shift`; no key works with `Ctrl`,
`Alt` or `Cmd`. None of them is a shortcut elsewhere in the editor. The place of a key is read
from the native key code where the platform gives one (the macOS virtual key, the Windows scan
code, the X11/Wayland key code); elsewhere the key is taken by its letter on a QWERTY layout. The
keys are not configurable yet.

### Color sets

The Inspector's `Sets` topic lists the 64 color sets of the `.cRP` (the Win32 color set dialog):
the number, the name and the colors of each active set, `(inactive)` for the others.

- `Store` (Win32 `Set`): the selected set takes the edit colors and becomes active. Only as many
  colors as the project has are stored (4 or 16); the other entries of the set stay as they
  were.
- `Load`, or a double click on an active set (Win32: a click): the edit colors take the set's
  colors, again only as many as the project has.
- The name field renames the selected active set when it is left or `Enter` is pressed (63
  characters at most). Inactive sets cannot be renamed, as in the Win32 dialog.
- `Deactivate` makes the set inactive and keeps its colors and name; `Store` activates it again.
  The Win32 editor has no way to deactivate a set.
- All of these are undoable. Sets, names and active flags are saved in the `.cRP`; a project that
  is opened and saved without changing them writes their bytes unchanged, including what the
  Win32 editor left behind the end of a name.
- The `.cRP` also stores the first set the Win32 dialog showed: the list scrolls to it when a
  project is opened, and scrolling the list by hand updates it (not counted as a change, like the
  draw color mode). The current set the file names is selected when a project is opened; the
  Win32 editor never changes that value, and neither does this one.

### Color picker

Everywhere a color is chosen (edit colors, favorites, palette colors, dynamic colors, the
dynamic shadow color, color rotation colors) the editor's color picker opens. It has the fields
of the Win32 picker, all kept in step with each other:

- a hue/saturation square and a value bar,
- `RGB888` as six hex digits,
- `R8 G8 B8` (0-255),
- `RGB565` as one decimal number (0-65535),
- `R5 G6 B5` (0-31, 0-63, 0-31), each of which changes its own component only,
- `H S V`: hue in degrees, saturation and value in percent, with one decimal,
- the last 8 picked colors (most recent first; a click takes one), kept in the application
  settings and shared by all projects,
- `Before` and `Now`.

The result is the RGB565 color of the fields. A color that is shown and entered again in any field
gives the same RGB565 value back; that holds for all 65536 RGB565 colors (with whole numbers for
H, S and V, as the Win32 picker shows them, about 10,000 colors would come back different).

### Edit colors and dynamic sets

`Edit > Dyn` (Win32 `IDC_COLTODYNA`) copies the edit colors into the current dynamic set
(`Dyn Set` on the `Dyn` page) of every selected frame that has dynamic data; `Dyn > Edit`
(`IDC_DYNATOCOL`) copies the current frame's dynamic set into the edit colors. Both use the plane
dynamic data is edited on (see "Dynamic Masks") and as many colors as the project has. `Edit >
Dyn` is one undoable step per frame, in the frame's dynamic color history; `Dyn > Edit` is one step
in the edit color history.

## Drawing Tools

Drawing tools operate on the active SD/HD mode. `Point`, the shapes and `Flood` paint with the
draw color or, in `Set Replace` mode, with the edit color of each pixel's original shade (see
"Draw color modes" under "Colors").

When Inspector `Target` is `Frame`, ordinary drawing tools edit the active colorized frame.

When Inspector `Target` is `Background`, ordinary drawing tools edit the selected shared background image. `Pick` samples from that background image, flood fill fills the background image, and the edits are recorded in the history of that background and resolution.

### Point

Click one pixel to paint it with the active draw color (in `Set Replace` mode: the edit color of its
original shade).

### Line

Press, drag, and release to draw a line.

The preview is shown while dragging. The whole line is committed on release as one undoable edit.

### Rect

Press, drag, and release to draw a rectangle outline bounded by the start and end pixels.

The preview is shown while dragging. The whole rectangle is committed on release as one undoable edit.

### Fill Rect

Press, drag, and release to draw a filled rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled rectangle is committed on release as one undoable edit.

### Circle

Press, drag, and release to draw a circle outline.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole circle is committed on release as one undoable edit.

### Fill Circle

Press, drag, and release to draw a filled circle.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole filled circle is committed on release as one undoable edit.

### Ellipse

Press, drag, and release to draw an ellipse outline inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole ellipse is committed on release as one undoable edit.

### Fill Ellipse

Press, drag, and release to draw a filled ellipse inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled ellipse is committed on release as one undoable edit.

### Grad

Press, drag, and release to fill the selected Palette `Shape` with a directional palette gradient.

The filled pixels sample the actual palette entries from `From` through `To`; the canvas tool does
not recalculate colors between the endpoints. Palette color numbers are displayed as `1` through
`64`, while the saved cRP data remains unchanged internally.

The Palette tab separates gradient behavior into:

- `Shape`: the selected area to fill, currently rectangle, circle, or ellipse.
- `Pattern`: how colors are distributed inside the selected area, currently linear, circle, or ellipse.
- `Direction`: the color order used by the fill. `Free` is a two-step workflow: first draw the shape
  or selection area, then draw a virtual direction line that defines the gradient vector. The fixed
  linear directions ignore the mouse drag direction and use the selected area's bounds instead.
  Circle and ellipse patterns use the selected area's bounds for radial fills; `Edge -> Center`
  reverses radial fills.

This is currently implemented for colorized frame pixels and selected background image pixels. Each
gradient fill is committed as one undoable edit.

### Sel

Use `Sel` to create a persistent canvas selection before applying a palette gradient to that exact
area.

Create a selection:

1. Open the Inspector's `Palette` topic.
2. Choose the selection shape in `Shape`: rectangle, circle, or ellipse.
3. Select the toolbar tool `Sel`.
4. Press, drag, and release on the colorized canvas.
5. The selected pixels are shown as a translucent cyan overlay.

`Sel` selects in every mode, also while a mask or the sprite capture is being edited; it never
paints a mask.

Fill a selection with a fixed-direction gradient:

1. Create a selection.
2. In the `Palette` topic, set `From` and `To`.
3. Set `Pattern` to `Linear`, `Circle`, or `Ellipse`.
4. Set `Direction` to a fixed direction such as `Left -> Right`, `Top -> Bottom`,
   `Center -> Edge`, or `Edge -> Center`.
5. Click `Grad Sel`.

Fill a selection with a free-direction gradient:

1. Create a selection.
2. In the `Palette` topic, set `From`, `To`, and `Pattern`.
3. Set `Direction` to `Free`.
4. Click `Set Dir`.
5. Press, drag, and release on the same canvas to draw the virtual gradient direction line.
6. The stored direction line is shown in the cyan selection overlay.
7. Click `Grad Sel`.

Clear or replace a selection:

1. Click `Clear Sel` to clear the current selection.
2. Or select `Sel` again and draw a new selection; the previous selection and stored direction line
   are replaced.

Selections are cleared when the frame, SD/HD mode, target, selected background, or tool context
changes. Selection gradient fills are undoable through the same frame/background edit histories as
ordinary pixel edits. A selection can also be made and changed with `Ctrl`, the `Edit` menu and
the other helpers, see "Selection"; `Grad Sel` then fills the selected pixels over their bounding
box.

### Pick

Click a colorized frame pixel to set the active draw color to that pixel's RGB565 value.

Hold `Shift` while clicking to select the edit color slot whose color is the pixel's color instead
(the last one if several are, as in the Win32 editor); the draw color becomes that edit color. The
status bar says when no edit color has the color. Without `Shift` the Win32 editor writes the
picked color into the selected edit color; here it only becomes the draw color.

When the `Dynamic` overlay is enabled, click a colorized frame pixel to select the dynamic set
stored at that pixel instead. This is useful before editing the set's dynamic colors or shadows.

This tool does not modify project data and does not create an undo entry.

### Flood

Click a colorized frame pixel to flood-fill the contiguous four-neighbor area that matches the clicked pixel's current color.

The fill uses the active draw color. The whole filled area is committed as one undoable edit. In
`Set Replace` mode the area is the same and each pixel takes the edit color of its original shade;
with several frames selected the area comes from the current frame and every selected frame is
filled there with its own shades.

TODO: consolidate frame, mask, background, and sprite drawing behind a shared editable-surface
pipeline so tools, previews, flood fill, undo/redo, and deferred dependent-view refresh are
implemented once and reused across canvas targets.

## Selection

The selection is a set of pixels of the plane being edited (the Win32 editor's copy mask). It is
shown as the translucent cyan overlay of `Sel` and is what `Grad Sel`, `Copy`, `Auto Copy...`,
`Black Original To Black` and the dynamic helpers work on. It is editor state, not project data:
it is not saved, it is not part of Undo (the Win32 editor records its changes in its one undo
buffer; here a selection step would sit between the edits of the frame's histories and make
Undo skip over invisible steps, and every selection command is one click to repeat), and it is
dropped when the frame, the SD/HD mode, the paint target or the selected background changes, as
the `Sel` selection always was. The Win32 editor keeps its selection across frames.

Making and changing a selection on the frame canvas (frame pixels or, with `Target` `Background`,
the selected background image; not in the `Mask`, `Dynamic` or `BG` modes):

- `Sel` tool: drag a rectangle, circle or ellipse (the `Shape` of the `Palette` page); it replaces
  the selection.
- `Ctrl`+drag with `Line`, `Rect`, `Fill Rect`, `Circle`, `Fill Circle`, `Ellipse` or
  `Fill Ellipse`: the shape's pixels join the selection. `Ctrl`+`Shift`+drag removes them.
- `Ctrl`+click with `Point`: the pixel joins (with `Shift`: leaves) the selection.
- `Ctrl`+click with `Flood`: the connected pixels of the clicked color join (with `Shift`:
  leave) the selection.
- Hold `W` while selecting, or check `Edit -> Select Non-Black Pixels Only`, and black pixels
  (stored color 0) stay out of what is added, as in the Win32 editor.
- `Ctrl`+`Alt`+click with `Flood`, or `Edit -> Select By Color` and then a click: every pixel of
  the clicked color joins the selection, with `Shift` leaves it. The color is the one the canvas
  shows (with dynamic content, sprites and, when shown, the background layer); the Win32 editor
  compares stored colors and leaves dynamic pixels out.
- `Edit -> Select All` (`Ctrl+A`), `Select None` (`Ctrl+Shift+A` or `Ctrl+D`), `Invert
  Selection` (`Ctrl+Shift+I`).
- `Edit -> Selection From Background Mask` (`Ctrl+B`): the current frame's background mask on
  the plane; the frame needs a background.
- `Edit -> Mirror Selection Horizontally` (`Ctrl+K`) and `Vertically` (`Ctrl+L`).
- `Edit -> Auto Select...`: stripes of rows or columns: from `Offset` on, `Selected` lines are
  selected and `Skipped` lines are not, to the edge of the plane (Win32 limits: offset 0 to 256,
  1 to 256 lines each). It replaces the selection.

A selection edited in any of these ways is a set of pixels, not a shape: `Grad Sel` spreads its
gradient over the selection's bounding box. On macOS `Ctrl` means `Cmd` throughout; the
`Control` key works as well for selecting (a `Control`+click can also be the system's right
click, hence `Cmd`).

## Copy And Paste

`Edit -> Copy` (`Ctrl+C`) copies the selected pixels of the current frame: their colors on the
plane, the original shades of the ROM pixels they show and, on the plane dynamic zones live on
(see "Dynamic Masks"), their dynamic mask values and the colors and shadow of every dynamic set
they use. What is kept is the selection's bounding box. The colors also go to the system
clipboard as a PNG image (transparent outside the selection). The internal clipboard is emptied
when a project is opened.

`Edit -> Paste` (`Ctrl+V`) starts paste mode: the copied pixels float over the frame canvas,
first where they were copied from, then centred on the mouse. In paste mode:

- the arrow keys move them by a pixel, with `Shift` by 10; the keypad's `2`, `4`, `6`, `8` as in
  the Win32 editor;
- `K` mirrors them horizontally, `L` vertically (again to turn back);
- a click or `Enter` pastes them on the current frame or, with several frames selected, on every
  selected frame, and ends paste mode (as in the Win32 editor);
- `Esc` cancels.

What a paste writes, per frame: the colors, the dynamic mask, and the colors and shadow of the
dynamic sets the copied pixels use (Win32 `CopyDynaCol`). Variants:

- `Paste Where Originals Match` (`Ctrl+Shift+V`), or `Shift` held at the click or `Enter`: only
  pixels whose original shade on the target frame equals the copied pixel's.
- `Paste Dynamic Mask Only` (`Ctrl+Alt+V`), or `Alt` held at the click: only the dynamic mask,
  no colors and no set colors. The Win32 editor does not mirror this variant; here it is
  mirrored like the others.

Pixels that land outside the plane are left out. Frames without a picture on the HD plane are
left alone when pasting there. Each changed frame is one undoable step in its own pixel history
(in the `Dynamic` mode its dynamic mask history), see "Undo And Redo".

Content is pasted only on the plane it was copied from (the Win32 editor's `Copy_Content`
check): SD content on the HD plane and HD content on the SD plane are refused with a message
naming the planes, and so is content of a project of another layout. On the HD plane of an
ordinary project nothing dynamic is copied or pasted, since the zones are the SD plane's. `Paste
Dynamic Mask Only` needs the dynamic plane.

Copy and paste work on the frame canvas. The Win32 sprite window's copy (a sprite into the
clipboard) and paste (the clipboard into a sprite) are not built; `Capture` on the `Sprites` page
takes a sprite from a frame.

## Auto Copy And Scrolling Fill

### Auto copy

`Edit -> Auto Copy...` (Win32 `IDC_AUTOCOPY`) copies the current frame's colorization inside the
selection to every frame whose original frame shows exactly what the current frame's shows
there. The selection is turned into ROM frame pixels first (on the doubled HD plane of an
ordinary project a ROM pixel counts when any of its 2x2 pixels is selected). Candidates are
found by a CRC32 over the selected original pixels, as the Win32 editor does, and every
candidate is then compared pixel by pixel, so two different pictures with the same CRC are not
taken.

The dialog chooses `All frames` or `Selected frames only` and, before anything changes, says how
many frames show the same and how many of them would change (a frame that already has those
colors does not change), how many have no picture on the HD plane, and how many had the same CRC
but other pixels. `Apply` writes the colors and, on the dynamic plane, the dynamic mask of the
selected pixels; the dynamic set colors are not copied (Win32 `AutoCopy` copies the frame and the
mask only). One undoable step per changed frame. Unlike the Win32 editor (which asks nothing and
cannot undo it), the count comes first and every frame can be undone.

### Scrolling auto fill

`Edit -> Scrolling Auto Fill...` (Win32 `IDC_SCROLLFILL`, `AutoFillScrolling`) colorizes the
frames of a scrolling between two colorized frames. Select the frames of the scrolling in the
frame list, the current frame among them: the run of selected frames without a gap around the
current frame is used, its first and last frame are the references, and the frames strictly
between them are filled.

For each frame between, the editor looks for the smallest shift at which the frame's original
is the reference's moved up, down, left or right (in that order: "camera moving down" first;
the overlapping whole rows or columns have to be equal). The rows or columns the two share then
get the reference's colors and, on the dynamic plane, its dynamic mask; the uncovered ones stay.
This is done with the first frame, then with the last, which wins where both apply. On the HD
plane of an ordinary project the shift counts twice. Frames without a picture on the plane take
no part. The question before it runs says how many frames scroll from each reference and how
many change; one undoable step per changed frame.

The Win32 editor compares the rows and columns by CRC32; here they are compared exactly. A
single shared row is enough for a match, as in the Win32 editor, so rows that repeat (blank
rows) can match at another shift than the true one.

## Recolor Helpers

- `Recolor`: `Alt`+click with `Flood` (Win32: Alt+click with the fill tool), or `Edit ->
  Recolor...` and then a click: every pixel that has the clicked pixel's stored color, in the
  current frame or in every selected frame, gets the draw color, or in `Set Replace` mode the edit
  color of its original shade. Refused on a dynamic pixel ("works only for static content", as
  in the Win32 editor). One undoable step per changed frame.
- `Black Original To Black` (`Ctrl+Shift+O`; Win32 `Ctrl+O`, which is `Open` here): pixels
  whose original shade is 0 become black on every selected frame, in the selection if there is
  one on the plane, else everywhere. The original pixel is the ROM pixel the plane pixel shows,
  as libserum lays the planes out; the Win32 editor's handling of 256x64 originals is not
  followed.
- `Remove Dynamic Zones` (`Ctrl+E` or `Alt+D`, as in the Win32 editor) and `Assign Dynamic Set`
  (`Alt+A`), see "Dynamic Masks".

## Keyboard Shortcuts

`Ctrl` is `Cmd` and `Alt` is `Option` on macOS.

| Keys | Action | Win32 |
|---|---|---|
| `Ctrl+C` | Copy the selection | same |
| `Ctrl+V` | Paste | same |
| `Ctrl+Shift+V` | Paste where the originals match | `Shift`+click in paste mode |
| `Ctrl+Alt+V` | Paste the dynamic mask only | `Alt`+click in paste mode |
| Arrows, `Shift`+arrows, keypad `2 4 6 8` | Move the paste (1 or 10 pixels) | keypad |
| `K`, `L` | Mirror the paste | held when paste starts |
| Click, `Enter` | Stamp the paste (`Shift`, `Alt`: variants) | same |
| `Esc` | Cancel paste mode or a pending `Select By Color` / `Recolor` click | - |
| `Ctrl+A` | Select all pixels | same |
| `Ctrl+Shift+A`, `Ctrl+D` | Select none | `Ctrl+D` |
| `Ctrl+Shift+I` | Invert the selection | button |
| `Ctrl+B` | Selection from the background mask | same |
| `Ctrl+K`, `Ctrl+L` | Mirror the selection | same |
| `Ctrl`(+`Shift`)+click or drag | Add to (remove from) the selection | same |
| hold `W` | Select non-black pixels only | same |
| `Ctrl+Alt`+click, fill tool | Select by color | same |
| `Alt`+click, fill tool | Recolor every pixel of the color | same |
| `Ctrl+Shift+O` | Black original to black | `Ctrl+O` |
| `Ctrl+E`, `Alt+D` | Remove dynamic zones | same |
| `Alt+A` | Assign the dynamic set | same |

With a text field, a spin box or a list in focus, `Ctrl+C`, `Ctrl+V` and `Ctrl+A` do what they do
there (`Ctrl+A` in the frame list selects every frame). A single letter cannot be a menu
shortcut (a native macOS menu bar takes it from every widget), so `Invert Selection` is not `I`;
`K`, `L` and `W` act only while the frame canvas has the focus. Other shortcuts of the editor:
the platform's keys for Open (`Ctrl+O`), Save, Save As, Quit, Undo and Redo, `Ctrl+R` Runtime
view, `Ctrl+3` / `Ctrl+6` its planes, `Delete` / `Backspace` delete frames in the frame list,
and the edit color keys on the canvas (see "Keys for the edit colors"). No two actions share a
key (`tools/ui_dump` prints every shortcut of the window and the shared ones).

## Undo And Redo

Every edited object has its own undo/redo history. Undo and Redo act on the histories that belong
to the current editing context; a new edit clears redo only in the history it is recorded in.

Histories:

- Colorized frame pixels: per frame and per SD/HD resolution.
- Frame settings: per frame, for both resolutions together. This history holds the comparison mask
  ID, shape mode, background ID, trigger ID (including `Switch mono`), and duration edits of the
  frame, and its `Create HD` and `Remove HD`.
- Comparison masks: per mask. Frames that share a mask share its history.
- Dynamic masks, dynamic colors, and dynamic shadows: three separate histories per frame. They
  belong to the plane the dynamic data is edited on, which is one per project (see "Dynamic
  Masks"): an ordinary project has no histories for HD dynamic data, which cannot be edited.
- Background masks: per frame and per SD/HD resolution.
- Background images: per background and per SD/HD resolution.
- Sprite color images: per sprite and per SD/HD resolution.
- Sprite HD: per sprite, for both resolutions together. It holds `Create HD Sprite` and
  `Remove HD Sprite`.
- Palette colors: per palette.
- Records: one history for the project. It holds the deletion of frames and the addition and
  deletion of sprites, backgrounds and comparison masks.
- Edit colors and color sets: one history for the project. It holds edit color changes (picker,
  dropped colors, `Dyn > Edit`) and `Store`, `Load`, renaming and `Deactivate` of color sets.
  Each step restores the edit colors and the color sets as a whole, so Undo puts back every byte.

Which histories Undo and Redo use, by context. The record history belongs to every one of
them:

1. `Edit Sprite` active with a sprite selected: the selected sprite's color image history for the
   active resolution (its SD history when the sprite has no HD data), its sprite HD history and
   the edit color history.
2. `Mask` active: the comparison mask assigned to the current frame and the current frame's
   settings.
3. `Dynamic` active: the current frame's dynamic-mask, dynamic-color, and dynamic-shadow
   histories and the edit color history.
4. `BG` active: the current frame's background-mask history for the active resolution and the
   current frame's settings.
5. Otherwise: the current frame's pixel history for the active resolution, the current frame's
   settings, the selected palette, the current frame's dynamic-color and dynamic-shadow histories
   (the same ones in SD and in HD mode), the image history of the selected background for the
   active resolution and the edit color history. This is the same whether Inspector `Target` is `Frame` or `Background`, so a
   `Frame -> BG` copy or a background image import can be undone without switching the target.

Within a context, Undo reverts the edit that was made most recently among all of the context's
histories, whichever history it is in. For example, after a frame pixel edit followed by a palette
edit, Undo reverts the palette edit first and the pixel edit second. Redo reapplies the edit that
was undone most recently, so Undo followed by Redo changes nothing and repeated Redo replays the
edits in the order in which they were originally made. An edit that has just been redone counts as
the most recent edit again.

Only the histories of the current context take part. Edits made in other contexts, on other frames,
or in the other SD/HD mode stay in their own histories and are skipped; switch to that context to
undo them. Switching frame, SD/HD mode, edit mode, or selected background or palette updates
whether Undo and Redo are available.

An edit applied to several selected frames is recorded once per changed frame, in that frame's own
history. Undo therefore reverts it only on the current frame; select another frame to undo it
there. This applies to drawing (also in `Set Replace` mode), to dynamic shadow direction and color
changes, to `Edit > Dyn`, and to all frame settings.

Paste, `Auto Copy...`, `Scrolling Auto Fill...`, `Recolor`, `Black Original To Black`, `Remove
Dynamic Zones` and `Assign Dynamic Set` are recorded the same way, one step per changed frame,
even when a step changes the frame's colors, its dynamic mask and its dynamic set colors
together. The step goes into the frame's pixel history of the plane it was made on, or, when it
was made in the `Dynamic` mode, into the frame's dynamic mask history, so that it is undone in
the context it was made in. Auto copy and the scrolling fill change other frames than the current
one: their steps are undone on those frames. The selection itself is not part of Undo (see
"Selection").

Undoing `Create HD` removes the HD frame again and restores the HD data the dialog options had
overwritten: the frame's HD background mask and the HD image of the background that was assigned
at the time (or its absence). Undoing `Remove HD`
restores the HD pixels that were removed. Undo and Redo of these two switch the canvas back to SD
when the current frame is left without HD data; Redo of `Create HD` does not switch to HD by
itself. The same holds for `Create HD Sprite` and `Remove HD Sprite`, which restore the sprite's HD
mask and HD colors.

Histories are dropped when the data they refer to is replaced as a whole:

- Opening a project drops all histories.
- Adding and deleting records drops the histories that name a renumbered record, see below.
- `Remove HD` drops the frame's HD pixel history.
- `Create HD` drops the frame's HD pixel history. Each related item that is upscaled in the dialog
  also drops its HD history: the frame's background mask and the image of the assigned
  background. Dynamic histories are not touched.
- `Create HD Sprite` and `Remove HD Sprite` drop the sprite's HD color image history.
- Undoing or redoing any of these four drops the same histories again, because the HD data is
  replaced as a whole in both directions. Pixel edits made on HD data in between can therefore not
  be undone separately afterwards.
- Sprite `Capture` drops the sprite's SD and HD color image histories. The capture itself is one
  undoable step (see "Sprites" below).

All other histories stay available. Importing an image into a background and copying a frame to a
background are ordinary undoable edits and do not drop any history.

Adding and deleting records:

- Undo puts back exactly what was there: the records with their content, their numbers, the
  references of the frames (sprite slots at their old places, background and mask assignments),
  the sections, the sprite tables and mask names of the `.cRP`, and the imported palettes. Undo
  of an addition removes the record again and takes it away from the frames that were given it
  in between; Redo brings it back with those assignments and with what was edited on it.
- Redo of a deletion deletes the records as they are then, so edits made to them after the Undo
  are not lost if the deletion is undone again.
- These operations are undone in the reverse order in which they were made, whatever the
  context: they are all in the one record history.
- Adding the frames of a dump is one such operation: Undo removes exactly the added frames,
  Redo appends them again with what was edited on them in between. Deleting same frames is a
  deletion of frames like any other.
- Other histories refer to records by number. Whenever one of these operations runs (when it is
  made, undone or redone), every history that names a record whose number changes or that
  appears or disappears is dropped, so that no edit can ever be undone on another record than
  the one it was made on:
  - frames: the histories of the deleted frames and of all frames behind the first deleted one
    (pixels, settings, dynamic mask, colors and shadows, background mask). Histories of frames
    before it stay;
  - a sprite: the color image and HD histories of that sprite and of all sprites behind it;
  - a background: the image histories of that background and of all behind it, and every
    frame's settings history that holds a background assignment to one of them or a `Create HD`
    that upscaled one of them;
  - a comparison mask: the histories of that mask and of all behind it, and every frame's
    settings history that holds an assignment of one of them;
  - the settings history of each frame that lost its mask or background through the deletion.
  A history dropped by a deletion stays dropped when the deletion is undone. An edit made
  between an operation and its Undo on a record with a changed number can therefore not be
  undone by itself afterwards; the edit itself stays.
- A deleted record is kept by the history until the history is dropped (a project is opened) or
  the step can no longer be redone. Nothing is copied for that; the memory of deleted frames is
  simply not released before. A deleted frame holds what it held in the project (its original
  and colorized pictures and whatever of its other parts was edited), typically 12 to 45 KB.
- The add and delete steps keep at most 1 GB of deleted records. When a step goes beyond
  that, the oldest add and delete steps are dropped (never the one just made, however large):
  they can no longer be undone, and the status bar says so after the result of the step, for
  example `Deleted 3000 frame(s), 2 remain (undo keeps at most 1024 MB of deleted records: the 1
  oldest add or delete steps can no longer be undone)`. The steps that remain undo exactly as
  before. The other histories keep only the changes and are not limited.

When an edit cannot be undone or redone because it no longer fits the project (it throws), the
project is left as it was, the step stays where it is in its history, and the status bar says
`Undo failed, the step stays in the history: <reason>`. In particular a deletion whose Undo fails
stays undoable: it is not dropped, so it never becomes permanent that way.

Undoable besides: sprite slot assignment, removal and bounds and color rotation settings (in the
history of each frame they change, one step per frame), sprite capture (one step in the history
of the frame it was made on: the sprite's picture, detection area `0`, source frame and the
frame's slot together), sprite detection areas and sprite names (in the selected sprite's
history, which Undo reaches while that sprite is selected).

Not undoable: palette names and the draw color mode (a setting, see "Draw color modes").
The rotation `Length` and `Delay` boxes count a typed value as one change when it is confirmed
(Enter or leaving the box); each arrow click is a change of its own.

## Comparison Masks

The `Mask` toolbar toggle shows the assigned comparison mask as a semi-transparent red overlay on the original/reference region.

A comparison mask has one pixel per original pixel. In a project with 256x64 original frames
the masks are 256x64, and the overlay, the tools, flood fill, erasing and undo all work on those
256x64 pixels of the original/reference region. In a project with 192x64 original frames the
same holds for its 192x64 masks.

The Inspector exposes per-frame comparison metadata:

- The `Mask` topic shows the visually selected mask from the left sidebar as `Selected: <ID>`, with
  `(assigned)` when it is the mask of the current frame. Selecting a mask in the sidebar only
  selects it, in the same way as selecting a background or a sprite does; no frame is changed. The
  selection starts as the mask of the first frame and then stays until another mask is selected.
- `Assign Mask` assigns the selected mask to all selected frames.
- The picture of a mask in the left sidebar follows every edit of that mask, its Undo and its
  Redo (redrawn once when the editor is idle again, not per pixel).
- `Edit Mask` enables the mask overlay and returns the canvas to the frame preview. If the sprite
  editor is open, it is closed first (`Edit Sprite` is switched off). It edits the mask assigned to
  the current frame, which is not necessarily the mask selected in the sidebar.
- `Mask ID` shows the comparison mask assigned to the current frame and assigns the entered mask
  directly to all selected frames. `None` stores the legacy no-mask assignment. Its range is the
  masks the project has; a mask number that does not exist is refused, also by the project
  itself (`comparison mask 5 does not exist; the project has 2`). The same holds for the
  `Background` ID and backgrounds. A loaded frame whose ID names no mask or background keeps it
  and is marked `ref. problem` (see "Frame List"); the field then shows `None`, which is what the
  runtime does with it.
- `Shape` selects `Full comparison` or `Shape mode` for all selected frames.
- Both fields are saved back into `.cROM`.
- Mask ID and shape mode edits can be undone, see "Undo And Redo".
- `New` and `Delete` above the mask list of the left sidebar add an empty mask and delete the
  selected one, see "Adding And Deleting Records". `Mask ID` offers the masks the project has.
- Below these controls the page holds the comparison tools: which frames are the same as the
  current one under its mask, the pixels that differ among the selected frames and a mask made
  of them, and the frames that use a mask. See "Same Frames And Common Pixels". The page
  scrolls if the window is too low for all of it.

Editing rules:

- Mask editing is active only while the `Mask` toggle is on.
- Colorized-frame drawing is paused while the `Mask` toggle is on.
- Click the original/reference region to set that mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill also edit the assigned mask from the original/reference region.
- Shape tools show a red live preview while drawing on the mask.
- Hold `Shift` while clicking, drawing, or flood filling the original/reference region to erase mask pixels back to `0`.
- Shift-erase shape tools show a dark live preview.
- Edits apply to the assigned comparison mask, not just the current frame.
- If the current frame has no valid comparison mask assignment, clicking the original/reference region does nothing and shows a short status message.
- Mask pixels are saved back into `.cROM`.

## Same Frames And Common Pixels

The lower part of the Inspector's `Mask` page. It answers what a comparison mask is drawn for:
which frames the runtime cannot tell apart.

Two frames are "the same" under a comparison mask and a shape mode if their originals have the
same hash under them: without the pixels the mask covers, and in shape mode with every shade
above 1 taken as 1. That is the hash libserum identifies frames by, so "the same" means exactly
"the runtime identifies one as the other". (The Win32 editor compares the pixels instead; the
two differ only if two different pictures have the same CRC.) Checked on `afm_113b` against
libserum with a colorization of one frame alone: 45 combinations of frame, mask and shape mode,
the same list of frames each time.

- `Same frames: N` is the number of frames that are the same as the current frame under its own
  mask and shape mode. It follows the frame, its `Mask ID` and `Shape`, and every stroke on its
  mask. The box beside it lists them; choosing one goes to that frame. From 10000 on the label
  reads `>=10000` and the box lists the first 10000, as in the Win32 editor.
- `Update Same List` counts the same frames of the current frame for no mask and for every mask
  of the project, each as `full comparison / shape N`. It helps to choose a mask. The list is
  emptied when the frame or anything the comparison depends on changes.
- `Delete Same As Current...` deletes the frames that are the same as the current frame.
- `Delete Same As Selected...` does that for every selected frame, each under its own mask and
  shape mode.
- `Delete All Duplicates...` does it for every frame of the project, from the first frame on.
- `Keep selected frames`: none of the three deletes a selected frame.

The three deletions work as in the Win32 editor: the frames are taken in ascending order, each
one that is still there removes every other frame that is the same as it, and a frame that was
removed removes nothing. "The same" is not symmetric when frames have different masks, so a
frame can remove an earlier one. At least one frame always remains. Each deletion asks first,
naming the frames, and is one step for `Undo`, like every deletion of frames (see "Adding And
Deleting Records"). Unlike the Win32 editor, a deletion is not limited to the first 10000 same
frames of a frame.

- `Spot Common` marks, in yellow on the original/reference region, the pixels in which the
  originals of the selected frames differ. With fewer than two frames selected nothing is
  marked. The marking follows the selection. (The Win32 editor tints the other pixels, the
  common ones.)
- `Common To Mask` makes the comparison mask of the current frame cover exactly those pixels,
  replacing what the mask held. It needs at least two selected frames and a mask assigned to
  the current frame; it does not need `Spot Common` to be on. The `Mask` toggle is switched on,
  and the step is undone with `Undo` like a stroke on the mask. The mask is shared: every frame
  that uses it is compared with it, and the status bar says how many do.

  A typical use: select the frames that show the same scene with a changing score, add a mask
  (`New` on the `Masks` page), assign it to them with `Mask ID`, press `Common To Mask`. `Same
  frames` then counts them, and `Delete Same As Current...` leaves one of them.

- `Frames using` lists the frames that have the chosen mask assigned, or no mask; the entries
  show how many there are. A click on a frame goes to it. `Select Them` selects them in the
  frame list.

A frame that the frame filter hides cannot be gone to or selected; the status bar says so.

The same frames are worked out only while the `Mask` page is shown. The first question about a
mask reads every original once (a fraction of a second for a few thousand frames); after that
the answers are kept until an original, a mask or an assignment changes. `Update Same List`
does that for every mask and can take seconds in a large project with many masks.

## Dynamic Masks

The `Dynamic` toolbar toggle shows the current frame's dynamic-mask pixels as a semi-transparent cyan overlay.
Every dynamic mask edit, Undo and Redo also redraws the frame's thumbnail and its `Dyn` flag in
the frame list.

**Dynamic zones belong to one plane: the one with the size of the original frame.** That is the
128x32 (SD) plane in an ordinary project, the 256x64 plane in a project with 256x64 original
frames and the 192x64 plane in one with 192x64 original frames. It is the plane the runtime
takes them from. In an ordinary project libserum renders the 64-row picture in layers: the HD
frame and HD background as they are, and the dynamic content at 128x32, with the SD dynamic
mask and the SD dynamic colors, upscaled and put on top. It does not read the HD dynamic mask
and the HD dynamic colors at all (libserum, `Colorize_Framev2`: with a 64-row request a 128x32
colorization always runs in "layer mode", where `frameHasDynamicExtra` is forced false;
libserum's AGENTS.md, "Resolution scaling and the two-layer renderer": "HD dynamic masks are
ignored outright").

So in an ordinary project:

- Dynamic zones are shown and painted in SD mode. In HD mode the `Dynamic` button is disabled and
  its tool tip says `Dynamic zones are defined on the 128x32 plane: switch to SD to see and paint
  them`. Switching to HD while `Dynamic` is on switches it off, with a message in the status bar.
  The zones are not drawn over the HD frame: at twice the size they would look like something
  that can be painted there, and the HD picture on the canvas already shows what they do.
- The `Dyn` page works in both modes and always shows and edits the SD plane's dynamic colors
  and shadows, which are the ones both pictures use. In HD mode a note at the top of the page
  says so. An edit made there in HD mode shows in the HD picture at once and is undone from
  either mode.
- The editing canvas and `Frame -> BG` in HD mode render the HD plane as libserum does:
  dynamic pixels have the SD set's color for the original's shade, every SD pixel doubled.
  libserum rounds the edges of that upscale (Scale2x); the Runtime view with
  `Runtime Plane: 64 Rows` shows the exact picture.
- HD dynamic masks, colors and shadows a project already holds are kept: they are loaded,
  written back unchanged and never shown or edited. The Win32 editor writes them, as a
  line-doubled copy of the SD ones.
- One thing of the stored HD data does reach the picture: **shadows**. A dynamic set that has no
  shadow direction on the SD plane takes its direction and color from the HD shadow tables when
  libserum renders the 64-row plane (they are settings per set, not pictures). The editor's HD
  rendering does the same. The `Dyn` page shows and edits the SD shadow only: a shadow that
  comes from the HD tables can be replaced by setting an SD direction for the set, and cannot be
  switched off in this editor.
- The save rule that writes colorized pixels under a dynamic mask as 0 still uses the stored HD
  mask for the HD frame, see "Saving".

Helpers of the `Edit` menu, on every selected frame and on the dynamic plane only (in HD mode of
an ordinary project they are refused with a message), in the selection if there is one on the
plane and otherwise on the whole frame (the Win32 editor always takes the whole frame):

- `Remove Dynamic Zones` (`Ctrl+E`, `Alt+D`): the pixels leave their zones (mask `255`) and get
  the plain color of their original shade, as in the Win32 editor.
- `Assign Dynamic Set` (`Alt+A`): the pixels join the `Dyn Set` of the `Dynamic` page.
- Copy and paste carry the dynamic mask and the colors and shadow of the sets used; `Paste
  Dynamic Mask Only` pastes the mask alone; auto copy and the scrolling fill copy the mask with
  the colors. See "Copy And Paste" and "Auto Copy And Scrolling Fill".

Rules:

- The `Dyn Set` field in the Inspector's `Dynamic` topic selects the dynamic set written by drawing tools.
- The Inspector shows editable dynamic color swatches for the active frame and the selected dynamic set.
- Click a dynamic color swatch to choose a replacement RGB565 color for that original color index.
- The dynamic mask is rendered over the colorized frame of its plane.
- `Mask` and `Dynamic` are mutually exclusive overlay modes.
- Click the colorized frame to set that dynamic-mask pixel to the selected `Dyn Set`.
- With the `Pick` tool, click the colorized frame to select the dynamic set stored under the cursor.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the dynamic mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase dynamic-mask pixels back to `255`.
- Dynamic-mask edits are undoable per frame.
- Dynamic color changes are undoable per frame.
- Dynamic shadow direction and color changes apply to all selected frames that have dynamic data
  and are undoable per frame.
- `Edit > Dyn` under the toolbar copies the edit colors into the selected `Dyn Set` of all selected
  frames that have dynamic data, one undoable step per frame; `Dyn > Edit` copies the current
  frame's set into the edit colors (see "Edit colors and dynamic sets" under "Colors").
- The color swatches and `Shadow Color` open the editor's color picker (see "Color picker").

Dynamic shadows are loaded from and saved back to the legacy `.cROM` dynamic-shadow blocks. The
canvas and thumbnails render shadows after dynamic-color replacement and before sprite
composition. Shadow pixels are written only into neighboring pixels that are not themselves dynamic,
matching the legacy renderer behavior. On the HD plane of an ordinary project a shadow is one HD
pixel wide around the doubled zone, as libserum generates it there.

The Inspector's `Dynamic` topic provides shadow controls for the active dynamic set:

- Direction buttons `TL`, `T`, `TR`, `L`, `R`, `BL`, `B`, and `BR` toggle where shadows are cast.
- `Shadow Color` opens a color picker for the active set's shadow color.
- Shadow edits apply to all selected frames that have dynamic data.

## Color Rotations

Modern `.cROM` projects store four color-rotation slots per frame and per SD/HD resolution. Each
slot contains a length, a delay in milliseconds, and up to 62 RGB565 colors.

The Inspector's `Rotation` topic edits the active frame's rotation slots for the current SD/HD mode:

- `Slot` selects one of the four rotation slots.
- `Length` sets the number of active colors in that slot. A length of `0` disables the slot.
- `Delay` sets the milliseconds between rotation steps.
- The color grid shows all colors in the selected slot at once. Active colors can be clicked to
  open a color picker; colors outside the active length remain visible but disabled.
- `Clear Slot` resets the selected slot to all zeros.

Length, delay, grid color edits, and clear operations apply to all selected frames. Rotation data is
loaded from and saved back to `.cROM`. The static canvas shows unrotated frame data; playback applies
an editor-side rotation preview.

Which pixels a rotation moves, at runtime and in the preview:

- A background, static or sprite pixel whose color is listed in a slot takes the color that many
  places further in the slot at every step. A color listed more than once belongs to the first
  slot, and the first place in it, that lists it.
- **Dynamic content does not rotate**: a pixel colored from a dynamic zone's color set keeps its
  color even if a rotation lists that color. White score digits over a background whose stars
  rotate through white stay white.
- **A dynamic shadow does not rotate** either, whatever it covers and whatever its color is.
- The dynamic pixels of a sprite do rotate, like the rest of the sprite. That is what libserum
  does (`Colorize_Spritev2`).

The editor's preview follows these rules pixel for pixel; an automated test steps libserum's
rotation and compares (see "Testing Checklist"). Its timing is the editor's own: every slot
steps at its delay from the moment playback starts, while libserum keeps a clock per slot that
carries on from frame to frame.

## Palettes

Legacy `.cRP` files can store 256 palettes with 64 RGB565 colors each, plus 64-byte palette-name
slots. When that table exists, the Qt rewrite loads it into the document model and saves edits back
to the same `.cRP` offsets.

The Inspector's `Palette` topic provides a palette selector, the selected palette name, and a
64-color grid:

- Edit the name field to rename the selected palette. Names are capped at 63 characters so they fit
  the fixed cRP name slot.
- Click any palette color to make it the active drawing color.
- Drag any palette color onto one of the four toolbar favorite color slots to replace that favorite,
  or onto an edit color swatch under the toolbar to set that edit color (undoable).
- Double-click any palette color to open a color picker for that RGB565 palette entry.
- Palette color cells and `From`/`To` controls are displayed as `1` through `64`.
- Use `From`, `To`, and `Apply Gradient` to fill a palette range by interpolating between the
  endpoint colors. Reversing `From` and `To` reverses the generated palette range. The red, green,
  and blue RGB565 components are interpolated separately and rounded to the nearest value; the two
  endpoint colors themselves are never changed.
- Use `Shape`, `Pattern`, and `Direction` to control the canvas `Grad` drawing tool.
- Palette color changes and gradient fills are undoable per palette.
- Palette color/name changes mark the project dirty and are saved with the `.cRP` file.

The current frame renderer does not yet use these palettes for color-rotation playback or gradient
fill previews outside the Palette editor. Palette editing is the storage/UI foundation for those
next steps.

## Sprites

Sprite data is loaded into the document model and saved back through the legacy `.cROM` layout.
`New` and `Delete` above the sprite list of the left sidebar add an empty sprite and delete the
selected one, see "Adding And Deleting Records".

Currently preserved sprite data includes:

- Sprite names from the `.cRP` sidecar, when available.
- Sprite original masks.
- SD and HD sprite color images.
- HD sprite masks.
- Sprite detection dwords, positions, and areas.
- Per-frame sprite assignments and bounding boxes.

The Inspector has a sprite browser and assignment controls:

- The sprite selector lists loaded sprite records and shows `.cRP` sprite names when available.
- The sprite name field edits the selected sprite name. Names are saved back to the fixed-size
  `.cRP` sprite-name table when that table exists.
- `Show Det Areas` overlays valid per-sprite detection areas on the original sprite in the sprite
  editor.
- The detection area selector chooses one of the sprite's detection areas. The `X`, `Y`, `W`, `H`
  fields edit an area that is set; they are disabled while the selected area is not set. A new
  area is created by dragging it in the sprite editor (see below).
- `Clear Area` disables the selected sprite detection area using the legacy `0xffff` marker.
- `Edit Sprite` switches the main canvas to a large sprite editor with colorized sprite on top and original sprite below.
- While `Edit Sprite` is active, `Create HD Sprite` creates HD sprite mask/color data by scaling the SD sprite data to 2x, and `Remove HD Sprite` clears the sprite HD-present flag. Both can be undone while the sprite editor is active; Undo restores the sprite's previous HD mask and HD colors.
- In the sprite editor canvas, drag on the colorized sprite to repaint existing non-black sprite pixels with the active draw color.
- Hold `Shift` while dragging on the colorized sprite to erase existing non-black sprite pixels back to black.
- The sprite editor supports the toolbar point, line, rectangle, filled rectangle, circle, filled circle, ellipse, and filled ellipse tools on the colorized sprite.
- The sprite editor also supports the toolbar pick and flood-fill tools on the colorized sprite.
- Sprite shape tools show a live preview and commit the full stroke on mouse release.
- Sprite pixel edits update the sprite editor immediately. Frame thumbnails and the composed frame
  preview refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Undo and redo for sprite color edits are scoped per sprite and per active SD/HD resolution, and
  are available while the sprite editor canvas is active.
- Drag on the lower original sprite canvas to set the selected detection area.
  Detection-area edits update the sprite editor immediately. Frame thumbnails and composed frame
  previews refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Sprite source/original pixels are not freehand editable.
- `Source Frame` goes to the frame the sprite was taken from, as CDMD's "to source frame": the
  frame it was added or last captured on (the `.cRP` keeps it; deleting frames moves it along).
  Its tool tip names the frame.
- `Capture` copies original and colorized pixels from the current frame into a sprite, using the
  selected sprite slot bounds as the source rectangle. Captured pixels are placed at the sprite
  origin, black original pixels become transparent sprite pixels, detection area `0` is set to the
  captured rectangle, and the selected sprite is assigned to the current frame slot. The `.cRP`
  records the current frame as the sprite's source frame, as CDMD does.
- The confirmation offers `Replace Current` (the selected sprite) and `Create New`. `Create New`
  asks for the new sprite's name as `New` does and captures into the new sprite; adding the
  sprite and the capture are two undo steps. Without a selected sprite only `Create New` is
  offered. As in CDMD, a capture keeps the sprite's dynamic mask and colors.
  Because the bounds of an empty slot cannot be edited, capturing a rectangle into an empty slot
  takes `Assign` first, then the bounds, then `Capture`; or use `Select Capture`, which needs no
  bounds.
- `Select Capture` enables a persistent shaped capture selection on the original frame. Use the
  normal point, line, rectangle, circle, ellipse, and filled-shape tools on the original frame to
  add selected pixels; hold `Shift` to erase selected pixels. `Clear Sel` removes the selection.
- When a capture selection exists, `Capture` uses the selected pixels instead of the slot rectangle.
  The selected pixels are copied into the sprite relative to the selection bounds, unselected pixels
  inside those bounds remain transparent, and selected black original pixels remain real sprite
  pixels.
- `Capture` replaces the selected sprite record after confirmation. To capture into a new sprite,
  add one with `New` on the `Sprites` page first and capture into it. The dialog's `Create New`
  button is still disabled.
- Rectangle capture without a capture selection still treats black original pixels as transparent.
- The current frame's sprite assignment slots and bounding boxes are shown as text.
- A frame's sprite slots never have an empty slot before a used one: libserum stops looking for
  sprites at the first empty slot, so the sprites behind it would be ignored. Every edit keeps the
  list closed up:
  - `Assign` writes the selected sprite into the selected slot for all selected frames. Where
    that slot of a frame is empty, the sprite goes to the frame's first empty slot instead (the
    slot itself or one before it), with that slot's bounds; the status bar says on how many
    frames that happened, and the current frame's slot selection follows.
  - `Capture` assigns the captured sprite by the same rule.
  - `Remove` works as in the Win32 editor (`IDC_DELSPRITE2`): it takes the sprite of the current
    frame's selected slot and removes it from every selected frame that has it, from the last
    slot that holds it; the slots behind move up with their bounds, and the last slot becomes
    empty, keeping its bounds. Selected frames without that sprite are skipped and counted in the
    status bar (`Removed sprite 2 from 3 frame(s), 1 skipped (without that sprite)`).
  - A project loaded from a file can have such a gap (the Win32 editor before `Remove` closed up
    could leave one; none of the five 3.17.3 test projects has one). It is kept as it is, and the
    frame is marked: its list item has the line `ref. problem` and its tool tip and the frame
    status line's tool tip say `Sprite slot N is empty: the sprites after slot N are ignored by
    the runtime`. Removing a sprite in front of the gap or assigning one into the gap closes it.
- `Min X`, `Min Y`, `Max X`, and `Max Y` edit the selected slot's bounding box for all selected
  frames in which that slot has a sprite. They are disabled while the slot of the current frame is
  empty. A minimum cannot be raised above its maximum and a maximum cannot be lowered below its
  minimum; the edited value stops at the other one.
- `Show Bounds` toggles a translucent filled overlay for the selected slot's bounding box on the original frame. It is disabled by default.
  The overlay also shows the bounds an empty slot holds.
- `Edit Sprite` and `Select Capture` are switched off when a project is opened. `Edit Mask` and
  `Edit BG` switch `Edit Sprite` off, since they return to the frame view.
- Canvas, thumbnails, and PNG export compose detected frame sprites over the resolved frame preview.
  Sprites are looked for and drawn by libserum's rules:
  - the sprite list of a frame ends at its first empty slot, so a sprite in a slot behind an
    empty one is not looked for;
  - a sprite assigned to two slots is looked for once, in the bounds of its first slot;
  - a detection area may end on `Max X` and `Max Y`: a sprite that fills its bounds is found;
  - a sprite in shape mode is compared by lit and unlit pixels, whatever the shades are;
  - a dynamic sprite pixel takes the color its dynamic set gives the original frame's shade
    at that place.

In projects with 256x64 original frames, sprites are detected, bounded, captured and drawn on
the 256x64 original, `Create HD Sprite` is `Create 256x64 Sprite` and there is no
`Remove HD Sprite`; see "Projects With 256x64 Original Frames". In projects with 192x64 original
frames all of this happens on the 192x64 original and there are no HD sprites; see "Projects
With 192x64 Original
Frames".

Sprite-name edits are limited to 31 characters so the legacy fixed 32-byte `.cRP` name slot
remains null-terminated.

## Backgrounds

The app loads and saves background data from modern `.cROM` projects.

Currently implemented:

- Shared SD and HD background image data is preserved.
- Per-frame background IDs are preserved.
- Per-frame SD and HD background masks are preserved.
- The project status line shows the loaded background count.
- `BG ID` assigns the shared background for all selected frames that have background data. `None` stores the legacy no-background assignment.
- `Show BG` renders the assigned background layer over original black pixels where the active SD/HD background mask allows it.
  The original pixel looked at is the one the plane pixel belongs to: (x, y) in SD, (x/2, y/2) in
  HD, and (x, y) on the single plane of a project with 256x64 or 192x64 original frames.
- Inspector `Target -> Background` displays the selected background image on the canvas and edits that shared background image with the ordinary drawing tools.
- The `BG` toolbar toggle shows the current frame's background mask as a semi-transparent yellow overlay on the colorized frame.
- `BG` and `Dynamic` are mutually exclusive edit modes because both write on the colorized frame.
- In SD mode, `BG` edits the SD background mask.
- In HD mode, `BG` edits the HD background mask.
- Click the colorized frame to set that background-mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD background mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase background-mask pixels back to `0`.
- Background-mask edits are undoable per frame and per resolution.

`New` and `Delete` above the background list of the left sidebar add a background with the picture
of the current frame and delete the selected one, see "Adding And Deleting Records".
Background import/copy operations replace the image of an existing background.

## Missing Editing Features

Known feature gaps that affect first-release completeness:

- Palette import/export and reusable gradient definitions.
- Directional palette-gradient fills for masks, sprites, and dynamic masks.
- Filling with palette gradients while respecting outline-only tool shapes.
- Saved (named) selections.
- Copy and paste on the sprite canvas (the Win32 sprite window's copy and paste).

## Preview Rendering

libserum is the source of truth for every runtime-composed pixel. There are two renderers:

- The **Runtime view**, runtime playback and `Export Current Frame PNG...` show libserum's own
  output, see "Runtime View".
- The **editing canvas and the thumbnails** use the editor's own renderer, which draws the
  layers being edited and has to stay cheap. It follows libserum's
  rules and is checked against libserum by automated tests on constructed projects of all three
  layouts and by `colorizing_qt_v2_preview_diff` on real projects (see "Testing Checklist").

The rules the editor's renderer follows, in libserum's order:

- Frame pixels are resolved from background, static and dynamic data in one scan. The
  background shows where its mask is set and the original is 0; a dynamic zone takes its set's
  color for the original's shade.
- A lit dynamic pixel casts its set's shadow onto its neighbors in the set's directions. A
  shadow never covers lit dynamic content, and the first shadow on a pixel stays.
- Sprites are looked for on the original frame and drawn after the frame and its shadows, in
  the order they were found. The rules are listed under "Sprites".
- In a project with 256x64 original frames, a frame without its 256x64 data is black, and
  libserum's rule for the 256x64 dynamic data of sprites applies: sprite number N keeps it only
  if frame number N has 256x64 data.

On the plane that has the size of the original frame (128x32 in an ordinary project, 256x64 and
192x64 in the other two layouts) the editor's renderer and libserum give the same pixels, with
one intended exception: in a project with 256x64 original frames, a frame whose background or
sprite lacks its 256x64 data is still drawn without it, so that it can be edited, while libserum
shows nothing for the frame. Last
measured: `afm_113b` 3047 of 3047 frames, `trn_174h` 5211 of 5211, `hook_501` 3897 of 3897,
`smb3` 4430 of 4430, `frankst1` 303 of 303 sampled frames.

On the HD plane of an ordinary project the editor's renderer follows libserum's layers: the HD
frame and HD background as they are, and on top the dynamic zones of the SD plane with the SD
colors (the stored HD dynamic mask and colors are not used, see "Dynamic Masks"), an unlit zone
pixel under the SD background mask left to the HD picture, and shadows generated on the HD plane,
one pixel wide, from the SD shadow tables with the HD tables as a fallback per set. An automated
test compares the whole HD plane with libserum for a frame with HD statics, zones, shadows and a
background, built of rows so that libserum's upscale has nothing to round, and checks that other
stored HD dynamic masks and colors change nothing.

Known differences, all on the HD plane of an ordinary project. libserum upscales what it renders
at 128x32 once (Scale2x) before putting it on top. The editor doubles pixels instead.

- **Edges of dynamic zones.** Scale2x rounds corners: a pixel at the edge of a glyph gets the
  color of a neighboring 128x32 pixel, or is left to the HD frame below. This is all that is left
  of the dynamic differences: in `trn_174h` 113940 pixels in 4222 of 5211 frames, 27 per frame,
  every one of them such an edge pixel (before the editor used the SD zones: 151384). Shadows
  follow the rounded shape in libserum and the doubled one in the editor.
- **Edges of sprites and backgrounds.** libserum reads the original frame upscaled where it
  decides between background and frame, and rounds the edges of upscaled sprites. The editor
  doubles pixels: a sprite without HD is its 128x32 picture with every pixel doubled, and an HD
  sprite keeps the doubled 128x32 pixels where the sprite is dynamic. Single pixels differ along
  such edges, for example `trn_174h` frames 1455 and 697 (782 pixels in all).
- **Frames without HD** have no HD plane in the editor at all. libserum's upscaled 64-row
  picture of them is only in the Runtime view.
- **A frame with HD whose background has no HD image** is, by libserum's code
  (`HasHdStaticContent()`), rendered like a frame without HD, upscaled from 128x32. The editor
  shows the HD frame without the background. Not measured: none of the real projects has one.
- **HD pixels painted under a stored HD dynamic mask** are saved as 0, see "Saving". The canvas
  shows the painted color until the project is reopened.

For all of these the Runtime view with `Runtime Plane: 64 Rows` shows the real picture.

## Testing Checklist

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
- `Store`, `Load`, renaming, `Deactivate` and their Undo on the `Sets` page; save and reopen.
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
with its tool tip, that the `Dyn` page there shows the SD colors with its note, that a color
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
test on the `Mask` page: the count follows the frame and a stroke on its mask; `Update Same
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
