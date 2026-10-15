# Preview Rendering

libserum is the source of truth for every runtime-composed pixel. There are two renderers:

- The **Runtime view**, runtime playback and `Export Current Frame PNG...` show libserum's own
  output, see "Runtime View".
- The **editing canvas and the thumbnails** use the editor's own renderer, which draws the
  layers being edited and has to stay cheap. It follows libserum's
  rules and is checked against libserum by automated tests on constructed projects of all three
  layouts and by `colorizing_qt_v2_preview_diff` on real projects (see "Testing Checklist").

The rules the editor's renderer follows, in libserum's order:

- Frame pixels are resolved from background, static and dynamic data in one scan. The
  background shows where its mask is set and the original is 0; a dynamic zone takes its set's
  color for the original's shade.
- A lit dynamic pixel casts its set's shadow onto its neighbors in the set's directions. A
  shadow never covers lit dynamic content, and the first shadow on a pixel stays.
- Sprites are looked for on the original frame and drawn after the frame and its shadows, in
  the order they were found. The rules are listed under "Sprites".
- In a project with 256x64 original frames, a frame without its 256x64 data is black, and
  libserum's rule for the 256x64 dynamic data of sprites applies: sprite number N keeps it only
  if frame number N has 256x64 data.

On the plane that has the size of the original frame (128x32 in an ordinary project, 256x64 and
192x64 in the other two layouts) the editor's renderer and libserum give the same pixels, with
one intended exception: in a project with 256x64 original frames, a frame whose background or
sprite lacks its 256x64 data is still drawn without it, so that it can be edited, while libserum
shows nothing for the frame. Last
measured: `afm_113b` 3047 of 3047 frames, `trn_174h` 5211 of 5211, `hook_501` 3897 of 3897,
`smb3` 4430 of 4430, `frankst1` 303 of 303 sampled frames.

On the HD plane of an ordinary project the editor's renderer follows libserum's layers: the HD
frame and HD background as they are, and on top the dynamic zones of the SD plane with the SD
colors (the stored HD dynamic mask and colors are not used, see "Dynamic Masks"), an unlit zone
pixel under the SD background mask left to the HD picture, and shadows generated on the HD plane,
one pixel wide, from the SD shadow tables with the HD tables as a fallback per set. An automated
test compares the whole HD plane with libserum for a frame with HD statics, zones, shadows and a
background, built of rows so that libserum's upscale has nothing to round, and checks that other
stored HD dynamic masks and colors change nothing.

Known differences, all on the HD plane of an ordinary project. libserum upscales what it renders
at 128x32 once (Scale2x) before putting it on top. The editor doubles pixels instead.

- **Edges of dynamic zones.** Scale2x rounds corners: a pixel at the edge of a glyph gets the
  color of a neighboring 128x32 pixel, or is left to the HD frame below. This is all that is left
  of the dynamic differences: in `trn_174h` 113940 pixels in 4222 of 5211 frames, 27 per frame,
  every one of them such an edge pixel (before the editor used the SD zones: 151384). Shadows
  follow the rounded shape in libserum and the doubled one in the editor.
- **Edges of sprites and backgrounds.** libserum reads the original frame upscaled where it
  decides between background and frame, and rounds the edges of upscaled sprites. The editor
  doubles pixels: a sprite without HD is its 128x32 picture with every pixel doubled, and an HD
  sprite keeps the doubled 128x32 pixels where the sprite is dynamic. Single pixels differ along
  such edges, for example `trn_174h` frames 1455 and 697 (782 pixels in all).
- **Frames without HD** have no HD plane in the editor at all. libserum's upscaled 64-row
  picture of them is only in the Runtime view.
- **A frame with HD whose background has no HD image** is, by libserum's code
  (`HasHdStaticContent()`), rendered like a frame without HD, upscaled from 128x32. The editor
  shows the HD frame without the background. Not measured: none of the real projects has one.
- **HD pixels painted under a stored HD dynamic mask** are saved as 0, see "Saving". The canvas
  shows the painted color until the project is reopened.

For all of these the Runtime view with `Runtime Plane: 64 Rows` shows the real picture.
