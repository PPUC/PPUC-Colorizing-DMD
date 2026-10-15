# Saving

Use `File -> Save` or the standard Save shortcut.

A new project (see "Creating A Project From A Dump") has no files until it is saved for the
first time. `Save` and `Save As...` then ask for the `.cROM` to write, proposing `<ROM
name>.cROM` in the folder of the dump; the name the runtime looks for is the ROM name. If the
question is cancelled nothing is written and the project stays unsaved. After the first save the
window title shows the file name instead of `<ROM name> (not saved yet)`, the project is in
`File -> Open Recent`, and `Save` writes to those files without asking.

Every save writes three files with the same base name into the project folder: `<name>.cROM`,
`<name>.cRPX` and `<name>.cRZ`. The archive holds exactly the bytes of the `.cROM` just written,
deflate-compressed, in one entry named `<ROM name>.cRom` (the ROM name stored in the project,
which need not be the file name), as the Win32 editor writes it. An archive that already exists
under another spelling of the extension (`.crz`) is the one that is replaced.

Use `File -> Save As...` to save the project under a new name. The app asks for a target `.cROM`
path and writes the `.cROM`, a `.cRPX` and a `.cRZ` with the same base name there.

Use `File -> Export Current Frame PNG...` to export the current frame as a PNG. The picture is
libserum's output for the frame, the one a machine shows, not the editor's own rendering; see
"Runtime View" for how it is made. Which plane is exported:

- with the Runtime view on, the plane it shows;
- otherwise the plane of the editing mode: the 32-row plane in SD mode and the 64-row plane in HD
  mode. The 64-row plane exists for every frame; for a frame without HD it is libserum's upscale
  of the 128x32 picture;
- the one plane of a project with 256x64 or 192x64 original frames.

The proposed file name ends in `_sd` or `_hd`, or in `_256x64` or `_192x64`. `Pixel Shape` and
`Pixel Size` of the Settings apply. The assigned background is always part of the picture: `Show BG` is a setting
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

`File -> Import Movie Clip Into Selected Frames...` gives each selected frame a picture of a
movie clip (MP4, MOV, MKV, AVI, WebM, MPEG, WMV, FLV or an animated GIF; read with ffmpeg, which
comes with the PUP plugin, so a package without it says it cannot read clips). A window shows
the clip:

- the slider, `◀` and `▶` (one picture of the clip back or on) choose where the first selected
  frame starts;
- dragging over the picture draws the crop zone, the part of the picture that goes into the
  frames; `Keep the DMD's proportions` (on by default) makes it as wide to high as the frame, so
  nothing is stretched; `Whole Picture` takes all of it again;
- the first selected frame's result is shown at DMD resolution;
- the next selected frames get the clip as the frames play (each frame's duration further on,
  as the Win32 editor does) or a picture every 5 to 3000 ms.

`Import` fills the selected frames in their order, on the active SD/HD plane, as `Import Image`
does: undoable per frame, frames without HD data skipped in HD mode. Frames whose time is past
the clip's end are left as they are; the status bar says how many. A long import shows its
progress and can be cancelled; the frames done so far keep their pictures.

Save behavior:

- Saving runs in the background. A save that is done within a moment (about 0.3 s) shows
  nothing; a longer one brings up the `Saving Project` window: `Writing the .cROM and .cRPX: N of
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
  The PuP pack name is kept in UTF-8 in the `.cRPX`, so any name survives on every system; read
  from a Win32 editor's `.cRP`, where it is UTF-16, it is converted.
  Deleting frames, sprites and comparison masks keeps the parts of it that are numbered by
  them in step, see "Adding And Deleting Records".
- The numbers of frames, comparison masks, sprites and backgrounds in the file headers are
  those of the project as it is; every block is written for exactly these records.
- HD pixels that the Win32 editor left in a frame or background whose HD is switched off are
  carried through until HD is created or removed for it here.
- Marks the document clean after successful save.

Files written by earlier Qt editor builds are not supported; only projects saved by the Win32
editor (currently 3.17.3) or by this serializer are.

## Exporting an animated GIF

`File -> Export Animated GIF...` writes the selected frames, in frame order, as an animated GIF
that loops, as CDMD's GIF export:

- The pictures are the ones the editor composes on the plane the canvas shows (SD or HD; the
  native plane of a 256x64 or 192x64 project), with the background when the canvas's background
  layer is on. Every frame must have that plane.
- `File -> Export Animated GIF With Originals...`, or holding `Shift` while choosing `Export
  Animated GIF...` (the Win32 editor's Shift+click), puts each original frame above its
  colorized picture, in the shades the editor shows originals in, 15 picture pixels apart and
  with the same dot size; the narrower of the two is centred.
- A frame pixel becomes a square dot with a black gap, about 640 picture pixels across (5 per
  pixel of a 128-wide plane, 3 of 192, 2 of 256, which has no gap; with the originals, the wider
  of the original and the plane counts).
- Colour rotations step as in the editor's preview: a picture at every step within the frame's
  duration; they start again with every frame.
- Each picture shows for its frame's duration or until the next rotation step. GIF counts time
  in 1/100 s: the times are rounded as they add up, so the whole stays right. A picture that
  would show less than 2/100 s (which viewers stretch to about 1/10 s), such as a frame shorter
  than 15 ms, is left out and its time given to the next picture.
- A picture with more than 256 colours keeps its 255 most frequent ones; other pixels take the
  nearest of them.
- The export shows its progress and can be cancelled; an existing file is replaced only when the
  new one is complete.

## Exporting the .cROMc

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
- On Windows libserum opens files by paths in the system code page. If the temporary folder has
  other characters (a user name in another script) and Windows keeps no short name for it, the
  export works in `%ProgramData%\ColorizingDMD` instead and removes its files afterwards.
- On a desktop, libserum loads a `<rom>.cROMc` it finds in the colorization folder instead of
  the `<rom>.cROM` beside it, without comparing their dates. An outdated `.cROMc` there hides
  later changes of the project: export again or remove it.

For an unedited project the result is byte for byte the `.cROMc` libserum generates from the
project's files (checked on `afm_113b`, `hook_501`, `trn_174h`, `smb3` and the 192x64
`frankst1`).

## Autosave and recovery

Every 10 minutes, as in CDMD, a project with changes that were not autosaved yet is written to
`<name>(auto).cROM` and `<name>(auto).cRPX` beside its files (`afm_113b(auto).cROM` for
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
