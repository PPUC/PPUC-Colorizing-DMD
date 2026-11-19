# Painting A Frame

## Choosing colors

The draw color is the box at the end of the toolbar's favorite colors. Several places give it a
new color:

- one of the four **favorites** in the toolbar;
- a color of the **palette** in the Inspector's `Palette` section, 64 colors per palette;
- an **edit color** in the row under the toolbar;
- the `Pick` tool, which takes the color of a pixel on the canvas.

A double click on the draw color, a favorite, an edit color or a palette color opens the color
picker. Colors are dragged from one place to another: a palette color onto an edit color, the
draw color onto a palette color.

## Two ways to paint

The two buttons at the start of the edit color row choose how pixels get their colors.

- **`1 Color`**: every painted pixel takes the draw color. For details and single pixels.
- **`Set Replace`**: every painted pixel takes the edit color of its own original shade. A pixel
  whose original has shade 2 becomes edit color 2. The edit colors are a color for each shade,
  so one stroke over a whole area keeps its light and dark parts, only in your colors.

`Set Replace` is the faster way for most of a frame. Set the edit colors for, say, a red
character (dark red to light red), paint over the character, then set them for the next object.

A gradient across the edit colors is quick: hold `Shift`, press on the darkest edit color, drag
to the lightest and let go. The colors between blend from one to the other. The same works in
the palette, the dynamic colors and the color rotations. A plain drag of one color onto another
swaps the two.

The `Color Sets` section stores a row of edit colors under a name (`Store`) and brings it back
(`Load`, or a double click). Keep a set per object: the red character, the blue score, the
yellow logo.

## The tools

The tools are in the toolbar, from left to right: `Point`, `Line`, `Rect`, `Fill Rect`,
`Circle`, `Fill Circle`, `Ellipse`, `Fill Ellipse`, `Grad`, `Sel`, `Pick` and `Flood`. Hold the
mouse over one for its name.

- `Point` paints a pixel, or a line along the mouse while dragging.
- The shapes are drawn from corner to corner; with `Alt` (`Option`) held, from the centre.
- `Flood` fills the pixels connected to the clicked one that have its color.
- `Grad` paints a gradient of palette colors (`From` and `To` in the `Palette` section) into a
  shape.
- `Sel` selects pixels (see the next chapter).

Before a click, the pixel under the mouse shows what the click would paint.

With `Shift` held, a click, a shape or a fill gives the pixels their original look back: the
plain amber color of their shade. That is how you take colors away again.

## Painting several frames at once

An animation is often a row of frames that differ only in a small part. Select them in the
strip (`Shift`+click the first and the last) and paint: the stroke goes on every selected frame.
In `Set Replace` mode each frame takes the colors of its own original shades, so the same stroke
colors each frame correctly even where the pictures differ.

One stroke on many frames is one step for undo. See "Undo".

## A first frame, step by step

1. Pick a frame that shows a large, simple picture, for example a logo in attract mode.
2. Set the edit colors for its main color: a dark and a light color, then the gradient between
   them with `Shift`+drag.
3. Choose `Set Replace` and `Fill Rect`, and drag a rectangle over the logo.
4. Switch to `1 Color`, pick a contrasting color, and paint details with `Point`.
5. Step to the next frames with `Right`. Frames that show the same logo, moving or fading, can be
   selected together and painted in one go.

Handbook: "Colors", "Drawing Tools", "Palettes", "Recolor Helpers".
