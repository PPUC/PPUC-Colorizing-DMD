# Copy And Paste

`Edit -> Copy` (`Ctrl/Cmd+C`) copies the selected pixels of the current frame: their colors on the
plane, the original shades of the ROM pixels they show and, on the plane dynamic zones live on
(see "Dynamic Masks"), their dynamic mask values and the colors and shadow of every dynamic set
they use. What is kept is the selection's bounding box. The colors also go to the system
clipboard as a PNG image (transparent outside the selection). The internal clipboard is emptied
when a project is opened.

`Edit -> Paste` (`Ctrl/Cmd+V`) starts paste mode: the copied pixels float over the frame canvas,
first where they were last pasted (the first paste after a copy: where they were copied from),
then centred on the mouse. Pasting an object that slides in frame by frame then takes only
`Ctrl/Cmd+V`, a few arrow key presses and `Enter` on each frame. Each paste starts unmirrored,
as in the Win32 editor. A fine line around them shows the copied box; it runs between the pixels
and covers none of them. In paste mode:

- the arrow keys move them by a pixel, with `Shift` by 10; the keypad's `2`, `4`, `6`, `8` as in
  the Win32 editor;
- `K` mirrors them horizontally, `L` vertically (again to turn back);
- a click or `Enter` pastes them on the current frame or, with several frames selected, on every
  selected frame, and ends paste mode (as in the Win32 editor);
- `Esc` cancels.

With `Edit -> Stay In Paste Mode` checked, a click or `Enter` pastes and paste mode goes on: the
copied pixels can be stamped again elsewhere, on this or another frame, until `Esc` ends it. The
status bar says so after each stamp. Each stamp is its own undo step. The setting is
kept for every project; it is off at first, as the Win32 editor works.

What a paste writes, per frame: the colors, the dynamic mask, and the colors and shadow of the
dynamic sets the copied pixels use (Win32 `CopyDynaCol`). Variants:

- `Paste Where Originals Match` (`Ctrl/Cmd+Shift+V`), or `Shift` held at the click or `Enter`: only
  pixels whose original shade on the target frame equals the copied pixel's.
- `Paste Dynamic Mask Only` (`Ctrl/Cmd+Alt+V`), or `Alt` held at the click: only the dynamic mask,
  no colors and no set colors. The Win32 editor does not mirror this variant; here it is
  mirrored like the others.

Pixels that land outside the plane are left out. Frames without a picture on the HD plane are
left alone when pasting there. A paste is one undoable step on all frames it changed; each frame
keeps its part in its own pixel history (in the `Dynamic` mode its dynamic mask history), see
"Undo And Redo".

Content is pasted only on the plane it was copied from (the Win32 editor's `Copy_Content`
check): SD content on the HD plane and HD content on the SD plane are refused with a message
naming the planes, and so is content of a project of another layout. On the HD plane of an
ordinary project nothing dynamic is copied or pasted, since the zones are the SD plane's. `Paste
Dynamic Mask Only` needs the dynamic plane.

On the sprite canvas (`Edit Sprite`), `Copy` and `Paste` work on the sprite, as in CDMD's sprite
window; the clipboard is the same as on the frame canvas:

- `Copy` puts the sprite's pixels that are not transparent into the clipboard, cut to their
  bounding box, with their colors and original shades (no dynamic content). Pasted into frames
  they behave as a copied frame selection.
- `Paste` replaces the sprite's picture on the plane the canvas shows with the clipboard, put at
  the sprite's top left corner; the rest of the sprite becomes transparent. A selection copied
  from a frame also makes that frame the sprite's source frame. It is one undo step of the
  sprite (`Undo sprite change`); its pixel edits before it can no longer be undone separately.
- The clipboard has to come from the same plane (SD or HD) of a project of the same frame size
  and fit into a sprite (256x64); otherwise the status bar says why. On the SD plane of a sprite
  with HD data, the HD picture is made again from the SD one, as `Capture` does.
- Mirroring as CDMD does with K and L held while pasting is not offered here: paste into a
  frame mirrored and copy it from there.
- `Paste Where Originals Match`, `Paste Dynamic Mask Only` and the selection tools work on the
  frame canvas only.

## Copy Frame Settings

`Edit -> Copy Frame Settings...` puts settings of the current frame on the other selected frames.
Each chosen setting replaces the target's, it is not merged into it: copied dynamic zones are
exactly the source's afterwards, and the target's own are gone. The pictures (colorized pixels)
are not copied; Copy and Paste is for those.

The dialog lists the settings, each with how many of the selected frames it changes:

- `Dynamic content`: the dynamic zones, the colors of every dynamic set (with which sets color the
  SD picture only) and every set's shadow, on the plane the zones live on (see "Dynamic Masks").
- `Background`: the assigned background, and the background masks.
- `Sprites and colors`: every sprite slot with its bounds, and the color rotations.
- `How and when the frame plays`: the comparison mask and the shape mode (they decide how libserum
  finds the frames, which the dialog points out when they are chosen), the trigger, the speaker
  lights, the DOF events and the duration.

At first the settings of the picture are chosen and those of how the frame plays are not; the
choices are kept for the next time. HD data goes only between frames that both have HD. A frame
of a scene takes only what it can have: the background, its mask, color rotations, lights, DOF
events and the duration (see "Scenes"); the dialog and the status bar say how many were left
out. The copy is one undo step for all frames.

