# PuP Tester

`View -> PuP Tester...` plays the project's PuP pack the way a machine does: with VPX's PUP
plugin (the Pin Up Player), which comes with the editor as a built-in plugin (see "Plugins").
The editor is the game and its colorization: when a frame with a trigger ID is shown, the
trigger goes to PUP as `D<id>`, and PUP shows the pack's media on its topper, backglass and DMD
screens, which the window draws one above the other.

- `PUP folder` is the PinUP System folder, the one holding `pupvideos`. The editor keeps it for
  the next time. `Browse...` picks it.
- `Game` is the pack's folder name in `pupvideos`. A project gives it: its PuP pack name, or its
  ROM name when it names no pack. It can be changed by hand, for instance to try another pack.
- The line below says whether `<PUP folder>/pupvideos/<game>` is there. Without it PUP starts
  but shows nothing.
- `Start` loads the pack, `Stop` ends it. Opening another project or closing the window stops
  it too.
- `Follow the editor` (on by default) sends the trigger of each frame the editor shows: picking
  a frame in the strip, stepping through frames and playback alike. Frames without a trigger
  send nothing.
- `Trigger` and `Send Trigger` send any trigger ID by hand.
- The list at the bottom shows the triggers sent, with the time, the last one first.

PUP's own messages (the pack it loads, missing files) are in `Help -> Plugin Log`.

Not there yet: PUP's sound, and the triggers of the frame the Runtime view or the frame tester
identifies (the tester sends the trigger of the frame the editor shows).
