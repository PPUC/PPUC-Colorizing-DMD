# ColorizingDMD Qt Rewrite v2 Handbook

This handbook describes the implemented behavior of `qt_rewrite_v2`.
Update it in the same change set whenever a user-visible feature changes.

## Project Files

The Qt rewrite edits legacy Serum editor projects directly:

- `.cROM` stores colorized frame data and runtime project data.
- `.cRP` stores original/reference frames and editor-side metadata such as sprite names and frame durations.
- `.cROMc` is not edited directly.

Saving writes back to the existing `.cROM` path and updates supported fields in the matching `.cRP` sidecar without changing the legacy file layout.

The in-memory project model currently carries structured comparison-mask data from `.cROM`:

- Per-frame shape comparison mode.
- Per-frame comparison mask ID.
- SD comparison mask bitmaps.

It also carries structured dynamic-mask data from `.cROM`:

- Per-frame SD and HD dynamic mask bitmaps.
- Per-frame SD and HD dynamic color sets.
- Per-frame SD and HD color rotation slots.

It also carries structured background data from `.cROM`:

- Shared SD and HD background images.
- Per-frame background assignments.
- Per-frame SD and HD background masks.

## Opening Projects

Use `File -> Open Project...` and select a `.cROM` file.

The app looks for a matching `.cRP` next to it with the same base name. If it cannot find that file, it asks for the `.cRP` path.

After a successful open, the project is added to `File -> Open Recent`.
Recent-project entries show the full `.cROM` path and internally remember both the `.cROM` and `.cRP` paths.

## Saving

Use `File -> Save` or the standard Save shortcut.

Use `File -> Save As...` to create a copied project. The app asks for a target `.cROM` path,
copies the current `.cROM` and matching `.cRP` to the new base name, then saves the current
in-memory edits into the copied `.cROM`.

Use `File -> Export Current Frame PNG...` to export the active colorized frame preview as a PNG.
The export uses the active SD/HD mode, falls back to SD when the selected frame has no HD data, and
includes the assigned background layer when `Show BG` is enabled.

Use `File -> Import Image Into Selected Frames...` or the Frame topic's `Import Image` button to
replace selected frame pixels from a PNG/BMP/JPEG. The image is scaled to the active SD/HD DMD size.
In HD mode, selected frames without HD data are skipped. The import is undoable per frame.

Save behavior:

- Updates colorized SD and HD frame pixels in the existing `.cROM`.
- Updates shape comparison modes, comparison-mask assignments, and comparison-mask bitmap data.
- Preserves loaded SD/HD dynamic masks and dynamic color sets.
- Preserves loaded SD/HD background images, per-frame background assignments, and background masks.
- Updates HD-present flags.
- Preserves cROM data that is not edited yet, including trailing data.
- Marks the document clean after successful save.

Use copied projects for testing until broader project metadata editing and save coverage are implemented.

## Playback

The top toolbar contains editor-only playback controls:

- `Play` advances through the currently visible frame list.
- `Pause` stops playback.
- `Loop` restarts at the first visible frame after the last visible frame.
- `cRP` uses the per-frame durations loaded from the `.cRP` file.
- The interval field controls the fallback frame advance interval in milliseconds.

Playback respects the current frame filter in the left `Frames` tab. When `cRP` timing is enabled,
frames with missing, zero, or unusually large durations use the fallback interval.

During playback, the central preview advances color-rotation slots for the displayed frame using
each slot's stored delay. This is an editor preview; libserum remains the runtime source of truth for
final composition order.

Rotation timing is not a visual preference. The per-slot delay stored in the project controls both
runtime behavior and editor playback, so future rotation-editing UI must keep timing edits explicit
and must be validated against libserum playback behavior.

## Frame List

The left frame list shows one thumbnail per loaded frame plus compact metadata.

Frame items show the project frame number, the loaded cRP duration, and compact flags such as
`HD`, `Dyn`, assigned `BG`, and sprite assignment count when present.
Selecting a frame updates the central canvas and the frame status line.

The filter above the frame list can show all frames, only frames with HD data, frames with an
assigned background, frames with dynamic data, or frames with sprite assignments. Editing still
targets the real project frame number shown in the item label, not the visible row number.

