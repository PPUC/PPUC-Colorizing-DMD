# Saving

Use `File -> Save` or the standard Save shortcut. A save writes the project file,
`<name>.cdmd` (see "Project Files").

A project without a project file (a new one, see "Creating A Project From A Dump", or one
converted from the files of an earlier editor, see "Opening Projects") gets one at its first
save: `Save` and `Save As...` then ask where to write it, proposing `<ROM name>.cdmd` in the
folder of the dump for a new project and `<name>.cdmd` beside the files it came from for a
converted one. If the question is cancelled nothing is written and the project stays as it was.
After the first save the window title shows the file name, the project file is in
`File -> Open Recent`, and `Save` writes to it without asking.

Use `File -> Save As...` to save the project under a new name; the app asks for the `.cdmd` to
write.

Every save notes, for each frame, comparison mask, background, sprite and palette that changed
since the last save, who changed it (`Your name` in `Settings -> Projects`) and when. What did
not change keeps what it had. A change that is undone by hand still counts as a change.

Use `File -> Export Current Frame PNG...` to export the current frame as a PNG. The picture is
libserum's output for the frame, the one a machine shows, not the editor's own rendering; see
"Runtime View" for how it is made. Which plane is exported:

- with the Runtime view on, the plane it shows;
- otherwise the plane of the editing mode: the 32-row plane in SD mode and the 64-row plane in HD
  mode. The 64-row plane exists for every frame; for a frame without HD it is libserum's upscale
  of the 128x32 picture;
- the one plane of a project with 256x64 or 192x64 original frames.

The proposed file name ends in `_sd` or `_hd`, or in `_256x64` or `_192x64`. `Pixel Shape` and
`Pixel Size` of the Settings apply. The assigned background is always part of the picture: `Show Background` is a setting
of the editing canvas and has no effect on the export.

If libserum has no picture for the frame, nothing is written and no file dialog comes up. The
status bar says why: `Frame N is not exported:` followed by the reason the Runtime view gives for
the frame (the frame is inactive and skipped, it lacks its 256x64 data, or libserum's own error
text). The editor's own rendering is never written in its place: a file would then show what no
machine shows. There is no separate export of the editor's rendering any more. It served no
purpose the runtime export does not: the frames libserum renders nothing for are frames with
nothing colorized to show (an inactive frame is one that is not colorized, a 256x64 frame without
its data is black), and the image of a background is exported by `Export Background`.

## Importing images and movie clips

Pictures go into frames as in the Win32 editor's Images window. First select frames, and on the
canvas the pixels that get the picture (any selection tool; without a selection, the whole
frame). Then:

