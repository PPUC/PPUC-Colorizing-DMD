# Frame Tester

`View -> Frame Tester...` (`Ctrl/Cmd+T`) feeds ROM frames, one after the other, to libserum with
the whole project loaded, as on a machine (CDMD's Frame Tester). The project is taken as it is
in the editor, saved or not.

- The frames are the frames selected in the frame list when the tester opens
  (`Selected Frames`), or every frame of a frame dump (`Load Dump...`, a TXT dump as for
  `New Project From Dump`, also as `.gz` or `.zip`, repeats included; a 128x16 dump is taken centred in 128x32 frames).
  A dump must have the project's frame size and no shade the project's frames cannot have.
  A frame of a dump lasts until the next one, as the dump import measures it.
- The tester shows libserum's 32-row and 64-row planes (a project with 256x64 or 192x64
  original frames has the 64-row one only) and below them the ROM frame that was fed. The
  pictures grow with the window (the planes take two thirds of the height), each drawn with as
  many whole screen pixels per frame pixel as fit. The line
  under the pictures gives the position, the project frame or the dump time, the duration, and
  libserum's answer: `identified as frame N`, `(inactive: the display keeps its picture)`, `not
  identified`, `libserum keeps the picture on display`, and a trigger it reported. While
  libserum shows a frame of a scene, the line names the scene instead, as `scene 50082 plays
  (frame 1)` or `background scene 50082 plays (group 2, frame 3)`. Color rotations run.
- The slider and the position box go to a frame; `Play` and the backwards `Play` step through
  the frames with their durations at the chosen speed (1/4x to 32x); `Loop` starts again at the
  other end. `◀ Frame` and `Frame ▶` go one frame back or forward, also while playing: the
  playback goes on from that frame. At the first or last frame they go round with `Loop` and
  stay there without it.
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
- The Runtime view is switched off while the tester is open and on again afterwards. libserum
  holds the whole project while the tester is open. The `.cROM` is written straight into
  libserum's load and never kept in memory as a whole; loading takes about a second for
  `smb3` and some seconds for the largest projects (7 s for a project of 34000 frames).

The project's scenes play as on a machine (see "Scenes and Scaling"): a scene in
front of the picture as well as a background scene. libserum renders their pictures in its
rotation step, which the tester calls when libserum asks for it, as it does for the color
rotations of the frame shown; both go on while the tester stays on a frame. The tester
schedules that step as a machine's host (libdmdutil) does: a frame that libserum does not
identify, or reports as the one on display, leaves a running scene or rotation going on its
own time; a newly identified frame sets the time anew, and a step that is due runs before the
next frame is fed. A frame with the trigger of the background scene that runs is reported as
the one on display: libserum keeps the scene going and puts that frame on top of it at the
scene's next step, where the tester shows it. They keep
libserum's own timing: at a play speed other than 1x the dump's frames come faster or slower,
a scene and a rotation do not.

`Play scenes` (on by default) switches the scenes off: libserum loads the project again without
them, neither the project's nor those of a `pup.csv` beside it, and the frames of the game are
tested on their own; a frame that triggers a scene then shows its own picture.

Not there: output to a real DMD (ZeDMD, see `View -> Output To DMD`). PuP packs play in their own
window, see "PuP Tester".