The frame list supports native multi-selection. Frame drawing, comparison-mask drawing,
dynamic-mask drawing, background-mask drawing, and background assignment apply to all selected
frames where the operation is valid. Comparison-mask edits are grouped by assigned mask ID, so a
shared mask is edited once even if multiple selected frames use it. Undo/redo history is still
stored per frame, per resolution, or per shared mask as appropriate.

## SD And HD Modes

Use the `SD` and `HD` toolbar buttons to switch the active editing resolution.

Rules:

- SD edits target 128x32 colorized frame pixels.
- HD edits target 256x64 colorized frame pixels.
- If a frame has no HD data, the canvas falls back to SD display/editing.
- Canvas hit testing maps mouse coordinates explicitly to SD or HD pixel coordinates.
- The Inspector `Create HD` button creates HD data for the current frame by scaling the SD colorized frame to 2x.
- When creating HD, a dialog asks whether to also upscale existing dynamic masks/colors, the frame background mask, and the assigned background image.
- The Inspector `Remove HD` button removes HD data from the current frame and switches the canvas back to SD when needed.

TODO: offer selectable scaling algorithms for HD creation instead of always using nearest-neighbor 2x scaling.

## Canvas

The central canvas shows:

- The active colorized frame region.
- The original/reference frame region below it.

The canvas does not draw text labels or white frame outlines over the image data.

Drawing previews are transient overlays. They do not modify project data until mouse release or click commit.

Use the `Mask` toolbar toggle to show the current frame's assigned comparison mask over the original/reference region.
When `Mask` is active, clicks on the original/reference region add pixels to the assigned comparison mask.

Use the `Dynamic` toolbar toggle to show and edit dynamic-mask pixels over the active colorized frame.
The dynamic overlay and edits follow the active SD/HD mode.

Use the `BG` toolbar toggle to show and edit the current frame's background-mask pixels over the active colorized frame.
The background-mask overlay and edits follow the active SD/HD mode.

The left sidebar is the visual component browser:

- `Frames` shows frame thumbnails and the frame filter.
- `Masks` shows comparison-mask thumbnails; selecting one assigns it as the current frame's mask.
- `Backgrounds` shows background thumbnails; selecting one selects the same background in the Inspector for assignment or editing.
- `Sprites` shows sprite thumbnails; selecting one selects the same sprite in the Inspector.

Frame metadata, dynamic colors, and sprite controls live in selectable topics in the right-side Inspector:

- `Frame` contains frame/background assignment, trigger IDs, DMD render settings, and HD frame actions.
- `Mask` contains comparison-mask assignment, comparison mode, and a shortcut into mask editing.
- `Background` contains the selected background ID, assignment, and edit shortcut.
- `Dynamic` contains the dynamic set selector and dynamic color swatches.
- `Rotation` contains color rotation slots for the current frame and active SD/HD mode.
- `Palette` contains the selected cRP palette name and all 64 palette colors.
- `Sprites` contains sprite selection, naming, assignment, bounds, detection areas, sprite editing, and sprite previews.

The `Frame` topic exposes:

- `Target` selects whether ordinary drawing tools edit the current frame or the selected background image.
- `BG ID` assigns the shared background used by the current frame.
- `Trigger ID` edits the per-frame legacy trigger ID for all selected frames.
- `Duration` edits the per-frame `.cRP` duration for all selected frames.
- `Switch mono` writes the legacy switch-to-monochrome trigger ID `65432` and disables manual trigger-ID entry while checked.
- `DMD Shape` selects square pixels or round dot pixels for the main canvas and PNG export.
- `DMD Size` sets the relative pixel fill size in 10% steps. Values below `100%` leave black spacing between pixels, which makes the DMD grid visible.
- `Show BG` renders the assigned background layer into the colorized frame preview and is enabled
  by default.
- `Import Image` replaces selected frame pixels from an image file in the active SD/HD mode.

`DMD Shape` and `DMD Size` are view/export settings only. They do not change project data or the cROM/cRP file format.

The `Background` topic exposes:

