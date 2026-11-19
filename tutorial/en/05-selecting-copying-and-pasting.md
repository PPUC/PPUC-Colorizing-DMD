# Selecting, Copying And Pasting

Much of a colorization repeats: the same object in many frames, at other places. Copy and
paste carries colored pixels from one frame to the next.

## Making a selection

The selection is a set of pixels, shown in translucent cyan.

- The `Sel` tool drags a rectangle, circle or ellipse (its `Shape` is chosen in the `Palette`
  section) and replaces the selection.
- `Ctrl/Cmd` with any drawing tool adds to the selection instead of painting;
  `Ctrl/Cmd+Shift` takes away.
- Hold `W` while selecting to take only pixels that are not black: the object without the dark
  space around it.
- `Ctrl/Cmd+A` selects all pixels, `Ctrl/Cmd+D` none, `Ctrl/Cmd+Shift+I` inverts.

The selection is not part of the project and not part of its undo. It has an undo of its own:
`Alt+Z` (`Option+Z`) brings back the selection before its last change.

## Copy and paste

1. Select the colored object on a frame that is done.
2. `Ctrl/Cmd+C` copies it: its colors, the original shades under it and its dynamic zones.
3. Go to the next frame and press `Ctrl/Cmd+V`. The copy floats over the canvas.
4. Move it with the arrow keys (with `Shift` by 10 pixels) until it covers the object; `K` and
   `L` mirror it.
5. A click or `Enter` pastes it; `Esc` cancels.

With several frames selected, the paste goes on every one of them. For an object that slides in
frame by frame, the next paste starts where the last one was put: `Ctrl/Cmd+V`, a few arrow
keys, `Enter`, next frame.

`Edit -> Paste Where Originals Match` (`Ctrl/Cmd+Shift+V`) pastes only the pixels whose original
shade on the target frame is the copied pixel's own. The copy then leaves the pixels of other
objects alone.

`Edit -> Stay In Paste Mode` keeps the copy after a paste, to stamp it again elsewhere.

## Letting the editor do it

`Edit -> Auto Copy...` copies the colors inside the selection to every frame whose original shows
exactly the same there: a logo that stands still in many frames is colored in one step. The
window says how many frames it would change before anything happens. `Edit -> Scrolling Auto
Fill...` colors the frames of a scrolling between two frames you colored.

## Copying a frame's settings

`Edit -> Copy Frame Settings...` gives selected frames the settings of the current frame: its
dynamic zones, background, sprites, color rotations, comparison mask, trigger and duration,
chosen in the window. Each chosen setting replaces the target's. Useful for a
frame found later that belongs with frames already set up.

Handbook: "Selection", "Copy And Paste", "Auto Copy And Scrolling Fill".
