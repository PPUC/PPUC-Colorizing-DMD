# Backgrounds

The app loads and saves background data from modern `.cROM` projects.

Currently implemented:

- Shared SD and HD background image data is preserved.
- Per-frame background IDs are preserved.
- Per-frame SD and HD background masks are preserved.
- The project status line shows the loaded background count.
- `Background ID` assigns the shared background for all selected frames that have background data. `None` stores the legacy no-background assignment.
- `Show Background` renders the assigned background layer over original black pixels where the active SD/HD background mask allows it.
  The original pixel looked at is the one the plane pixel belongs to: (x, y) in SD, (x/2, y/2) in
  HD, and (x, y) on the single plane of a project with 256x64 or 192x64 original frames.
- A frame that starts a background scene (see "Scenes") shows the scene's first frame through
  its background mask instead, still, so that nothing moves while you draw; on HD it is upscaled
  as libserum upscales it when the scene frame has no HD picture. While `Play` runs the scene
  plays there, as the color rotations do: its frames of group 1 one after the other, each for
  the scene's frame duration, in a loop. The scene's options show as libserum renders them:
  with `Keep only dynamic content in front` the frame's lit static pixels under the mask show
  the scene too, and with `Black dynamic pixels show the scene` a lit dynamic pixel whose set
  color is black shows it there (and casts no shadow), while an unlit one whose set has a color
  for its shade keeps that color. Such a frame needs no
  background of its own: its mask can be drawn without one, and the editor gives libserum the
  placeholder background libserum needs for it. Opening a project takes the placeholder an
  earlier editor needed off such frames, and the status bar says for how many; the change is
  saved with the project. A background stays where libserum shows it after the scene has
  ended: a scene that is not looped and ends black or on the frame before.
- The Inspector has two background sections, each shown on its own canvas: `Frame Background`
  on the frame canvas (the selected background, `Assign Background`, `Remove Background`, `Frame -> Background`,
  `Background -> Frame`, `Edit Background`) and `Background` while the background is edited (the selected
  background, `Import Image`, `Export Background`, `Create HD`, `Remove HD`, and `Back to Frame`, which
  ends editing as `Edit Background` clicked again does). Entering background editing opens it.
- `Edit Background` edits the current frame's own background; a frame without one, the background
  selected in the section. A double click on a background in the left sidebar edits that one.
- `Edit Background` in the Inspector's `Frame Background` section (or a double click on a background in the
  left sidebar) displays the selected background image on the canvas and edits that shared
  background image with the ordinary drawing tools; the button stays down meanwhile. Click
  `Edit Background` again to go back to the frame, as `Edit Sprite` does for a sprite, or double-click
  a frame in the frame strip to edit that frame.
- `Create HD` on the `Background` section gives the selected background its HD image: its SD
  image scaled to 2x with the filter chosen in the window it opens (the one used last, by any
  `Create HD`, is preselected; `Nearest` at first). While the background is edited, the canvas
  switches to its new HD image. `Remove HD` takes the HD image away. Each is enabled when it
  applies, and both can be undone.
- While a background is edited, `SD` and `HD` choose its SD or HD image, whether or not the
  frame shown has HD data; a background without an HD image is edited in SD. A background also gets its HD image with `Create HD` on frames that use it
  ("Assigned background image") and with `Import Image` on the 256x64 plane.
- The `Background` toolbar toggle shows the current frame's background mask as a semi-transparent yellow overlay on the colorized frame.
- `Background` and `Dynamic` are mutually exclusive edit modes because both write on the colorized frame.
- In SD mode, `Background` edits the SD background mask.
- In HD mode, `Background` edits the HD background mask.
- Click the colorized frame to set that background-mask pixel to `1`. The original frame works
  the same way on its plane (as for dynamic zones).
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD background mask.
- The flood fill takes its area from the frame, not from the mask: the pixels connected to the
  clicked one that have its color in the frame's colorized picture, or its shade when clicked on
  the original frame. The mask is set there (with `Shift`, cleared), whatever it held before.
- Hold `Shift` while clicking, drawing, or flood filling to erase background-mask pixels back to `0`.
- The frame strip follows mask edits: where the background shows changes the frame's thumbnail.
- Background-mask edits are undoable (histories per frame and per resolution; an edit of several
  selected frames is one step).

`New` and `Delete` above the background list of the left sidebar add a background with the picture
of the current frame and delete the selected one, see "Adding And Deleting Records".
Background import/copy operations replace the image of an existing background.
