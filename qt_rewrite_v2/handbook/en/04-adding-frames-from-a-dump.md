# Adding Frames From A Dump

`File -> Add Frames From Dump...` appends the frames of a dump that the open project does not
have yet. The dialog is the one of "Creating A Project From A Dump", without the ROM name and
with one more option:

- `Leave out frames the project identifies with its masks and shape modes`, on by default.
  - On: a frame of the dump is left out if the runtime would identify it as a frame of the
    project, that is if some frame of the project has the same hash under that frame's own
    comparison mask and shape mode. This is exactly libserum's test; checked on `smb3` by
    feeding all 25127 frames of its dump to libserum with the project loaded: the 11469 frames
    the editor calls known are the ones libserum identifies, the other 13658 are not. Checked
    the same way on `hook_501` with its real 128x16 dump: 11661 of 12838 dump frames known and
    identified by libserum, 1177 not, no disagreement; of the 10476 different frames 9491 are
    left out as known and 985 are added.
  - Off: a frame is left out only if a frame of the project has exactly the same original,
    whatever that frame's mask is.

The summary then reads `N read, a duplicates, b too short, c with too many colors, d already in
the project (x identical, y identified through a mask or shape mode), e to add`.

The dump has to fit the project:

- its frames must have the size of the project's original frames (a 128x16 dump fits a 128x32
  project);
- a dump with shades above 3 cannot be added to a project with 4 original colors. A dump
  without them is added to a project with 16 colors as it is; the shades 0 to 3 are valid
  there.

Otherwise the dialog says why and offers nothing to add. (The Win32 editor does not check and
damages the project.)

The new frames are appended behind the last frame, in the order of the dump, uncolorized like
the frames of a new project. Sections and sprite source frames stay where they are. Adding is
one step for `Undo`, which removes exactly the added frames again; see "Undo And Redo". The
added frames are selected afterwards.

The document must not change while the dialog is open, which is why the dialog is modal;
playback is stopped when it opens.
