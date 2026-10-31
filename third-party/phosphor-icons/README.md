# Phosphor Icons (vendored)

The icons of the drawing tools in the toolbar. MIT license, see `LICENSE`.

- `svg/`: the icons of https://github.com/phosphor-icons/core, package `@phosphor-icons/core`
  version 2.1.1, unchanged (`assets/regular` and `assets/fill`), except `ellipse.svg` and
  `ellipse-fill.svg`: Phosphor has no ellipse, so these two are the editor's own, in the same
  grid and line width.
- `png/`: those icons rendered at 128x128 with `QSvgRenderer`, in black on transparent. The
  editor builds these in and colors them for the theme, so that it needs no Qt module besides
  Qt Base.

For a new icon, copy its SVG to `svg/`, render it to `png/` and list it in `app/CMakeLists.txt`.
