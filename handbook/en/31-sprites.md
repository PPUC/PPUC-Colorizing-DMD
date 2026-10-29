# Sprites

Sprite data is loaded into the project and saved in the project file.
`New` and `Delete` above the sprite list of the left sidebar add an empty sprite and delete the
selected one, see "Adding And Deleting Records". Several sprites can be selected in the list
(`Ctrl/Cmd`+click, `Shift`+click for a range), as in the Win32 editor: `Delete` then deletes all
of them after one question, as one undo step, and `Assign` puts each of them on the selected
frames (see below). The sprite clicked last is the one the `Sprite` sections show.

Currently preserved sprite data includes:

- Sprite names from the `.cRP` sidecar, when available.
- Sprite original masks.
- SD and HD sprite color images.
- HD sprite masks.
- Sprite detection dwords, positions, and areas.
- Per-frame sprite assignments and bounding boxes.

The Inspector has two sprite sections, each shown on its own canvas:

- `Frame Sprites`, on the frame canvas: what ties sprites to the frames. The selected sprite,
  the sprites of the current frame (in a short list that scrolls; `Used by the current frame`
  in the left sidebar shows them too), the slot selector, `Assign`, `Capture`, `Remove`,
  `Move Up`, `Move Down`,
  `Select Capture`, `Clear Sel`, `Show Bounds`, `Outline Sprites`, `Source Frame`, the slot's
  bounds (`Min X` to `Max Y`) and `Edit Sprite`.
- `Sprite`, in the sprite editor: the sprite itself. The selected sprite, its name,
  `Show Det Areas` and the detection areas, `Shape mode` and the dynamic sets, `Create HD Sprite`
  and `Remove HD Sprite`, `Apply Sprite`, `Source Frame`, and `Back to Frame`, which leaves the
  sprite editor as `Edit Sprite` clicked again does. Entering the sprite editor opens this section.

The controls:

- The sprite selector lists loaded sprite records and shows `.cRP` sprite names when available.
- The sprite name field edits the selected sprite name. Names are saved back to the fixed-size
  `.cRP` sprite-name table when that table exists.
- Each sprite has a name of its own. `New` asks again, with a message, for a name another sprite
  has already, and a rename to such a name is refused with a message and keeps the old name.
- `Show Det Areas` overlays valid per-sprite detection areas on the original sprite in the sprite
  editor.
- The detection area selector chooses one of the sprite's detection areas. The `X`, `Y`, `W`, `H`
  fields edit an area that is set; they are disabled while the selected area is not set. A new
  area is created by dragging it in the sprite editor (see below).
- A detection area must hold at least four pixels of the sprite next to each other in a row: the
  runtime looks for such a row in a frame to find the sprite. An area without one would never
  find it, so the editor refuses it with a message and keeps the area as it was, whether it was
  dragged or typed in. The Win32 editor refused to save such an area.
- `Clear Area` disables the selected sprite detection area using the legacy `0xffff` marker.
- `Edit Sprite` switches the main canvas to a large sprite editor. It is split like the frame
  canvas: the colorized sprite in the upper half, the original sprite in the lower half, both
  showing the same part. A double click on a frame in the frame list leaves the sprite editor and
  shows that frame for editing.
- The sprite editor zooms as the canvas does: `View -> Zoom In` / `Zoom Out` (`Ctrl/Cmd++`,
  `Ctrl/Cmd+-`) around the middle, `Ctrl/Cmd` with the mouse wheel and a pinch on a trackpad
  around the mouse, up to 8 times the size that fits; `Show Whole Frame` (`Ctrl/Cmd+0`) and a
  click on the toolbar's zoom go back to that size. Zoomed in, scroll bars at the bottom and the right, the
  mouse wheel (with `Shift` sideways), a trackpad, and a drag while `Space` is held move the shown
  part. While the sprite editor is shown, these entries and the toolbar's `-`, zoom and `+` zoom
  it, not the frame canvas, and the toolbar shows its zoom.
- `Create HD Sprite` on the `Sprite` section creates the HD mask and colors of the selected
  sprite by scaling its SD data to 2x: the colors with the filter chosen in the window it opens
  (the one used last, by any `Create HD`, is preselected), the mask doubled. `Remove HD Sprite`
  clears its HD-present flag. Each
  button is enabled when it applies. Both can be undone; Undo restores the sprite's previous HD
  mask and HD colors.
