# Color Rotations

Modern `.cROM` projects store four color-rotation slots per frame and per SD/HD resolution. Each
slot contains a length, a delay in milliseconds, and up to 62 RGB565 colors.

The Inspector's `Rotation` topic edits the active frame's rotation slots for the current SD/HD mode:

- `Slot` selects one of the four rotation slots.
- `Length` sets the number of active colors in that slot. A length of `0` disables the slot.
- `Delay` sets the milliseconds between rotation steps.
- The color grid shows all colors in the selected slot at once. Active colors can be clicked to
  open a color picker; colors outside the active length remain visible but disabled.
- `Clear Slot` resets the selected slot to all zeros.

Length, delay, grid color edits, and clear operations apply to all selected frames. Rotation data is
loaded from and saved back to `.cROM`. The static canvas shows unrotated frame data; playback applies
an editor-side rotation preview.

Which pixels a rotation moves, at runtime and in the preview:

- A background, static or sprite pixel whose color is listed in a slot takes the color that many
  places further in the slot at every step. A color listed more than once belongs to the first
  slot, and the first place in it, that lists it.
- **Dynamic content does not rotate**: a pixel colored from a dynamic zone's color set keeps its
  color even if a rotation lists that color. White score digits over a background whose stars
  rotate through white stay white.
- **A dynamic shadow does not rotate** either, whatever it covers and whatever its color is.
- The dynamic pixels of a sprite do rotate, like the rest of the sprite. That is what libserum
  does (`Colorize_Spritev2`).

The editor's preview follows these rules pixel for pixel; an automated test steps libserum's
rotation and compares (see "Testing Checklist"). Its timing is the editor's own: every slot
steps at its delay from the moment playback starts, while libserum keeps a clock per slot that
carries on from frame to frame.
