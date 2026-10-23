# Palettes

Legacy `.cRP` files can store 256 palettes with 64 RGB565 colors each, plus 64-byte palette-name
slots. When that table exists, the editor loads it into the project and saves it, with every
change, in the project file.

The Inspector's `Palette` section provides a palette selector, the selected palette name, and a
64-color grid:

- Edit the name field to rename the selected palette. Names are capped at 63 characters so they fit
  the fixed cRP name slot.
- Click any palette color to make it the active drawing color.
- Drag any palette color onto one of the four toolbar favorite color slots to replace that favorite,
  or onto an edit color swatch under the toolbar to set that edit color (undoable).
- Double-click any palette color to open a color picker for that RGB565 palette entry.
- Palette color cells and `From`/`To` controls are displayed as `1` through `64`.
- Use `From`, `To`, and `Apply Gradient` to fill a palette range by interpolating between the
  endpoint colors. Reversing `From` and `To` reverses the generated palette range. The red, green,
  and blue RGB565 components are interpolated separately and rounded to the nearest value; the two
  endpoint colors themselves are never changed.
- Use `Shape`, `Pattern`, and `Direction` to control the canvas `Grad` drawing tool.
- Palette color changes and gradient fills are undoable per palette.
- Palette color/name changes mark the project dirty and are saved in the project file.

The current frame renderer does not yet use these palettes for color-rotation playback or gradient
fill previews outside the Palette editor. Palette editing is the storage/UI foundation for those
next steps.
