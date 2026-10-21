# Project Files

The Qt rewrite opens the projects of the Win32 editor (CDMD 3.17.3) and saves its own:

- `.cROM` stores colorized frame data and runtime project data. It is what libserum reads, and
  stays in the format libserum knows.
- `.cRPX` is this editor's editor file: original/reference frames and editor-side metadata such
  as sprite names and frame durations. Every save writes it.
- `.cRP` is the Win32 editor's editor file, with the same content. It is only read: a project
  of the Win32 editor is opened with it and saved with a `.cRPX` (see "The .cRPX and the Win32
  editor").
- `.cRZ` is a zip archive holding the `.cROM`, in one entry named `<ROM name>.cRom`. It is the
  form in which a colorization is usually handed around, together with the editor file. The
  editor opens it directly and saves projects as it.
- `.cROMc` is not edited directly.
- A frame dump (`.txt`) is what a project starts from and what frames are added from: the
  frames a ROM showed, as text. It is only read, see "Creating A Project From A Dump".

Saving writes complete `.cRZ` and `.cRPX` files from the in-memory project (see "Saving"); a
`.cROM` is only read. Opening is verified with cROM header revision 19, 20 and 21 projects.
A project for libserum 2.8 keeps its revision: revision 20 projects are saved as revision 20,
everything older as revision 19, so a save never moves a project to a revision an older
libserum cannot read. A project for libserum 3.0 is saved as revision 21 (see "libserum
target").

## libserum target

Every project is made for a libserum, chosen in `File -> Project Settings...` (`Made for`). It
decides what the project may hold and how the `.cROM` is written:

| | libserum 2.8 | libserum 3.0 |
| --- | --- | --- |
| Plays with | libserum 2.8 and later | libserum 3.0 and later only |
| Sprites per frame | 32 | 128 |
| Sprites in the project | 255 | 65535 |
| Backgrounds in the project | 255 | 65535 |
| `.cROM` | revision 19 or 20, format version 0 | revision 21, format version 1 |

A project of the Win32 editor, a new project and every `.cROM` without a format version are
for libserum 2.8, and stay readable by the Win32 editor's libserum as long as they are. A
`.cROM` of format version 1 is opened as a project for libserum 3.0. Format version 1 adds the
format version and the number of sprite slots per frame to the header and stores sprite IDs
with 16 bits; a `.cROM` of a newer format version than this editor knows is refused.

Switching to libserum 3.0 gives every frame 128 sprite slots; the new ones are empty and search
the whole frame. Switching back is only possible while the project fits libserum 2.8: the
dialog names what keeps it from fitting (too many sprites or backgrounds, frames with more than
32 sprites and the first of them) and `OK` stays disabled. The change is one undo step,
together with the other project settings.

More than 32 sprites on one frame can make libserum's sprite detection noticeably slow, so the
`Frame Sprites` section warns about such a frame (see "Sprites").

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

## The .cRPX and the Win32 editor

A project saved by this editor can no longer be opened by the Win32 editor. This is on purpose:
the editor file has to grow for new features, which the Win32 editor would not understand.

- The Win32 editor opens a `.cROM` with the `.cRP` of the same name, and reads that file without
  any check. This editor never writes a `.cRP`: its `.cRPX` holds what a `.cRP` holds, behind a
  header the Win32 editor does not know (`CDMDcRPX`, a format version, the size of that part),
  and room for more data after it, in tagged parts: the PuP pack name, the helper lines (see
  "Helper Lines"), the scenes and scaling settings (see "Scenes and Scaling"), the Images
  window's saved zones, the frames' tags (see "Frame List"), the libserum target and the
  names and source data of sprites beyond the 255 a `.cRP` has room for. A part this editor
  does not know (one of a later version) is skipped.
- When a save writes a project that has a `.cRP` beside it (a project of the Win32 editor saved
  here for the first time), that `.cRP` is renamed to `<name>.cRP.bak`, so that the Win32 editor
  cannot open the new `.cROM` with the old editor file. The status bar says so. Opening a
  project of the Win32 editor says in the status bar that saving it here has this effect.
- To go back to the Win32 editor, the `.cRP.bak` belongs to the `.cROM` of the save before; it
  does not match the `.cROM` this editor wrote.
- A `.cRPX` of a newer format version than this editor knows is refused when opened.

The `.cROM` and `.cRZ` stay readable by libserum, and so by every machine.
