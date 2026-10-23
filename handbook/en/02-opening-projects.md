# Opening Projects

Use `File -> Open Project...` and select a project file (`.cdmd`), or the files of an earlier
editor: a `.cROM` or a `.cRZ`.

If the open project has unsaved changes, `Open Project...`, an entry of `Open Recent`, `New
Project From Dump...`, `Quit` and closing the window ask first: `Save` writes the project (a
project without a project file asks where to first) and goes on only if it was written,
`Discard Changes` goes on without saving, `Cancel` (or Escape) stays with the project as it is.
A project without unsaved changes is closed without a question.

## Files of earlier editors

A `.cROM` or `.cRZ` is opened with its editor file beside it, of the same base name: the
`.cRPX` that versions of this editor before the project file wrote, or, for a project of the
Win32 editor, the `.cRP`. If there is neither, the editor asks for one. The project is then
converted: it opens as a project without a project file, the window title says `(converted, not
saved yet)`, and `Save` asks where to write its project file, proposing `<name>.cdmd` beside the
files it came from. The files themselves are never written or renamed; they stay as they are
(see "Project Files"). Closing the converted project without saving loses nothing and asks
nothing.

A project of the Win32 editor takes the `<rom>.pup.csv` and `scaling.txt` beside its `.cROM`
into the project, with its scene frames linked to their scenes (see "Scenes and Scaling"). The
Win32 editor stored the names of palettes, sprites, masks, color sets and sections in the
Windows code page; they are read so, and umlauts and other accented letters show as they were
named.

Opening a `.cRZ` reads the colorization from the archive; nothing is unpacked to disk. The
archive's first file entry is taken, whatever it is named. If a `.cROM` with the same base name
lies next to the `.cRZ`, the file that was selected is the one that is loaded; the other is not
looked at.

After a successful open, the project is added to `File -> Open Recent`: a project file by its
path, the files of an earlier editor by the `.cROM` or `.cRZ` that was opened. If that file no
longer exists when the entry is used, the file with the same base name and the other of the two
extensions is opened instead.

Opening runs in the background. A project that opens within a moment (about 0.3 s) just
appears; a larger one brings up the `Opening Project` window with what is being done
(`Reading the project: N of M parts` for a project file; `Unpacking the .cRZ archive...`, then
`Reading the project: N of M MB` for the files of an earlier editor) with a bar and a
`Cancel` button. While it is up the main window takes no input. `Cancel` (or Escape, or closing
the small window) stops the open: the project that was open stays open, unchanged, and the
status bar says `Open cancelled`. Unpacking a `.cRZ` is done by libserum in one step that cannot
be interrupted; a `Cancel` pressed then takes effect when it ends (`Cancelling when this step
ends...`). The project that was open stays in memory until the new one is complete, so for a
moment both are held. If the open fails, the project that was open stays as well.

Memory: the editor keeps a project in memory in a compact form.

- The parts of a frame that hold one value throughout, which in real projects are most of them
  (the dynamic mask of a frame without dynamic zones, the background masks of a frame without a
  background, the 64-row picture of a frame without HD, rotation tables, unused dynamic colors),
  take no memory until they are edited.
- Parts that are the same in several frames (frames of a scene, repeated frames of a dump) are
  held once until one of them is edited.
- While the editor is idle (2 seconds without input, no window or task open), it packs the
  other parts with LZ4, as libserum packs a colorization for playback, a little at a time, and
  unpacks a part again when it is shown or edited (a few microseconds). The current and the
  selected frames stay unpacked. Packing changes nothing in the project; a save writes the same
  file.

The project's pictures (pixels, masks, colors) take:

| Project | `.cROM` | Before packing | Packed |
| --- | --- | --- | --- |
| `afm_113b` (3047 frames) | 255 MB | 38 MB | 8 MB |
| `trn_174h` (5211 frames) | 429 MB | 220 MB | 72 MB |
| GoT (33977 frames, many with HD) | 2.8 GB | 859 MB | 480 MB |

A full packing pass of the largest of them takes about 1.5 s of idle time; a save unpacks what
it reads, and the next idle moments pack it again.

A project file is read one part at a time. A `.cROM` is read straight into the project; a
`.cRZ` is first unpacked by libserum into memory as a whole, which adds the size of its `.cROM`
while it is being read. On a machine with 8 GB a project with a `.cROM` of 2 GB opens
comfortably; for the largest projects open the `.cROM` rather than the `.cRZ`. Saving needs only
a little more than the project itself (see "Saving").

Opening a project always starts in the frame view: `Edit Sprite` and `Select Capture` are switched
off, a pending sprite capture selection is dropped, and the mask browser selection starts again at
the first frame's mask.
