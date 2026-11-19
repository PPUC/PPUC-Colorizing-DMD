# Backgrounds And Color Rotations

## Backgrounds

Many frames show something in front of empty, dark space: text, a score, a character. A
background fills that dark space with a picture: a starfield, a gradient, a landscape. It is
stored once and shared by every frame that uses it.

A frame shows its background only where two things are true: its original pixel is black, and
its background mask lets the background through. The mask is how you keep the background out of
places that should stay black, the inside of a letter for example.

### Making one

1. Go to a frame whose colored picture is a good start, and press `New` above the left
   sidebar's `Backgrounds` list: a new background of the current frame's picture.
   `Frame -> Background` in the Inspector's `Frame Background` section copies a frame into the
   selected background later.
2. `Edit Background` (or a double click on the background in the list) shows the background on
   the canvas. Paint it with the ordinary tools, or bring in a picture with `Import Image` in the
   `Background` section.
3. `Back to Frame` (or `Edit Background` again) returns to the frame.

### Using it

1. Select the frames and press `Assign Background`, or drag the background from the list onto
   the canvas.
2. Press `Background` in the toolbar. The background mask shows in yellow on the colorized frame.
3. Paint the mask where the background may show; `Shift` erases. `Flood` takes its area from the
   frame's picture, so a click into a dark area masks exactly that area.
4. Press `Background` again to go back to colors.

`Show Background` in the `Frame` section shows the background on the canvas as you paint.

## Color rotations

A color rotation makes colors cycle while a frame is shown: a shine running along a logo, flames
that flicker, lights that chase. Each frame has four rotation slots. A slot has a list of colors
and a delay; at every step each pixel whose color is in the list takes the next color of the
list.

### Making one

1. Paint the area with a row of colors, for example a gradient in the palette painted with
   `Grad`.
2. In the `Color Rotations` section choose a `Slot`, set its `Length` (the number of colors) and
   its `Delay` in milliseconds.
3. Give the slot the same colors in the same order: click the colors of the grid, or `Shift`+drag
   from the first to the last for a gradient.
4. Press `Stay` beside `Play` to watch it on the current frame.

A rotation moves only pixels whose color is exactly one of the slot's colors. The line below the
grid says how many of the frame's pixels that is. If it is none, the frame was painted with other
colors than the slot holds.

Dynamic content does not rotate: a score in a dynamic zone keeps its colors even over a rotating
background.

Handbook: "Backgrounds", "Color Rotations", "Palettes", "Playback".
