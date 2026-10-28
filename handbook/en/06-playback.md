# Playback

The top toolbar contains editor-only playback controls:

- `Play` advances through the currently visible frame list.
- `Pause` stops playback.
- `Loop` restarts at the first visible frame after the last visible frame.
- `Stay` keeps `Play` on the current frame: the frame does not change, while its color rotations
  and its background scene run, to look at them or to draw while they move. Switched while
  playing, playback goes on in the other mode.
- `Fixed Duration` shows every frame for the interval beside it; unchecked (the default), each
  frame plays for its own duration (`Duration` in the Frame section).
- The interval field controls the fallback frame advance interval in milliseconds.

Playback respects the current frame filter in the left `Frames` tab. When `Fixed Duration` is
unchecked, frames with missing, zero, or unusually large durations use the fallback interval.

During playback, the central preview advances color-rotation slots for the displayed frame using
each slot's stored delay, counted from the start of playback. Which pixels rotate follows libserum,
see "Color Rotations". The background scene a frame starts plays through its background mask the
same way (see "Backgrounds"). This is an editor preview; libserum remains the runtime source of truth, and
its own rotation, with its own clock, is what playback in the Runtime view shows.

With the Runtime view on, playback is done by libserum instead: the frames are fed to it in the
order of the list, with the same timings, libserum rotates the colors, and it identifies every
frame as it would on a machine, which depends on the frames before it. The scenes the frames start
play, background scenes behind the frames too. With `Stay` libserum is given the current frame
alone and plays its rotations and its scene. See "Runtime View".

Rotation timing is not a visual preference. The per-slot delay stored in the project controls both
runtime behavior and editor playback, so future rotation-editing UI must keep timing edits explicit
and must be validated against libserum playback behavior.
