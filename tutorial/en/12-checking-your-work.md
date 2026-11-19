# Checking Your Work

The canvas shows what you edit, with overlays and helpers. What a player sees is what libserum
makes of the project. Two tools show exactly that.

## The Runtime view

`View -> Runtime View` (`Ctrl/Cmd+R`, or `Runtime` in the toolbar) shows the current frame as
libserum renders it: identified by its hash, with its comparison mask, dynamic zones, sprites,
background and rotations. Editing is off while it is on; the Inspector still works, and every
change shows at once.

With the Runtime view on, `Play` feeds the frames to libserum in the order of the strip, with
their durations. Scenes start, backgrounds play, and frames are identified as on a machine.

## The frame tester

`View -> Frame Tester...` (`Ctrl/Cmd+T`) feeds frames to libserum with the whole project loaded,
as a machine would: the selected frames, or every frame of a dump (`Load Dump...`). Use a dump
that was not used to make the project, a new recording of the game, and the tester shows what a
player will see.

The tester shows libserum's 128x32 and 256x64 pictures with the ROM frame between them. The line
below says what libserum did: `identified as frame N`, `not identified`, a scene that plays, a
trigger it reported.

- `Next Unidentified` runs until a frame is not identified. That is the frame your project
  misses: a picture you deleted, a mask that hides too little, a score outside its zone.
- `Find Frame` looks for a project frame in the dump.
- `Show In Editor` goes to the project frame that was identified, to fix it.

Work through a dump with `Next Unidentified` until it reaches the end, and the colorization
covers what the recording showed.

## The DMD output

Colors on a DMD look different from those on a monitor. With a real display attached,
`View -> Output To DMD` (the `DMD` button beside `Runtime`) shows the canvas on it as a live
preview: the frame you edit, libserum's picture in the Runtime view, or the frame tester's
last frame.

Handbook: "Runtime View", "Frame Tester", "DMD Output".
