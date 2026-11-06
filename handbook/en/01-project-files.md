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
  file. They are opened and converted (see "Opening Projects"), never written over; the editor
  writes none of them.
- `.cdmdpkg` is a package: frames of a project with what they use, for another project (see
  "Packages").
- A frame dump (`.txt`) is what a project starts from and what frames are added from: the
  frames a ROM showed, as text. It is only read, see "Creating A Project From A Dump".

Opening is verified with cROM header revision 19, 20 and 21 projects.

## libserum 2.9

Projects are made for libserum 2.9 and later (and the 3.0 to come): what a project may hold is
what that libserum reads.

| | |
| --- | --- |
| Sprites per frame | 128 |
| Sprites in the project | 65535 |
| Backgrounds in the project | 65535 |
| `.cROM` (inside the `.cROMc` and the runtime) | header revision 21, format version 1 |

Format version 1 adds the format version and the number of sprite slots per frame to the header
and stores sprite IDs with 16 bits; a `.cROM` of a newer format version than this editor knows is
refused. A project of the Win32 editor or of libserum 2.8 (32 sprite slots per frame) is opened
as such a project: every frame gets 128 slots, the new ones empty and searching the whole frame.
Nothing of it is lost. The machines need libserum 2.9 or later to play it.

The sprites work exactly as before; there may only be more of them. Stay within 32 sprites per
frame: it remains the recommendation, and every sprite more costs libserum time on each frame.
Going past it is a decision for a single frame that cannot be done otherwise, never best
practice. See "Sprites" for the warnings the editor gives.

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
A project starts without one (`Not chosen`). None of the licenses the editor offers lets others
change the colorization: who wants to change or correct it is asked to work on it together
with you in the Serum community.

| License | Others may |
| --- | --- |
| All rights reserved | play it, but not share it without your permission |
| Creative Commons, no changes | share it unchanged, naming you: CC BY-NC-ND 4.0, or CC BY-ND 4.0 with `Allow commercial use` |
| PPUC Colorization License | use and share it unchanged, naming you, as the choices below say |
| Custom | what your own text says, written in the box that appears |

The choices below the box:

- `Allow commercial use` (Creative Commons): CC BY-ND 4.0 instead of CC BY-NC-ND 4.0.
- `Allow commercial use on a real pinball machine with a PPUC DMD` (CC BY-NC-ND and the PPUC
  license): commercial use only with a PPUC DMD (see [ppuc.org](https://ppuc.org)), not with
  any other display, for example PIN2DMD or PIN2DMD MMI. With Creative Commons it is an
  additional permission ("CC+"): a Creative Commons license itself cannot forbid anything more.
- `On real pinball machines, only with a PPUC DMD, also when not commercial` (PPUC license):
  on a real pinball machine the colorization may be used with a PPUC DMD only. Virtual pinball
  is not affected. This is why the PPUC license exists: Creative Commons cannot say it.

A project with one of the earlier Creative Commons licenses that allow changes (CC0, BY, BY-SA,
BY-NC, BY-NC-SA) keeps it and shows it as no longer offered until another is chosen.

The Creative Commons licenses link their full text (the "deed") below the box. `Attribution`
says who is to be named, for example the authors; `Link` where the colorization or its license
can be found; `Contact` how to reach you, for example an e-mail address or a Discord name.
Changing the license is one undo step, together with the other project settings.

The license is stored in the project file, and the exported `.cROMc` carries it readable at its
start, in plain text: the ROM, the author (`Attribution`; in a team project else the owner,
whoever exports; else `Your name` from `Settings -> Projects`, or else the GitHub account of
your signing certificate; "not named" without any of them), the contact, the contributors (a
team project's other members, or whoever else changed frames, masks, backgrounds, sprites or
palettes of a solo project, for example through a package), the license with its address and
terms, and the terms every
colorization has: the file must not be changed, a signature must not be removed, and changes
are made together with the author in the Serum community. libserum writes the text to its log
when it loads the file. Anyone who opens the file sees it first, tools and AI tools too.
`File -> Show .cROMc Header...` shows the text as the next export writes it, with `Copy`.

## The files of earlier editors

The editor reads the projects of the Win32 editor (`.cROM` or `.cRZ` with `.cRP`) and of its own
versions before the project file (`.cRZ` with `.cRPX`), with everything they hold: the `.cRPX` holds
what a `.cRP` holds, behind a header the Win32 editor does not know (`CDMDcRPX`, a format version,
the size of that part), and after it, in tagged parts, the PuP pack name, the helper lines (see
"Helper Lines"), the scenes and scaling settings (see "Scenes and Scaling"), the Images window's
saved zones, the frames' tags (see "Frame List"), the speaker lights (see "Speaker Lights"), the
libserum target of earlier versions (skipped), the license (see "License") and the names and source
data of sprites beyond the 255 a `.cRP` has room for. A part this editor does not know is skipped; a
`.cRPX` of a newer format version than this editor knows is refused.

Opening converts the project; its files stay as they are, and the Win32 editor can still open
them. What the project file does not keep of them: bytes the Win32 editor left in its tables
without meaning (after the end of a name, entries behind the last section or sprite).