- `File -> Import Image Into Selected Frames...` (or the Frame section's `Import Image`): a
  PNG, BMP or JPEG;
- `File -> Import Image From the Clipboard...`: the picture on the clipboard;
- `File -> Import Movie Clip Into Selected Frames...`: a movie clip (MP4, MOV, MKV, AVI, WebM,
  MPEG, WMV, FLV or an animated GIF; read with ffmpeg, which comes with the PUP plugin, so a
  package without it says it cannot read clips);
- the Background section's `Import Image`: an image for the selected background.

The `Import Image` window shows the picture and a zone of it with the proportions of the
selection's bounds, the part that is scaled into the selection:

- the zone follows the mouse (dashed); the mouse wheel makes it larger or smaller, one screen
  pixel a notch, ten with `Shift`. A click takes it: the pink zone, which the selection gets.
  `Whole Picture` takes the largest zone again;
- a right click takes an end zone (blue): over the selected frames the zone then moves and
  grows or shrinks from the pink zone to the blue one (the Win32 editor's scroll copy).
  `No End Zone` drops it;
- `Brightness` (-50 to 50), `Contrast` (shown as the factor, 0.25 to 2.00) and `Blur` (0 to 10
  pixels) change the picture as in the Win32 editor; `Scaling` is `Nearest` (one pixel of the
  picture each, as in the Win32 editor) or `Smooth`. They are kept for the next import;
- `Into`: the selection of the selected frames, or the selected background (the rectangle of
  the selection, or all of it). The planes: the 128x32 and the 256x64 picture of an ordinary
  project, each or both; a frame or background without HD gets it first (as `Create HD` with
  `Nearest`), the Win32 editor's "copy to extra resolution (create if needed)". The selection is
  carried to the other plane: doubled, or halved where any of the pixels a pixel covers is
  selected;
- `The selection gets` shows the result for the first frame; pixels outside the selection,
  which keep theirs, are dark;
- `Saved zones` keeps the zone, the end zone and the picture's settings under a name in the
  project (the Win32 editor's crop zone saves): type a name and `Save Zone` (a name that exists
  is replaced), choose a name in the list to take them again, `Delete Zone` removes the chosen
  one. A saved zone is kept as a part of the picture, so it fits a picture of another size, and
  it keeps the proportions of the selection at hand. Saved zones are part of the project as soon
  as they are saved, also when the window is then cancelled; they are not undo
  steps. The crop zone saves of the Win32 editor were kept in screen pixels of its window and
  cannot be carried over: they stay in the project as they were, unused.

For a movie clip the window also has the clip's time: the slider, `◀` and `▶` step back or on
by the unit chosen (`Hour`, `Minute`, `Second` or `Picture`), and the next selected frames get
the clip as the frames play (each frame's duration further on, as the Win32 editor does), a
picture every 5 to 3000 ms, or this picture, all of them.

`Import` writes the frames in their order, each frame one undo step. Frames whose time is past
the clip's end are left as they are; the status bar says how many, and how many HD frames were
created. A long import shows its progress and can be cancelled; the frames done so far keep
their pictures.

## Save behavior

- Saving runs in the background. A save that is done within a moment (about 0.3 s) shows
  nothing; a longer one brings up the `Saving Project` window: `Writing the project: N of M
  parts` with a bar, then `Replacing the project files...`. While it is up the main window takes
  no input at all: nothing can be edited, no menu or shortcut works, so the file holds exactly
  the project as it was when `Save` was chosen. The project is saved from its blocks as they are
  in memory, without a copy of them, so a save needs little memory beyond the project.
- `Cancel` (or Escape, or closing the small window) stops the save: the temporary file is
  removed, the project file on disk stays exactly as it was, the project keeps its unsaved
  changes, and the status bar says `Save cancelled: the project file on disk is unchanged`.
  Once the file is being replaced, `Cancel` is disabled.
- The file is written completely next to its target first (as `<file>.saving`) and then swapped
  in, so a failed or interrupted save does not leave a truncated project behind.
- The parts are compressed at a fast level. GoT (34 000 frames, many with HD) saves in about
  20 s to 500 MB, where its `.cRZ` and `.cRPX` took 34 s for 546 MB; `afm_113b` saves in 0.8 s
  to 8 MB.
- The time spent on the project is written as counted so far (see "Canvas").
- HD pixels that the Win32 editor left in a frame or background whose HD is switched off are
  carried through until HD is created or removed for it here.
- The 96x32 data of a project with 192x64 original frames is not kept. If the files it was
  opened from had 96x32 pictures in use, the first save asks first (see "Projects With 192x64
  Original Frames").
- Marks the project as saved after a successful save.

The cROM the editor hands to libserum (in the Runtime view, the frame tester and the `.cROMc`
export) is the one libserum 2.9 reads, header revision 21 and format version 1 (see "Project
Files"):

- Frame identification hashes are recalculated from the original frame, its comparison mask, and
  its shape mode.
- The "active frame" flag is recalculated: a frame shorter than 15 ms stays inactive until it is
  colorized or gets dynamic content. The frame list marks the frames the runtime skips for that
  reason, see "Frame List".
- Colorized pixels covered by a dynamic mask are written as 0, as the Win32 editor does. On the
  HD plane of an ordinary project this is the stored HD dynamic mask, which the runtime does not
  use and the editor does not show (see "Dynamic Masks"): an HD pixel painted under it is still
  written as 0, and is black on a machine wherever no SD dynamic zone covers it. The Runtime view
  shows that; the editing canvas shows the painted color.
- The 96x32 blocks of a project with 192x64 original frames are written as "no extra data".

## Exporting an animated GIF

`File -> Export Animated GIF` writes the selected frames, in frame order, as an animated GIF
that loops, as CDMD's GIF export. Its entries say what each picture holds:

- `Colorized Frames...`: the picture the editor composes on the plane the canvas shows (SD or
  HD; the native plane of a 256x64 or 192x64 project). Every frame must have that plane.
  Holding `Shift` while choosing it adds the originals, as the Win32 editor's Shift+click did.
- `With Originals...`: each original frame above its colorized picture, in the shades the editor
  shows originals in.
- `Both Planes...`: the 128x32 picture above the 256x64 one, as the Win32 editor's GIF of a
  project with both. A frame without a 256x64 picture leaves its part black; when none of the
  frames has one, there is only the 128x32 picture.
- `Originals And Both Planes...`: the original, the 128x32 and the 256x64 picture.

The two `Both Planes` entries are there in a project with a 128x32 and a 256x64 plane only.
The parts lie 15 picture pixels apart with the same dot size, the narrower ones centred:

- The colorized pictures have the background when the canvas's background layer is on.
- As in the Win32 editor, every frame pixel becomes a dot of 4x4 picture pixels without its
  corners, with a black gap of one: 5 picture pixels per frame pixel on every plane, so a
  128x32 picture is 640x160 and a 256x64 one 1280x320.
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
project. libserum makes it, exactly as it does on a machine when it finds the `.cROM` alone.

A `.cROMc` belongs to the libserum that makes it: the editor's own (`Help -> About PPUC
Colorizing DMD` names it), of the line after 2.8. A machine needs libserum 2.9 or later to load
it; libserum 2.8 cannot read it.

- The project is taken as it is in the editor, saved or not, and handed to libserum directly,
  without a `.cROM` on disk. It carries the project's scenes and scaling settings (see "Scenes
  and Scaling"), so they are in the `.cROMc`. Only for a project that has none (one of the
  Win32 editor not yet opened with scenes) the `<rom>.pup.csv` and `scaling.txt` of the
  project's folder go in with it, if they are there (in any case of the letters), as on a
  machine; the status bar then names them.
- libserum's part cannot be interrupted and takes from about a second to half a minute for
  large HD projects.
- An existing file is replaced only once the new one is complete. A target name other than
  `<rom>.cROMc` is allowed; the status bar reminds that libserum looks for `<rom>.cROMc`.
- The Runtime view is switched off for the export and on again afterwards; playback stops.
- On Windows libserum opens files by paths in the system code page. If the temporary folder has
  other characters (a user name in another script) and Windows keeps no short name for it, the
  export works in `%ProgramData%\PPUC-Colorizing-DMD` instead and removes its files afterwards.
- On a desktop, libserum loads a `<rom>.cROMc` it finds in the colorization folder instead of
  the `<rom>.cROM` beside it, without comparing their dates. An outdated `.cROMc` there hides
  later changes of the project: export again or remove it.

For an unedited project the result is byte for byte the `.cROMc` libserum generates from the
project's files (checked on `afm_113b`, `hook_501`, `trn_174h`, `smb3` and the 192x64
`frankst1`).

## Autosave and recovery

Every 10 minutes, as in CDMD, a project with changes that were not autosaved yet is written to
`<name>(auto).cdmd` beside its project file (`afm_113b(auto).cdmd` for `afm_113b.cdmd`),
compressed as a save is. A project without a project file (a new one, or one converted from the
files of an earlier editor) is autosaved under its ROM name in the editor's own folder (on
Windows `%LOCALAPPDATA%\ppuc.org\PPUC Colorizing DMD\autosave`, on macOS
`~/Library/Application Support/ppuc.org/PPUC Colorizing DMD/autosave`, on Linux
`~/.local/share/ppuc.org/PPUC Colorizing DMD/autosave`).

- The file is written in the background while you go on working; it shows the project as it
  was when the autosave started. The status bar says `Autosaved to <file> at <time>`, or why
  it failed. Nothing is written while a dialog is open or the project is unchanged.
- An autosave never replaces the project file, and notes no author or time of a change: only a
  save does. An autosave stopped by quitting leaves the earlier autosave as it was.
- Opening a project whose autosave is newer than its file asks first: `Open Autosave` opens the
  autosave as that project with unsaved changes (`Save` then writes it to the project file),
  `Open Project` opens the project file. The autosave is kept either way, as CDMD keeps it.
- After `Discard Changes` the autosave holding those changes is not offered again; the file
  stays. The autosave of a project without a project file is deleted when its changes are
  discarded or when it is saved for the first time.
- At start the editor offers the newest autosave of a project without a project file: `Open`
  (as a new project with unsaved changes), `Delete` or `Later`.
- Opening an `(auto)` file directly works as for any project; its autosave then goes to the same
  file.
- The autosaves of earlier versions (`<name>(auto).cRZ` and `.cRPX`) are not offered any more;
  they can be opened as the files of an earlier editor.
