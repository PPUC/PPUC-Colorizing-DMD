# Backgrounds

The app loads and saves background data from modern `.cROM` projects.

Currently implemented:

- Shared SD and HD background image data is preserved.
- Per-frame background IDs are preserved.
- Per-frame SD and HD background masks are preserved.
- The project status line shows the loaded background count.
- `Background ID` assigns the shared background for all selected frames that have background data. `None` stores the legacy no-background assignment.
- `Show BG` renders the assigned background layer over original black pixels where the active SD/HD background mask allows it.
  The original pixel looked at is the one the plane pixel belongs to: (x, y) in SD, (x/2, y/2) in
  HD, and (x, y) on the single plane of a project with 256x64 or 192x64 original frames.
- `Edit BG` in the Inspector's background section (or a double click on a background in the
  left sidebar) displays the selected background image on the canvas and edits that shared
  background image with the ordinary drawing tools; the button stays down meanwhile. Click
  `Edit BG` again to go back to the frame, as `Edit Sprite` does for a sprite, or double-click
  a frame in the frame strip to edit that frame. The `Target` field of the frame section
  (`Frame` or `Background`) does the same.
- The `BG` toolbar toggle shows the current frame's background mask as a semi-transparent yellow overlay on the colorized frame.
- `BG` and `Dynamic` are mutually exclusive edit modes because both write on the colorized frame.
- In SD mode, `BG` edits the SD background mask.
- In HD mode, `BG` edits the HD background mask.
- Click the colorized frame to set that background-mask pixel to `1`. The original frame works
  the same way on its plane (as for dynamic zones).
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD background mask.
- The flood fill takes its area from the frame, not from the mask: the pixels connected to the
  clicked one that have its color in the frame's colorized picture, or its shade when clicked on
  the original frame. The mask is set there (with `Shift`, cleared), whatever it held before.
- Hold `Shift` while clicking, drawing, or flood filling to erase background-mask pixels back to `0`.
- The frame strip follows mask edits: where the background shows changes the frame's thumbnail.
- Background-mask edits are undoable per frame and per resolution.

`New` and `Delete` above the background list of the left sidebar add a background with the picture
of the current frame and delete the selected one, see "Adding And Deleting Records".
Background import/copy operations replace the image of an existing background.