- `Paint Transparent` (beside them, for a sprite with HD data) makes pixels of the HD sprite
  transparent, so that the frame shows through them in HD, while the original, which the sprite
  is found by, stays as it is: for example to leave out the outline a sprite has in the original
  and keep only its inner pixels in HD. On the sprite canvas (`Edit Sprite`, switched to HD)
  strokes of the point, line and shape tools make the pixels transparent; with `Shift` they cover
  again, where the original under them is, with their colors as before. Transparent HD pixels
  over the original are marked with a pattern while the mode is on. The fill tool does not paint
  transparency, and `Paint Dynamic` and `Paint Transparent` are not on together. Each stroke is
  one undo step of the sprite. In a 256x64 project the button works on the 256x64 sprite.
  - Why not capture the sprite without those pixels: a captured sprite starts at its top left
    pixel, so without the outline its original is smaller and starts at the inner pixels. The
    sprite is then also found where the outline is missing, and the HD art made for the full
    sprite is drawn one pixel too far up and left. Keep the full sprite and make the outline
    transparent in HD instead.
  - Step by step: select the sprite (`Create HD Sprite` first if it has no HD data), turn on
    `Paint Transparent`, open `Edit Sprite` and switch the canvas to HD, then paint over the
    pixels to leave out. `Shift` while painting brings pixels back.
- In the sprite editor canvas, drag on the colorized sprite to repaint existing non-black sprite pixels with the active draw color.
  With color set replace (the drawing mode of the edit colors, as on the frame canvas) each
  painted pixel instead takes the edit color of the shade of the sprite's original under it (on
  the HD plane the shade at half the coordinates); the preview shows those colors. A pixel over
  a transparent part of the original keeps its color. A sprite captured with `Capture` or
  `Select Capture` can so be colored set by set.
- Hold `Shift` while dragging on the colorized sprite to erase existing non-black sprite pixels back to black.
- The sprite editor supports the toolbar point, line, rectangle, filled rectangle, circle, filled circle, ellipse, and filled ellipse tools on the colorized sprite.
- The sprite editor also supports the toolbar pick and flood-fill tools on the colorized sprite.
- Sprite shape tools show a live preview and commit the full stroke on mouse release.
- Sprite pixel edits update the sprite editor immediately. Frame thumbnails and the composed frame
  preview refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Undo and redo for sprite color edits are scoped per sprite and per active SD/HD resolution, and
  are available while the sprite editor canvas is active.
- Drag on the lower original sprite canvas to set the selected detection area.
  Detection-area edits update the sprite editor immediately. Frame thumbnails and composed frame
  previews refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Sprite source/original pixels are not freehand editable.
