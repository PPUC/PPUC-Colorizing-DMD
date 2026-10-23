# Project Files

A project is one file, its project file: `<name>.cdmd`. Every save writes it, and it holds
everything of the project: the original and colorized frames, the comparison masks,
backgrounds, sprites and palettes, the scenes, the speaker lights, the license, and the editor's
data (sections, tags, helper lines, color sets, saved image zones).

Inside, it is a zip archive with one part per frame, comparison mask, background and sprite.
Each of them has an ID of its own that it keeps for its whole life, also when other frames are
deleted or moved, and the name of who changed it last (`Settings -> Projects -> Your name`) and
when. A save notes that only for what changed since the last save. This is what working on a
colorization together, and taking frames from one colorization into another, will be built on.

Other files:

- `.cROMc` is what a machine loads. `File -> Export .cROMc...` writes it (see "Saving").
- `.cROM`, `.cRZ` (a zip archive holding the `.cROM`), `.cRP` and `.cRPX` are the files of
  earlier editors: the Win32 editor (CDMD 3.17.3) and versions of this editor before the project
  file. They are opened and converted (see "Opening Projects"), never written over.
  `File -> Export For Earlier Editors...` writes a `.cRZ` and a `.cRPX` of the project: the `.cRZ`
  plays with libserum 2.8 and 2.9, the `.cRPX` opens in the editor versions before the project
  file.
- `.cdmdpkg` is a package: frames of a project with what they use, for another project (see
  "Packages").
- A frame dump (`.txt`) is what a project starts from and what frames are added from: the
  frames a ROM showed, as text. It is only read, see "Creating A Project From A Dump".

Opening is verified with cROM header revision 19, 20 and 21 projects. The `.cRZ` of
`Export For Earlier Editors` keeps the project's revision for libserum 2.8: revision 20 projects
are written as revision 20, everything older as revision 19, so an export never moves a project
to a revision an older libserum cannot read. A project for libserum 2.9 is written as revision 21
(see "libserum target").

## libserum target

Every project is made for a libserum, chosen in `File -> Project Settings...` (`Made for`). It
decides what the project may hold and how the `.cROMc` and an exported `.cRZ` are written:

| | libserum 2.8 | libserum 2.9 |
| --- | --- | --- |
| Plays with | libserum 2.8 and later | libserum 2.9 and later only |
| Sprites per frame | 32 | 128 |
| Sprites in the project | 255 | 65535 |
| Backgrounds in the project | 255 | 65535 |
| `.cROM` | revision 19 or 20, format version 0 | revision 21, format version 1 |

A project of the Win32 editor, a new project and every `.cROM` without a format version are
for libserum 2.8, and stay readable by the Win32 editor's libserum as long as they are. A
`.cROM` of format version 1 is opened as a project for libserum 2.9. Format version 1 adds the
format version and the number of sprite slots per frame to the header and stores sprite IDs
with 16 bits; a `.cROM` of a newer format version than this editor knows is refused.

Switching to libserum 2.9 gives every frame 128 sprite slots; the new ones are empty and search
the whole frame. Switching back is only possible while the project fits libserum 2.8: the
dialog names what keeps it from fitting (too many sprites or backgrounds, frames with more than
32 sprites and the first of them) and `OK` stays disabled. The change is one undo step,
together with the other project settings.

The sprites work exactly as before; libserum 2.9 only allows more of them. Stay within 32
sprites per frame: it remains the recommendation, and every sprite more costs libserum time on
each frame. Going past it is a decision for a single frame that cannot be done otherwise, never
best practice. See "Sprites" for the warnings the editor gives.

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

## License

`File -> Project Settings...` has a `License` for the colorization: what others may do with it.
A project starts without one (`Not chosen`).

| License | Others may |
| --- | --- |
| All rights reserved | play it, but not share or change it without your permission |
| CC0 1.0 | use it for anything, without naming you |
| CC BY 4.0 | share and change it, also commercially, naming you |
| CC BY-SA 4.0 | as CC BY, with changed versions under the same license |
| CC BY-NC 4.0 | share and change it, not commercially, naming you |
| CC BY-NC-SA 4.0 | as CC BY-NC, with changed versions under the same license |
| Custom | what your own text says, written in the box that appears |

The Creative Commons licenses link their full text (the "deed") below the box. `Attribution`
says who is to be named, for example the authors; `Link` where the colorization or its license
can be found. Changing the license is one undo step, together with the other project settings.

The license is stored in the project file, and in the `.cRPX` of `Export For Earlier
Editors`. The `.cROMc` and the `.cRZ` do not carry it yet, so a machine does not see it.

## The files of earlier editors

The editor reads the projects of the Win32 editor (`.cROM` or `.cRZ` with `.cRP`) and of its own
versions before the project file (`.cRZ` with `.cRPX`), with everything they hold: the `.cRPX`
holds what a `.cRP` holds, behind a header the Win32 editor does not know (`CDMDcRPX`, a format
version, the size of that part), and after it, in tagged parts, the PuP pack name, the helper
lines (see "Helper Lines"), the scenes and scaling settings (see "Scenes and Scaling"), the Images
window's saved zones, the frames' tags (see "Frame List"), the speaker lights (see "Speaker
Lights"), the libserum target, the license (see "License") and the names and source data of
sprites beyond the 255 a `.cRP` has room for. A part this editor does not know is skipped; a
`.cRPX` of a newer format version than this editor knows is refused.

Opening converts the project; its files stay as they are, and the Win32 editor can still open
them. What the project file does not keep of them: bytes the Win32 editor left in its tables
without meaning (after the end of a name, entries behind the last section or sprite). The
cROM of an exported `.cRZ` is the same, byte for byte, as the one the project was opened from,
for an unedited project.

`File -> Export For Earlier Editors...` asks for a `.cRZ` and writes it and a `.cRPX` of the same
base name beside it, as the versions of this editor before the project file saved a project
(see "Saving"). The Win32 editor cannot open them: it opens a `.cROM` with a `.cRP` only.