- Backgrounds are selected visually from the left sidebar.
- The tab shows the selected background ID.
- `Assign BG` assigns the selected background to all selected frames.
- `Edit BG` switches ordinary drawing tools to the selected background image.
- `Frame -> BG` replaces the selected background image with the current frame image in the active SD/HD mode.
- `BG -> Frame` copies the selected background image into all selected frames in the active SD/HD mode.
- `Import Image` replaces the selected background image from a PNG/BMP/JPEG, scaled to the active SD/HD DMD size.
- `Export BG` writes the selected background image as a PNG using the current DMD dot/square render settings.

## Colors

The toolbar contains fixed swatches:

- White `0xffff`
- Red `0xf800`
- Green `0x07e0`
- Blue `0x001f`
- Black `0x0000`

The active draw color can also be set with the `Pick` tool. If the picked RGB565 color is not one of the fixed swatches, no swatch is checked, but the picked color is still used by later drawing tools.
The read-only color box next to the fixed swatches always shows the current active draw color.

## Drawing Tools

Drawing tools operate on the active SD/HD mode.

When Inspector `Target` is `Frame`, ordinary drawing tools edit the active colorized frame.

When Inspector `Target` is `Background`, ordinary drawing tools edit the selected shared background image. `Pick` samples from that background image, flood fill fills the background image, and undo/redo is scoped per background and per resolution.

### Point

Click one pixel to paint it with the active draw color.

### Line

Press, drag, and release to draw a line.

The preview is shown while dragging. The whole line is committed on release as one undoable edit.

### Rect

Press, drag, and release to draw a rectangle outline bounded by the start and end pixels.

The preview is shown while dragging. The whole rectangle is committed on release as one undoable edit.

### Fill Rect

Press, drag, and release to draw a filled rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled rectangle is committed on release as one undoable edit.

### Circle

Press, drag, and release to draw a circle outline.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole circle is committed on release as one undoable edit.

### Fill Circle

Press, drag, and release to draw a filled circle.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole filled circle is committed on release as one undoable edit.

### Ellipse

Press, drag, and release to draw an ellipse outline inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole ellipse is committed on release as one undoable edit.

### Fill Ellipse

Press, drag, and release to draw a filled ellipse inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled ellipse is committed on release as one undoable edit.

### Grad

Press, drag, and release to fill the selected Palette `Shape` with a directional palette gradient.

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

### Pick

Click a colorized frame pixel to set the active draw color to that pixel's RGB565 value.

When the `Dynamic` overlay is enabled, click a colorized frame pixel to select the dynamic set
stored at that pixel instead. This is useful before editing the set's dynamic colors or shadows.

This tool does not modify project data and does not create an undo entry.

### Flood

Click a colorized frame pixel to flood-fill the contiguous four-neighbor area that matches the clicked pixel's current color.

The fill uses the active draw color. The whole filled area is committed as one undoable edit.

TODO: consolidate frame, mask, background, and sprite drawing behind a shared editable-surface
pipeline so tools, previews, flood fill, undo/redo, and deferred dependent-view refresh are
implemented once and reused across canvas targets.

## Undo And Redo

Undo and redo are scoped per frame and per resolution.

Examples:

- Edits on frame 10 SD have a separate history from frame 10 HD.
- Edits on frame 10 SD have a separate history from frame 11 SD.
- Switching frame or SD/HD mode updates whether Undo and Redo are available for that context.

New edits clear redo only for the active frame/resolution history.

When `Mask` is active, Undo and Redo operate on comparison-mask edits instead of colorized-frame edits.
Mask edit history is scoped per comparison mask, so frames sharing the same assigned mask also share that mask's edit history.

## Comparison Masks

The `Mask` toolbar toggle shows the assigned comparison mask as a semi-transparent red overlay on the original/reference region.

The Inspector exposes per-frame comparison metadata:

- The `Mask` topic shows the visually selected mask from the left sidebar.
- `Assign Mask` assigns the selected mask to all selected frames.
- `Edit Mask` enables the mask overlay and returns the canvas to the frame preview.
- `Mask ID` assigns the shared comparison mask used by the current frame. `None` stores the legacy no-mask assignment.
- `Shape` selects `Full comparison` or `Shape mode`.
- Both fields are saved back into `.cROM`.

