# ColorizingDMD Qt Rewrite v2 Handbook

This handbook describes the implemented behavior of `qt_rewrite_v2`.
Update it in the same change set whenever a user-visible feature changes.

## Project Files

The Qt rewrite edits legacy Serum editor projects directly:

- `.cROM` stores colorized frame data and runtime project data.
- `.cRP` stores original/reference frames.
- `.cROMc` is not edited directly.

Saving currently writes back to the existing `.cROM` path. The `.cRP` file is required for loading but is not modified by the current editor features.

The in-memory project model currently carries structured comparison-mask data from `.cROM`:

- Per-frame shape comparison mode.
- Per-frame comparison mask ID.
- SD comparison mask bitmaps.

It also carries structured dynamic-mask data from `.cROM`:

- Per-frame SD and HD dynamic mask bitmaps.
- Per-frame SD and HD dynamic color sets.

## Opening Projects

Use `File -> Open Project...` and select a `.cROM` file.

The app looks for a matching `.cRP` next to it with the same base name. If it cannot find that file, it asks for the `.cRP` path.

After a successful open, the project is added to `File -> Open Recent`.
Recent-project entries show the full `.cROM` path and internally remember both the `.cROM` and `.cRP` paths.

## Saving

Use `File -> Save` or the standard Save shortcut.

Save behavior:

- Updates colorized SD and HD frame pixels in the existing `.cROM`.
- Updates shape comparison modes, comparison-mask assignments, and comparison-mask bitmap data.
- Preserves loaded SD/HD dynamic masks and dynamic color sets.
- Updates HD-present flags.
- Preserves cROM data that is not edited yet, including trailing data.
- Marks the document clean after successful save.

Use copied projects for testing until broader project metadata editing and save coverage are implemented.

## Frame List

The left frame list shows one thumbnail per loaded frame.

Frames with HD data are marked with `(HD)`.
Selecting a frame updates the central canvas and the frame status line.

## SD And HD Modes

Use the `SD` and `HD` toolbar buttons to switch the active editing resolution.

Rules:

- SD edits target 128x32 colorized frame pixels.
- HD edits target 256x64 colorized frame pixels.
- If a frame has no HD data, the canvas falls back to SD display/editing.
- Canvas hit testing maps mouse coordinates explicitly to SD or HD pixel coordinates.

## Canvas

The central canvas shows:

- The active colorized frame region.
- The original/reference frame region below it.

The canvas does not draw text labels or white frame outlines over the image data.

Drawing previews are transient overlays. They do not modify project data until mouse release or click commit.

Use the `Mask` toolbar toggle to show the current frame's assigned comparison mask over the original/reference region.
When `Mask` is active, clicks on the original/reference region add pixels to the assigned comparison mask.

Use the `Dynamic` toolbar toggle to show dynamic-mask pixels over the active colorized frame.
The dynamic overlay follows the active SD/HD mode.

## Colors

The toolbar contains fixed swatches:

- White `0xffff`
- Red `0xf800`
- Green `0x07e0`
- Blue `0x001f`
- Black `0x0000`

The active draw color can also be set with the `Pick` tool. If the picked RGB565 color is not one of the fixed swatches, no swatch is checked, but the picked color is still used by later drawing tools.

## Drawing Tools

All drawing tools operate on the active colorized frame and active SD/HD mode.

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

### Pick

Click a colorized frame pixel to set the active draw color to that pixel's RGB565 value.

This tool does not modify project data and does not create an undo entry.

### Flood

Click a colorized frame pixel to flood-fill the contiguous four-neighbor area that matches the clicked pixel's current color.

The fill uses the active draw color. The whole filled area is committed as one undoable edit.

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

The toolbar also exposes per-frame comparison metadata:

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

Dynamic-mask editing and background masks are not implemented yet.

## Dynamic Masks

The `Dynamic` toolbar toggle shows the current frame's dynamic-mask pixels as a semi-transparent cyan overlay.

Rules:

- The overlay is read-only for now.
- In SD mode, the SD dynamic mask is rendered over the SD colorized frame.
- In HD mode, the HD dynamic mask is rendered over the HD colorized frame.
- `Mask` and `Dynamic` are mutually exclusive overlay modes.

Dynamic-mask editing is not implemented yet.

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
