# Backgrounds

The app loads and saves background data from modern `.cROM` projects.

Currently implemented:

- Shared SD and HD background image data is preserved.
- Per-frame background IDs are preserved.
- Per-frame SD and HD background masks are preserved.
- The project status line shows the loaded background count.
- `BG ID` assigns the shared background for all selected frames that have background data. `None` stores the legacy no-background assignment.
- `Show BG` renders the assigned background layer over original black pixels where the active SD/HD background mask allows it.
  The original pixel looked at is the one the plane pixel belongs to: (x, y) in SD, (x/2, y/2) in
  HD, and (x, y) on the single plane of a project with 256x64 or 192x64 original frames.
- Inspector `Target -> Background` displays the selected background image on the canvas and edits that shared background image with the ordinary drawing tools.
- The `BG` toolbar toggle shows the current frame's background mask as a semi-transparent yellow overlay on the colorized frame.
- `BG` and `Dynamic` are mutually exclusive edit modes because both write on the colorized frame.
- In SD mode, `BG` edits the SD background mask.
- In HD mode, `BG` edits the HD background mask.
- Click the colorized frame to set that background-mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD background mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase background-mask pixels back to `0`.
- Background-mask edits are undoable per frame and per resolution.

`New` and `Delete` above the background list of the left sidebar add a background with the picture
of the current frame and delete the selected one, see "Adding And Deleting Records".
Background import/copy operations replace the image of an existing background.
