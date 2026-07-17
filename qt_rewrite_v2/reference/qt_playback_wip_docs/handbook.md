# PPUC-Serum-Colorizer Handbook

This handbook captures editor behaviors and workflows that differ from the legacy app. It is meant to be updated as new features land.

## Stability Checklist (Quick Regression Test)

Run this checklist after changes that touch rendering, saving, or playback:

- Drawing on frames/sprites shows immediately and remains visible after mouse release.
- Switching frames away and back keeps the drawing intact.
- Playback starts from the selected frame and loops correctly.
- Autosave runs only when the project is dirty and playback uses autosaved files.
- Save/Save As/Autosave produce a valid `.cROMc` (no missing masks/dynamics/sprites; sizes close to the source file).
- Preview row renders with rotations enabled.

## Performance Notes

- Large projects load frame data on demand; the first time you open a frame or run a full save, expect a short delay while data is decoded.
- Autosave can be enabled in Settings; it writes to separate `*.autosave.cRP` / `*.autosave.cROMc` files.

## Navigation and Selection

- Use the back/forward buttons (`<`/`>`) on each canvas to jump through recently viewed frames, sprites, images, and backgrounds.
- The preview row supports multi-selection with native shortcuts (Cmd/Ctrl and Shift).
- The last selected frame in a multi-selection is the active canvas frame; clicking within the selection switches the active frame.
- Use the preview-row filter button to show only the selected frames.
- Press `Esc` or click empty space in the preview row to clear a frame selection.

## Preview Row

- Frames are shown stacked: colorized on top, original (orange) below, and HD below that if available.
- Use the filter button to show only frames that use the currently selected mask, dynamic mask, background, or sprite.
- Use the HD filter button to show only frames that have HD data.
- Use the mask overlay toggle to show masks on the original preview frames.
- Use the rotate toggle to preview color rotations in the preview row.
- Use the refresh button to rebuild previews after bulk edits.
- Use Play/Pause/Stop/Prev/Next/Rew/Fwd in the Playback tab to test-play the selected frames (or the whole ROM if none or only one frame is selected). A single selected frame becomes the start point. Playback loops back to the first selected frame when it reaches the end.
- During playback, the current frame is highlighted in the preview row and the row scrolls to keep it centered when possible.
- Playback can override per-frame durations using the `Fixed` control (milliseconds) in the Playback tab.
- Playback uses libserum’s runtime pipeline, so scene triggers and scene timing behave like in production.
- Playback triggers an autosave and uses the autosaved files for rendering.

## Debug Check (Help → Run Debug Check)

Use this when something feels off or after a risky change. It will:

- Log the current project state (frame/sprite/background counts).
- Render frame 0 and the current frame via libserum and log whether they are empty.
- Attempt to initialize the libserum runtime for playback and log success/failure.

The results are written to the log file (Help → Copy Log).

## Canvas Controls

- **Fit to View** fits the current frame/sprite/background to the canvas.
- Canvases auto-fit the first time they show content; subsequent user zoom/pan is preserved.
- Pixels are rendered as circles. The diameter is adjustable in Settings (default 80%).
- Outline thickness and outline colors (mask/dynamic/background/sprite) are adjustable in Settings.
- **Original toggle** shows/hides the original (orange) reference under the colorized frame.
- **Mask/Dynamic buttons** select the mask mode for the original reference (only one active).
- **Background Mask** is independent and edits the background mask on the colorized frame.
- **Background toggle** shows/hides the background layer while editing frames.
- **HD toggle** switches between SD and HD for frames, sprites, and backgrounds.
- The status bar shows pixel coordinates for the canvas under the cursor.
- The Playback canvas shows the rendered output from libserum while playing.
- When playback is idle, the Playback canvas mirrors the currently selected frame.

## Drawing Tools

- Tools: point, line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, magic fill, color picker.
- Right-click erases for pixel edits; Shift erases when editing masks/dynamic masks.
- ESC cancels the current drawing preview or palette action.
- Live preview shows a semi-transparent overlay while drawing shapes.

## Frames and Multi-Frame Editing

- Drawing actions on frames apply to all currently selected frames.
- Mask edits apply to the assigned mask, which is shared across all frames using it.
- Dynamic mask edits apply to the current dynamic set for all selected frames.
- Background assignment and background mask edits apply to all selected frames.
- Dynamic-content regions are outlined in yellow on the frame canvas and are not paintable (same as sprites).
- Frame history supports back/forward navigation per canvas.
- Trigger ID is per frame in Tools → Inspector. “Switch to Monochrome” sets ID 65432 and locks the field; manual IDs must be < 65432.

## Masks (Comparison Masks)

