# Drawing Tools

Drawing tools operate on the active SD/HD mode. `Point`, the shapes and `Flood` paint with the
draw color or, in `Set Replace` mode, with the edit color of each pixel's original shade (see
"Draw color modes" under "Colors").

Before a click, the pixel under the mouse shows what the click would paint there: with
`Point` and the shapes (where their stroke starts), in the draw color on the frame or a
background, in the layer's color on a mask or dynamic zone (on the original frame where a click
there paints; with `Shift`, which erases, dark). The pixel to hit is visible before it changes.
`Flood`, `Pick` and `Grad` show none, nor does the mouse while `Ctrl/Cmd` is held for selecting,
in paste mode or while a `Select By Color` or `Recolor` click is pending.

A drag starts on the frame but may end beside it, beyond the frame's edge (as in the Win32
editor): the line, rectangle, circle or ellipse is then larger than the frame, and only its part
on the frame is painted. A filled circle or ellipse larger than the frame, or a gradient shape,
gives a curve or a gradient that runs past the frame's edges. The end may lie up to a few frame
sizes away.

Rectangles, circles and ellipses (filled or not, and the shapes of `Grad` and `Sel`) are drawn
from corner to corner, as in Photoshop. With `Alt` (`Option` on a Mac) held while dragging, the
pixel where the drag started is the shape's centre and the shape reaches as far on every side,
as the Win32 editor drew circles; it can be pressed and released during the drag. The sprite
canvas does the same.

On the frame, ordinary drawing tools edit the active colorized frame.

Hold `Shift` while clicking, drawing a shape or flood filling on the frame to give its pixels
back their original look: each gets the plain original color of its own original shade, the
color an uncolorized frame shows (the amber ramp). The preview shows such a stroke in amber. It
works on every selected frame, each with its own original, and is undone like any drawing.

While a background is edited (`Edit Background` in the `Frame Background` section), ordinary drawing tools edit the selected shared background image. `Pick` samples from that background image, flood fill fills the background image, and the edits are recorded in the history of that background and resolution.

## Point

Click one pixel to paint it with the active draw color (in `Set Replace` mode: the edit color of its
original shade).

## Line

Press, drag, and release to draw a line.

The preview is shown while dragging. The whole line is committed on release as one undoable edit.

## Rect

Press, drag, and release to draw a rectangle outline bounded by the start and end pixels.

The preview is shown while dragging. The whole rectangle is committed on release as one undoable edit.

## Fill Rect

The tool the editor starts with. Press, drag, and release to draw a filled rectangle bounded by
the start and end pixels. A click without a drag paints the one pixel, as `Point` does.

The preview is shown while dragging. The whole filled rectangle is committed on release as one undoable edit.

On the original frame, while `Show Bounds` is on, `Rect` and `Fill Rect` draw the search area of
the selected sprite slot instead (see "Sprites").

## Circle

Press, drag, and release to draw a circle outline.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole circle is committed on release as one undoable edit.

## Fill Circle

Press, drag, and release to draw a filled circle.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole filled circle is committed on release as one undoable edit.

## Ellipse

Press, drag, and release to draw an ellipse outline inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole ellipse is committed on release as one undoable edit.

## Fill Ellipse

Press, drag, and release to draw a filled ellipse inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled ellipse is committed on release as one undoable edit.

## Grad

Press, drag, and release to fill the selected Palette `Shape` with a directional palette gradient.

With a canvas selection on the plane (made with `Sel` or the other selection helpers), a drag
fills the selection at once instead, as the Win32 editor's "Gradient draw" did: on every selected
frame (or the selected background), one undo step, the pixels outside the selection untouched.
The `Pattern` decides how:

- `Linear`: from the drag's start to its end; before the start the first color, behind the end
  the last one.
- `Circle`: around the drag's start, out to the drag's length.
- `Ellipse`: around the drag's start, with the drag's width and height as its radii.

`Direction` `Edge -> Center` turns a circle or ellipse round; the linear direction is the drag's.
While dragging, the line (or the circle or ellipse) is shown. To pick the colors as in the Win32
editor, `Ctrl+drag` (`Cmd+drag` on macOS) across the colors of the `Palette` section: it sets
`From` and `To` in the order of the drag; with `Shift` held the other way round.