Editing rules:

- Mask editing is active only while the `Mask` toggle is on.
- Colorized-frame drawing is paused while the `Mask` toggle is on.
- Click the original/reference region to set that mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill also edit the assigned mask from the original/reference region.
- Shape tools show a red live preview while drawing on the mask.
- Hold `Shift` while clicking, drawing, or flood filling the original/reference region to erase mask pixels back to `0`.
- Shift-erase shape tools show a dark live preview.
- Edits apply to the assigned comparison mask, not just the current frame.
- If the current frame has no valid comparison mask assignment, clicking the original/reference region does nothing and shows a short status message.
- Mask pixels are saved back into `.cROM`.

## Dynamic Masks

The `Dynamic` toolbar toggle shows the current frame's dynamic-mask pixels as a semi-transparent cyan overlay.

Rules:

- The `Dyn Set` field in the Inspector's `Dynamic` topic selects the dynamic set written by drawing tools.
- The Inspector shows editable dynamic color swatches for the active frame, active SD/HD mode, and selected dynamic set.
- Click a dynamic color swatch to choose a replacement RGB565 color for that original color index.
- In SD mode, the SD dynamic mask is rendered over the SD colorized frame.
- In HD mode, the HD dynamic mask is rendered over the HD colorized frame.
- `Mask` and `Dynamic` are mutually exclusive overlay modes.
- Click the colorized frame to set that dynamic-mask pixel to the selected `Dyn Set`.
- With the `Pick` tool, click the colorized frame to select the dynamic set stored under the cursor.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD dynamic mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase dynamic-mask pixels back to `255`.
- Dynamic-mask edits are undoable per frame and per resolution.
- Dynamic color changes are undoable per frame and per resolution.
- Dynamic shadow direction and color changes are undoable per frame and per resolution.

Dynamic shadows are loaded from and saved back to the legacy `.cROM` dynamic-shadow blocks. The
canvas, thumbnails, and PNG export render shadows after dynamic-color replacement and before sprite
composition. Shadow pixels are written only into neighboring pixels that are not themselves dynamic,
matching the legacy renderer behavior.

The Inspector's `Dynamic` topic provides shadow controls for the active dynamic set:

- Direction buttons `TL`, `T`, `TR`, `L`, `R`, `BL`, `B`, and `BR` toggle where shadows are cast.
- `Shadow Color` opens a color picker for the active set's shadow color.
- Shadow edits apply to all selected frames that have dynamic data in the active SD/HD mode.

## Color Rotations

Modern `.cROM` projects store four color-rotation slots per frame and per SD/HD resolution. Each
slot contains a length, a delay in milliseconds, and up to 62 RGB565 colors.

The Inspector's `Rotation` topic edits the active frame's rotation slots for the current SD/HD mode:

- `Slot` selects one of the four rotation slots.
- `Length` sets the number of active colors in that slot. A length of `0` disables the slot.
- `Delay` sets the milliseconds between rotation steps.
- The color grid shows all colors in the selected slot at once. Active colors can be clicked to
  open a color picker; colors outside the active length remain visible but disabled.
- `Clear Slot` resets the selected slot to all zeros.

Length, delay, grid color edits, and clear operations apply to all selected frames. Rotation data is
loaded from and saved back to `.cROM`. The static canvas shows unrotated frame data; playback applies
an editor-side rotation preview.

## Palettes

Legacy `.cRP` files can store 256 palettes with 64 RGB565 colors each, plus 64-byte palette-name
slots. When that table exists, the Qt rewrite loads it into the document model and saves edits back
to the same `.cRP` offsets.

The Inspector's `Palette` topic provides a palette selector, the selected palette name, and a
64-color grid:

- Edit the name field to rename the selected palette. Names are capped at 63 characters so they fit
  the fixed cRP name slot.
- Click any palette color to make it the active drawing color.
- Drag any palette color onto one of the four toolbar favorite color slots to replace that favorite.
- Double-click any palette color to open a color picker for that RGB565 palette entry.
- Palette color cells and `From`/`To` controls are displayed as `1` through `64`.
- Use `From`, `To`, and `Apply Gradient` to fill a palette range by interpolating between the
  endpoint colors. Reversing `From` and `To` reverses the generated palette range.