- 64 shared masks; each frame references exactly one.
- Edit masks on the **original** (orange) frame.
- Reorder masks with `Up`/`Down` without breaking frame assignments.
- Shape comparison mode is available per frame (compares black vs. non-black).

## Dynamic Masks

- 32 dynamic masks per frame; each pixel stores a dynacouche index.
- Edit dynamic masks on the **original** (orange) frame.
- Dynamic masks use per-frame dynamic color sets (2-bit or 4-bit).
- The dynamic mask list shows a mask preview plus a mini color strip.
- Hold Shift while drawing to subtract from the current dynamic mask.

## Backgrounds

- Assign backgrounds per frame (drag & drop or inspector).
- Each frame has a background mask drawn on the **colorized** frame.
- Backgrounds can be edited on the Background canvas with the same drawing tools.
- Background previews and assignments are available from the Components list and the Inspector.

## Sprites

- Sprite canvas shows colorized sprite on top and the original reference below.
- Sprite detection areas (4) are edited on the original (bottom) sprite.
- Drag & drop sprites onto a selected frame zone to assign.
- Sprite overlays are drawn on top of frames and cannot be painted over.
- Sprite dynamic masks use per-sprite dynamic color sets.

## Sprite Zones (Frame Detection Zones)

- **Create a zone:** In Components → Sprite Zones, click `Add Zone`. A new zone is created for the current frame and shown on the frame canvas.
- **Select a zone:** Click a zone in the list to show its bounding box on the frame canvas.
- **Edit a zone:** Enable Zones on the frame canvas, then draw a rectangle on the *original* (bottom) frame. This updates the selected zone.
- **Assign a sprite to a zone:** With Zones enabled, drag a sprite from Components → Sprites onto the original frame on the canvas. The sprite is added to the selected zone.
- **Reorder sprites in a zone:** Use `Up`/`Down` in the Sprite Zones tab to reorder sprites assigned to the selected zone.
- **Remove a sprite from a zone:** Select the sprite in the zone list and click `Remove`.
- **Remove a zone:** Select the zone and click `Remove Zone`. This clears all sprite assignments for that zone.

## HD Sprites

- Use the HD toggle on the sprite canvas to switch between SD and HD sprites.
- `Create HD` in the HD tab will generate an HD sprite for the selected sprite when none exists.

## HD Frames and Backgrounds

- HD frames are 256x64; SD frames are 128x32.
- Use the HD tab to create or delete HD frames/backgrounds/sprites.
- Upscaling modes include nearest/scale2x/bilinear/bicubic with brightness correction.
- HD previews are double-size in Components and preview row.
- An all-black HD background is treated as “missing.”
- HD background masks are double-scaled when generated from SD.

## Colors, Palettes, and Rotations

- Full palettes: 256 sets × 64 RGB565 colors.
- Reduced palettes: 64 sets of 4 or 16 colors (2-bit/4-bit ROMs).
- Dynamic color sets: 32 per frame, used by dynamic masks.
- Rotation sets: editable in the Tools → Color Sets tab; use `Set Slot` to assign, and the gradient tool for ranges.
- Selecting a color sets the active draw color; the color picker updates the active draw color.
- Gradient tool fills a range between two selected palette slots.
- Set Slot applies the current draw color to a chosen slot.
- Left-clicking a full palette slot blinks matching foreground pixels on the canvas while the mouse is held.
- Right-clicking a full palette slot opens the color picker.
- Changing a full palette slot recolors matching foreground pixels on selected frames; backgrounds, sprites, and dynamic masks are not modified.
- Drag the current color onto a full palette slot to set it; drag a full palette color onto reduced, dynamic, or rotation slots to assign it.

## Undo/Redo and Settings

- Undo/redo stacks are separated for frames, masks, backgrounds, sprites, and palettes (including rotation edits).
- Undo/redo applies to all selected frames where relevant.
- Legacy `.cROM/.cRP` saves preserve existing metadata even if the editor does not expose it yet.
- Settings dialog allows adjusting undo/history sizes and cache limits (frames, sprites, backgrounds).
- Logging can be enabled/disabled in Settings (enabled by default) and writes to `ppuc-serum-colorizer.log` in the app data folder.
- If the app did not shut down cleanly, a startup prompt offers to copy the log for reporting.
- The Help menu includes “Copy Log” and the log is truncated automatically to keep it manageable.
- The Help menu includes “Dependencies & Licenses” with links to the bundled `licenses/` folder.

## Open/Save and Legacy Compatibility

- Legacy cROM/cROMc project load/save is supported.
- When a `.cROMc` exists next to a `.cROM`, the editor loads frame data from the `.cROMc` while still reading editor metadata from the `.cRP`.
- Saving a legacy project also writes/updates the matching `.cROMc`.
- “Open Recent” persists across app restarts.
- The editor targets Serum V2 while preserving legacy file compatibility.
