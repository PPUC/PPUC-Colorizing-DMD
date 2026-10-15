# Opening Projects

Use `File -> Open Project...` and select a `.cROM` or a `.cRZ` file.

If the open project has unsaved changes, `Open Project...`, an entry of `Open Recent`, `New
Project From Dump...`, `Quit` and closing the window ask first: `Save` writes the project (a new
project asks where to first) and goes on only if it was written, `Discard Changes` goes on
without saving, `Cancel` (or Escape) stays with the project as it is. A project without unsaved
changes is closed without a question.

The app looks for the editor file next to it with the same base name: the `.cRPX`, or, for a
project of the Win32 editor, the `.cRP`. If it finds neither, it asks for one. A project opened
with its `.cRP` is saved with a `.cRPX` beside its `.cROM` (see "The .cRPX and the Win32
editor").

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
internally remember that path and the editor file's path. If that file no longer exists when the
entry is used, the file with the same base name and the other of the two extensions is opened
instead, so an entry keeps working whether only the archive or only the loose `.cROM` is left. An
entry of a project of the Win32 editor that was saved here since opens its `.cRPX`.