- Use `Shape`, `Pattern`, and `Direction` to control the canvas `Grad` drawing tool.
- Palette color changes and gradient fills are undoable per palette.
- Palette color/name changes mark the project dirty and are saved with the `.cRP` file.

The current frame renderer does not yet use these palettes for color-rotation playback or gradient
fill previews outside the Palette editor. Palette editing is the storage/UI foundation for those
next steps.

## Sprites

Existing sprite data is loaded into the document model and saved back through the legacy `.cROM`
layout without changing record counts or binary structure.

Currently preserved sprite data includes:

- Sprite names from the `.cRP` sidecar, when available.
- Sprite original masks.
- SD and HD sprite color images.
- HD sprite masks.
- Sprite detection dwords, positions, and areas.
- Per-frame sprite assignments and bounding boxes.

The Inspector has a sprite browser and assignment controls:

- The sprite selector lists loaded sprite records and shows `.cRP` sprite names when available.
- The sprite name field edits the selected sprite name. Names are saved back to the fixed-size
  `.cRP` sprite-name table when that table exists.
- The first preview shows the sprite original mask.
- The second preview shows the sprite colorized image for the active SD/HD mode. HD is used only when the sprite has HD data.
- `Show Det Areas` overlays valid per-sprite detection areas on the original sprite preview.
- The detection area selector and `X`, `Y`, `W`, `H` fields edit the selected sprite detection area.
- `Clear Area` disables the selected sprite detection area using the legacy `0xffff` marker.
- `Edit Sprite` switches the main canvas to a large sprite editor with colorized sprite on top and original sprite below.
- While `Edit Sprite` is active, `Create HD Sprite` creates HD sprite mask/color data by scaling the SD sprite data to 2x, and `Remove HD Sprite` clears the sprite HD-present flag.
- In the sprite editor canvas, drag on the colorized sprite to repaint existing non-black sprite pixels with the active draw color.
- Hold `Shift` while dragging on the colorized sprite to erase existing non-black sprite pixels back to black.
- The sprite editor supports the toolbar point, line, rectangle, filled rectangle, circle, filled circle, ellipse, and filled ellipse tools on the colorized sprite.
- The sprite editor also supports the toolbar pick and flood-fill tools on the colorized sprite.
- Sprite shape tools show a live preview and commit the full stroke on mouse release.
- Sprite pixel edits update the sprite editor immediately. Frame thumbnails, composed frame preview,
  and side preview images refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Undo and redo for sprite color edits are scoped per sprite and per active SD/HD resolution, and
  are available while the sprite editor canvas is active.
- Drag on the lower original sprite canvas to set the selected detection area.
  Detection-area edits update the sprite editor immediately. Frame thumbnails and composed frame
  previews refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Sprite source/original pixels are not freehand editable.
- `Capture` copies original and colorized pixels from the current frame into a sprite, using the
  selected sprite slot bounds as the source rectangle. Captured pixels are placed at the sprite
  origin, black original pixels become transparent sprite pixels, detection area `0` is set to the
  captured rectangle, and the selected sprite is assigned to the current frame slot.
- `Select Capture` enables a persistent shaped capture selection on the original frame. Use the
  normal point, line, rectangle, circle, ellipse, and filled-shape tools on the original frame to
  add selected pixels; hold `Shift` to erase selected pixels. `Clear Sel` removes the selection.
- When a capture selection exists, `Capture` uses the selected pixels instead of the slot rectangle.
  The selected pixels are copied into the sprite relative to the selection bounds, unselected pixels
  inside those bounds remain transparent, and selected black original pixels remain real sprite
  pixels.
- `Capture` currently replaces the selected sprite record after confirmation. The dialog shows the
  planned `Create New` path, but it stays disabled until sprite allocation updates all cROM and
  `.cRP` sprite tables together.
