# Opening Projects

Use `File -> Open Project...` and select a `.cROM` or a `.cRZ` file.

If the open project has unsaved changes, `Open Project...`, an entry of `Open Recent`, `New
Project From Dump...`, `Quit` and closing the window ask first: `Save` writes the project (a new
project asks where to first) and goes on only if it was written, `Discard Changes` goes on
without saving, `Cancel` (or Escape) stays with the project as it is. A project without unsaved
changes is closed without a question.

The app looks for the editor file next to it with the same base name: the `.cRPX`, or, for a
project of the Win32 editor, the `.cRP`. If it finds neither, it asks for one. A project opened
with its `.cRP` is saved as a `.cRZ` and a `.cRPX` beside its `.cROM`, which is then renamed to
`.cROM.bak` (see "Saving" and "The .cRPX and the Win32 editor"). Such a project takes the `<rom>.pup.csv` and `scaling.txt` beside its `.cROM` into
the project, with its scene frames linked to their scenes (see "Scenes and Scaling").
The Win32 editor stored the names of palettes, sprites, masks, color sets and sections in the
Windows code page; they are read so, and umlauts and other accented letters show as they were
named. A name changed here is stored in UTF-8, as this editor reads it.

Opening a `.cRZ` reads the colorization from the archive; nothing is unpacked to disk. The
archive's first file entry is taken, whatever it is named. If a `.cROM` with the same base name
lies next to the `.cRZ`, the file that was selected is the one that is loaded; the other is not
looked at.

A project opened from `<name>.cRZ` stays that project: `Save` rewrites the `.cRZ` and its
`<name>.cRPX`, and no `.cROM` is written (see "Saving"). `Save As...` asks for another `.cRZ`.

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
  files.

The project's pictures (pixels, masks, colors) take:

| Project | `.cROM` | Before packing | Packed |
| --- | --- | --- | --- |
| `afm_113b` (3047 frames) | 255 MB | 38 MB | 8 MB |
| `trn_174h` (5211 frames) | 429 MB | 220 MB | 72 MB |
| GoT (33977 frames, many with HD) | 2.8 GB | 859 MB | 480 MB |

A full packing pass of the largest of them takes about 1.5 s of idle time; a save unpacks what
it reads, and the next idle moments pack it again.

A `.cROM` is read straight into the project; a `.cRZ` is first unpacked by libserum into memory
as a whole, which adds the size of its `.cROM` while it is being read. On a machine with 8 GB a
project with a `.cROM` of 2 GB opens comfortably; for the largest projects open the `.cROM`
rather than the `.cRZ`. Saving needs only a little more than the project itself (see "Saving").

Opening a project always starts in the frame view: `Edit Sprite` and `Select Capture` are switched
off, a pending sprite capture selection is dropped, and the mask browser selection starts again at
the first frame's mask.
Recent-project entries show the full path of the file that was opened (`.cROM` or `.cRZ`) and
internally remember that path and the editor file's path. If that file no longer exists when the
entry is used, the file with the same base name and the other of the two extensions is opened
instead, so an entry keeps working whether only the archive or only the loose `.cROM` is left. An
entry of a project of the Win32 editor that was saved here since opens its `.cRPX`.
