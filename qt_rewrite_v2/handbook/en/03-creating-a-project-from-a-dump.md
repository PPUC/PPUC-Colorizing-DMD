# Creating A Project From A Dump

`File -> New Project From Dump...` makes a new project of the frames of a frame dump, as
`New project` of the Win32 editor does.

## The dump

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

## The dialog

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

## The new project

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
