# Frame Tester

`View -> Frame Tester...` (`Ctrl/Cmd+T`) feeds ROM frames, one after the other, to libserum with
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

Not there: output to a real DMD (ZeDMD, see `View -> Output To DMD`). PuP packs play in their own
window, see "PuP Tester".
