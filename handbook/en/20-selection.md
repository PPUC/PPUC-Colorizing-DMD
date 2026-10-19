# Selection

The selection is a set of pixels of the plane being edited (the Win32 editor's copy mask). It is
shown as the translucent cyan overlay of `Sel` and is what `Grad Sel`, `Copy`, `Auto Copy...`,
`Black Original To Black` and the dynamic helpers work on. It is editor state, not project data:
it is not saved, it is not part of Undo (the Win32 editor records its changes in its one undo
buffer; here a selection step would sit between the edits of the frame's histories and make
Undo skip over invisible steps, and every selection command is one click to repeat). As in the
Win32 editor it stays when another frame is shown, so it can be checked on each frame of a
selection of frames before copying or pasting (a click on a frame of that selection keeps it, see
"Frame List"). It is dropped when the SD/HD mode, the paint target or the selected background
changes.

Making and changing a selection on the frame canvas (frame pixels or, with `Target` `Background`,
the selected background image). It works the same in the `Dynamic` and `BG` modes, on the
frame's picture: there `Ctrl/Cmd`+click selects instead of painting the zone or the mask, and
`Select All` and the `Sel` tool work as without them. Only the `Mask` mode, whose masks are
painted on the original frame, has no selection. The `Shape` of the `Sel` tool is chosen in the
`Palette` section, also when no palette is selected:

- `Sel` tool: drag a rectangle, circle or ellipse (the `Shape` of the `Palette` section); it replaces
  the selection.
- `Ctrl/Cmd`+drag with `Line`, `Rect`, `Fill Rect`, `Circle`, `Fill Circle`, `Ellipse` or
  `Fill Ellipse`: the shape's pixels join the selection. `Ctrl/Cmd`+`Shift`+drag removes them.
- `Ctrl/Cmd`+click with `Point`: the pixel joins (with `Shift`: leaves) the selection.
- `Ctrl/Cmd`+click with `Flood`: the connected pixels of the clicked color join (with `Shift`:
  leave) the selection.
- Hold `W` while selecting, or check `Edit -> Select Non-Black Pixels Only`, and black pixels
  (stored color 0) stay out of what is added, as in the Win32 editor.
- `Ctrl/Cmd`+`Alt`+click with `Flood`, or `Edit -> Select By Color` and then a click: every pixel of
  the clicked color joins the selection, with `Shift` leaves it. The color is the one the canvas
  shows (with dynamic content, sprites and, when shown, the background layer); the Win32 editor
  compares stored colors and leaves dynamic pixels out.
- `Edit -> Select All` (`Ctrl/Cmd+A`), `Select None` (`Ctrl/Cmd+Shift+A` or `Ctrl/Cmd+D`), `Invert
  Selection` (`Ctrl/Cmd+Shift+I`).
- `Edit -> Selection From Background Mask` (`Ctrl/Cmd+B`): the current frame's background mask on
  the plane; the frame needs a background.
- `Edit -> Mirror Selection Horizontally` (`Ctrl/Cmd+K`) and `Vertically` (`Ctrl/Cmd+L`).
- `Edit -> Auto Select...`: stripes of rows or columns: from `Offset` on, `Selected` lines are
  selected and `Skipped` lines are not, to the edge of the plane (Win32 limits: offset 0 to 256,
  1 to 256 lines each). It replaces the selection.

A selection edited in any of these ways is a set of pixels, not a shape: `Grad Sel` spreads its
gradient over the selection's bounding box. On macOS the `Control` key works as well as `Cmd` for
selecting (a `Control`+click can also be the system's right click, hence `Cmd` first).