- Rectangle capture without a capture selection still treats black original pixels as transparent.
- The current frame's sprite assignment slots and bounding boxes are shown as text.
- `Assign` writes the selected sprite into the selected slot for all selected frames.
- `Remove` clears the selected slot's sprite assignment for all selected frames.
- `Min X`, `Min Y`, `Max X`, and `Max Y` edit the selected slot's bounding box for all selected frames.
- Removing a sprite assignment preserves the slot's bounding box, so a replacement sprite can reuse it.
- `Show Bounds` toggles a translucent filled overlay for the selected slot's bounding box on the original frame. It is disabled by default.
  This overlay is visible before capture and does not require the slot to already contain a sprite.
- Canvas, thumbnails, and PNG export compose detected frame sprites over the resolved frame preview.

Sprite-name edits are limited to 31 characters so the legacy fixed 32-byte `.cRP` name slot
remains null-terminated.

## Backgrounds

The app loads and saves background data from modern `.cROM` projects.

Currently implemented:

- Shared SD and HD background image data is preserved.
- Per-frame background IDs are preserved.
- Per-frame SD and HD background masks are preserved.
- The project status line shows the loaded background count.
- `BG ID` assigns the shared background used by the current frame. `None` stores the legacy no-background assignment.
- `Show BG` renders the assigned background layer over original black pixels where the active SD/HD background mask allows it.
- Inspector `Target -> Background` displays the selected background image on the canvas and edits that shared background image with the ordinary drawing tools.
- The `BG` toolbar toggle shows the current frame's background mask as a semi-transparent yellow overlay on the colorized frame.
- `BG` and `Dynamic` are mutually exclusive edit modes because both write on the colorized frame.
- In SD mode, `BG` edits the SD background mask.
- In HD mode, `BG` edits the HD background mask.
- Click the colorized frame to set that background-mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD background mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase background-mask pixels back to `0`.
- Background-mask edits are undoable per frame and per resolution.

Creating or deleting background records is not exposed in the UI yet.
Background import/copy operations replace existing background records only, because the first release
must not change the loaded cROM format.

## Missing Editing Features

Known feature gaps that affect first-release completeness:

- Palette import/export, color-set management, and reusable gradient definitions.
- Directional palette-gradient fills for masks, sprites, and dynamic masks.
- Filling with palette gradients while respecting outline-only tool shapes.
- Wand/free selections and gradient fills inside arbitrary saved selections.
- Runtime-verified playback composition through libserum.

## Preview Rendering TODO

libserum is the source of truth for every runtime-composed pixel. The current Qt preview renderer
is an editor-side approximation used for canvas, thumbnails, and PNG export. It must stay small,
test-backed, and explicitly aligned with libserum until libserum exposes an editor preview API.

For normal cROM v2 frames, the local preview must mirror libserum's composition order:

- Resolve frame pixels from background/static/dynamic data in one scan, using the active dynamic
  mask semantics from libserum.
- Emit dynamic shadows from nonzero original dynamic pixels before writing that dynamic pixel.
- Treat written dynamic pixels and shadow pixels as occupied so later static/background pixels do
  not overwrite them.
- Render detected sprites after the resolved frame and dynamic shadows.

Future preview direction:

- Replace runtime-composed frame previews with libserum output where practical.
- Add a libserum API that accepts cROM data through memory views, without writing temporary files.
- Let libserum convert that in-memory cROM data to cROMc internally.
- Avoid converting the full project for every preview; render a minimal cROM containing one frame or all frames in the active sequence plus the shared data needed by those frames.
- Cache previews by frame, sequence, resolution, and relevant dirty revisions.
- Keep local editor rendering for isolated editing surfaces that libserum does not render directly, such as background images alone, masks, dynamic zones, tool previews, selections, and overlays.

## Testing Checklist

For drawing changes, test at least:

- SD preview and commit.
- HD preview and commit.
- Undo and redo in the same frame/resolution.
- Switching SD/HD does not interleave undo histories.
- Save, reopen, and verify edits persist.
- With `Mask` active, add and Shift-erase pixels on the original/reference region, undo/redo the mask edits, save, reopen, and verify the mask overlay persists.

For project-loading changes, test:

- Open via `.cROM` with matching `.cRP`.
- Open via `.cROM` and manually selected `.cRP`.
- Open via `File -> Open Recent`.