The filled pixels sample the actual palette entries from `From` through `To`; the canvas tool does
not recalculate colors between the endpoints. Palette color numbers are displayed as `1` through
`64`, while the saved cRP data remains unchanged internally.

The Palette tab separates gradient behavior into:

- `Shape`: the selected area to fill, currently rectangle, circle, or ellipse.
- `Pattern`: how colors are distributed inside the selected area, currently linear, circle, or ellipse.
- `Direction`: the color order used by the fill. `Free` is a two-step workflow: first draw the shape
  or selection area, then draw a virtual direction line that defines the gradient vector. The fixed
  linear directions ignore the mouse drag direction and use the selected area's bounds instead.
  Circle and ellipse patterns use the selected area's bounds for radial fills; `Edge -> Center`
  reverses radial fills.

This is currently implemented for colorized frame pixels and selected background image pixels. Each
gradient fill is committed as one undoable edit.

## Sel

Use `Sel` to create a persistent canvas selection before applying a palette gradient to that exact
area.

Create a selection:

1. Open the Inspector's `Palette` section.
2. Choose the selection shape in `Shape`: rectangle, circle, or ellipse.
3. Select the toolbar tool `Sel`.
4. Press, drag, and release on the colorized canvas.
5. The selected pixels are shown as a translucent cyan overlay.

`Sel` selects in every mode, also while a mask or the sprite capture is being edited; it never
paints a mask.

Fill a selection with a fixed-direction gradient:

1. Create a selection.
2. In the `Palette` section, set `From` and `To`.
3. Set `Pattern` to `Linear`, `Circle`, or `Ellipse`.
4. Set `Direction` to a fixed direction such as `Left -> Right`, `Top -> Bottom`,
   `Center -> Edge`, or `Edge -> Center`.
5. Click `Grad Sel`.

Fill a selection with a free-direction gradient:

1. Create a selection.
2. In the `Palette` section, set `From`, `To`, and `Pattern`.
3. Set `Direction` to `Free`.
4. Click `Set Dir`.
5. Press, drag, and release on the same canvas to draw the virtual gradient direction line.
6. The stored direction line is shown in the cyan selection overlay.
7. Click `Grad Sel`.

Clear or replace a selection:

1. Click `Clear Sel` to clear the current selection.
2. Or select `Sel` again and draw a new selection; the previous selection and stored direction line
   are replaced.

Selections are cleared when the frame, SD/HD mode, target, selected background, or tool context
changes. Selection gradient fills are undoable through the same frame/background edit histories as
ordinary pixel edits. A selection can also be made and changed with `Ctrl/Cmd`, the `Edit` menu and
the other helpers, see "Selection"; `Grad Sel` then fills the selected pixels over their bounding
box.

## Pick

Click a colorized frame pixel to set the active draw color to that pixel's RGB565 value.

Hold `Shift` while clicking to select the edit color slot whose color is the pixel's color instead
(the last one if several are, as in the Win32 editor); the draw color becomes that edit color. The
status bar says when no edit color has the color. Without `Shift` the Win32 editor writes the
picked color into the selected edit color; here it only becomes the draw color.

When the `Dynamic` overlay is enabled, click a pixel of the colorized frame or of the original
to select the dynamic set stored at that pixel instead. This is useful before editing the set's dynamic colors or shadows.

`Pick` picks once: after the click the tool chosen before it is back, so that a rectangle (or any
other tool) goes on with the picked color.

This tool does not modify project data and does not create an undo entry.

## Flood

Click a colorized frame pixel to flood-fill the contiguous four-neighbor area that matches the clicked pixel's current color.

The fill uses the active draw color. The whole filled area is committed as one undoable edit. In
`Set Replace` mode the area is the same and each pixel takes the edit color of its original shade;
with several frames selected the area comes from the current frame and every selected frame is
filled there with its own shades.

TODO: consolidate frame, mask, background, and sprite drawing behind a shared editable-surface
pipeline so tools, previews, flood fill, undo/redo, and deferred dependent-view refresh are
implemented once and reused across canvas targets.