- `Shape mode` (CDMD's `IDC_SHAPEM`): libserum finds the sprite by its shape, lit or not,
  instead of its shades. One undo step of the sprite.
- Sprite dynamic content (CDMD's sprite window): a dynamic sprite pixel shows, at runtime, the
  color its set gives the frame's original shade under it, as libserum's sprite drawing does;
  the canvas and the Runtime view show it so. A sprite has 9 sets (`Set 1` to `Set 9`).
  - `Paint Dynamic`: on the sprite canvas (`Edit Sprite`) strokes of the point, line and shape
    tools put the sprite's pixels into the selected set; with `Shift` (erase) they leave it.
    Every covered pixel can be painted, black ones too; transparent pixels never become
    dynamic. Dynamic pixels are tinted blue while the mode is on. The fill tool does not paint
    dynamic zones. The zones are on the plane the frames' dynamic zones are on: the 128x32
    sprite of an ordinary project (switch the canvas to SD), the native sprite otherwise.
  - The color grid shows the set's color for each original shade; a click changes one.
    `Edit Colors > Set` copies the edit colors into the set, `Set > Edit Colors` the set's
    colors into the edit colors (an edit color step).
  - Each stroke and each color change is one undo step of the sprite; the sprite's pixel
    edits before it stay undoable.
- `Source Frame` goes to the frame the sprite was taken from, as CDMD's "to source frame": the
  frame it was added or last captured on (the `.cRP` keeps it; deleting frames moves it along).
  Its tool tip names the frame. In the sprite editor, it leaves the editor first, as `Back to
  Frame`.
- `Capture` copies original and colorized pixels from the current frame into a sprite, using the
  selected sprite slot bounds as the source rectangle. Captured pixels are placed at the sprite
  origin, black original pixels become transparent sprite pixels, detection area `0` is set to the
  captured rectangle, and the selected sprite is assigned to the current frame slot. The `.cRP`
  records the current frame as the sprite's source frame, as CDMD does. When the captured sprite
  has no four pixels next to each other in a row, the status bar says that the runtime cannot
  find it.
- The confirmation offers `Replace Current` (the selected sprite) and `Create New`. `Create New`
  asks for the new sprite's name as `New` does and captures into the new sprite; adding the
  sprite and the capture are two undo steps. Without a selected sprite only `Create New` is
  offered. As in CDMD, a capture keeps the sprite's dynamic mask and colors.
  Because the bounds of an empty slot cannot be edited, capturing a rectangle into an empty slot
  takes `Assign` first, then the bounds, then `Capture`; or use `Select Capture`, which needs no
  bounds.
- `Select Capture` enables a persistent shaped capture selection on the original frame. Use the
  normal point, line, rectangle, circle, ellipse, and filled-shape tools on the original frame to
  add selected pixels; hold `Shift` to erase selected pixels. `Clear Sel` removes the selection.
- When a capture selection exists, `Capture` uses the selected pixels instead of the slot rectangle.
  The selected pixels are copied into the sprite relative to the selection bounds, unselected pixels
  inside those bounds remain transparent, and selected black original pixels remain real sprite
  pixels.
- `Capture` replaces the selected sprite record after confirmation. To capture into a new sprite,
  add one with `New` on the `Sprites` section first and capture into it. The dialog's `Create New`
  button is still disabled.
- Rectangle capture without a capture selection still treats black original pixels as transparent.
- The current frame's sprite assignment slots and bounding boxes are shown as text.
- A frame has 128 sprite slots (see "Project Files", "libserum 2.9"); stay within 32 per frame
  wherever you can. Every sprite more adds to the time libserum needs for each frame, so going past
  32 is for a single frame that cannot be done otherwise and should never become best practice. In
  the old Williams System 3 to 6 games, such as Flash, a few milliseconds of latency do not hurt;
  in a video mode avoid it.
  - Putting a 33rd sprite on a frame (`Assign` or `Capture`) asks first, with this advice;
    `Use More Sprites` goes on, `Cancel` leaves the frame as it is.
  - While the current frame uses more than 32 sprites, the section shows the warning above the
    list.
- A frame's sprite slots never have an empty slot before a used one: libserum stops looking for
  sprites at the first empty slot, so the sprites behind it would be ignored. Every edit keeps the
  list closed up:
  - `Assign` writes the selected sprite into the selected slot for all selected frames. Where
    that slot of a frame is empty, the sprite goes to the frame's first empty slot instead (the
    slot itself or one before it), with that slot's bounds; the status bar says on how many
    frames that happened, and the current frame's slot selection follows.
  - With several sprites selected in the list, `Assign` puts each of them on every selected frame,
    as the Win32 editor's `Add Sprite` did: a frame that has a sprite already keeps it in its slot
    with its bounds, otherwise the sprite goes to the frame's first empty slot. A new slot gets
    the bounds and offset of the current frame's selected slot when that slot holds a sprite, and
    the whole frame as its bounds otherwise. So for the ten digits of a score, set up the slot of
    one digit (bounds and offset), then select the other nine in the list and press `Assign`. A
    frame whose slots are all used gets no more; the status bar counts the sprites added, those
    there already and those left out. It is one undo step.
  - `Capture` assigns the captured sprite by the same rule.
  - `Remove` works as in the Win32 editor (`IDC_DELSPRITE2`): it takes the sprite of the current
    frame's selected slot and removes it from every selected frame that has it, from the last
    slot that holds it; the slots behind move up with their bounds, and the last slot becomes
    empty, keeping its bounds. Selected frames without that sprite are skipped and counted in the
    status bar (`Removed sprite 2 from 3 frame(s), 1 skipped (without that sprite)`).
  - `Move Up` and `Move Down` change the order of the slots, which is the order in which
    libserum draws the sprites: where sprites overlap, the one in the later slot is drawn over
    the one in the earlier slot. The sprite of the current frame's selected slot changes places
    with the sprite one slot up or down, both with their bounds, in every selected frame that
    has it (in the selected slot, or else in the first slot that holds it); the slot selection
    goes with it. A frame without a sprite in the slot to change places with is skipped and
    counted in the status bar. Each frame's change is an undo step of its own, as with `Remove`.
  - A project loaded from a file can have such a gap (the Win32 editor before `Remove` closed up
    could leave one; none of the five 3.17.3 test projects has one). It is kept as it is, and the
    frame is marked: its list item has the line `ref. problem` and its tool tip and the frame
    status line's tool tip say `Sprite slot N is empty: the sprites after slot N are ignored by
    the runtime`. Removing a sprite in front of the gap or assigning one into the gap closes it.
- `Min X`, `Min Y`, `Max X`, and `Max Y` edit the selected slot's bounding box for all selected
  frames in which that slot has a sprite. They are disabled while the slot of the current frame is
  empty. A minimum cannot be raised above its maximum and a maximum cannot be lowered below its
  minimum; the edited value stops at the other one.
- `Offset X` and `Offset Y` move the sprite the slot finds: it is still looked for in the bounding
  box, but drawn that many pixels to the right (negative: to the left) and lower (negative:
  higher) than where it is found, from libserum 2.99.11. One set of digit sprites can so show
  numbers at another place than the one the game draws them at, instead of a second set of
  sprites with a black margin. Like the bounds they apply to the selected slot of all selected
  frames that have a sprite in it.
  - The sprite is cut to its bounding box as always, and then drawn in the bounding box moved by
    the offset, as far as that lies in the frame; `Show Bounds` outlines that box with an orange
    dashed line next to the filled bounding box.
  - Where the sprite was found, the frame shows its own colorized picture, as anywhere without a
    sprite: paint the place as it should look (for example black). A dynamic zone there would
    still color the game's pixels, so leave such places out of the dynamic zones.
  - The sprite's dynamic pixels take the shades of the game's pixels where it was found.
  - A moved sprite does not hide another sprite from being found: sprites are always looked for
    in the game's frame. Where two drawn sprites overlap, the later slot is drawn over the
    earlier one (`Move Up`, `Move Down`).
  - `Assign` into a used slot keeps its bounds and its offset; `Capture` sets new bounds and no
    offset.
- `Show Bounds` toggles a translucent filled overlay for the selected slot's bounding box on the original frame. It is disabled by default.
- While `Show Bounds` is on and the frame itself is edited (no mask, dynamic zone or background
  mode), the `Rectangle` and `Filled Rectangle` tools draw the bounds on the original frame:
  drag the area in which the sprite is looked for, and on release it becomes the selected slot's
  bounds (`Min X` to `Max Y`), filled or not, from any corner, as typing them does: for all
  selected frames in which that slot has a sprite, one undo step. An empty slot gets
  none; the status bar says so. On the colorized frame the tools paint as always.
- `Outline Sprites` (on by default) draws a green outline on the colorized frame around the
  pixels the sprites libserum finds on the frame paint, exactly those, so that a found sprite
  can be told from the frame's own colors. It works with `Show Sprites` off too: the outline
  then shows where the sprites would be.
  The overlay also shows the bounds an empty slot holds.
- `Edit Sprite` and `Select Capture` are switched off when a project is opened. `Edit Mask` and
  `Edit Background` switch `Edit Sprite` off, since they return to the frame view.
- Canvas, thumbnails, and PNG export compose detected frame sprites over the resolved frame preview.
  `Show Sprites` in the Inspector's `Frame` section turns them off on the canvas, the DMD
  output and the GIF export (not in the thumbnails), to see the frame under them.
  Sprites are looked for and drawn by libserum's rules:
  - the sprite list of a frame ends at its first empty slot, so a sprite in a slot behind an
    empty one is not looked for;
  - a sprite assigned to two slots is looked for once, in the bounds of its first slot;
  - a detection area may end on `Max X` and `Max Y`: a sprite that fills its bounds is found;
  - a sprite in shape mode is compared by lit and unlit pixels, whatever the shades are;
  - a dynamic sprite pixel takes the color its dynamic set gives the original frame's shade
    at that place.

Sprites in HD are drawn on the doubled grid: at twice the position where they are found, their
HD art covering twice the size of their original. HD frame art painted off that grid, for
example a score painted one HD row lower to leave an empty row above it, does not line up with
them: a digit that is a sprite appears one row higher than the digits painted into the frame.
Moving pixel art does not help, since the frame art around it (bars, a band behind the score)
would jump between frames with and without the sprite. Make all the digits sprites instead, so
that they are all drawn on the same grid:

- Capture each digit (`0` to `9`) once into a sprite of its own. Use `Select Capture` and
  select the digit's whole cell, its black pixels included: they stay part of the sprite, which
  then covers the cell, so that no digit painted into the HD frame art shows through around it.
  Alternatively paint the score area of the HD frame art without digits.
- One slot per digit is enough, not one per position: a sprite is drawn wherever it is found in
  its slot's bounds, so one `0` sprite with bounds over the whole score draws all its zeros.
  Set up the bounds of one digit's slot, select the other digits in the sprite list and press
  `Assign`: the new slots get the same bounds and offset.
- The digits line up with each other then, but the score as a whole stays one HD row off frame
  art painted off the grid (its band has one row more margin on one side than on the other).
  Only HD frame art painted on the doubled grid avoids that.

In projects with 256x64 original frames, sprites are detected, bounded, captured and drawn on
the 256x64 original, `Create HD Sprite` is `Create 256x64 Sprite` and there is no
`Remove HD Sprite`; see "Projects With 256x64 Original Frames". In projects with 192x64 original
frames all of this happens on the 192x64 original and there are no HD sprites; see "Projects
With 192x64 Original
Frames".

Sprite-name edits are limited to 31 characters so the legacy fixed 32-byte `.cRP` name slot
remains null-terminated.
