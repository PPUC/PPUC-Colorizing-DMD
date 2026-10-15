# Comparison Masks

The `Mask` toolbar toggle shows the assigned comparison mask as a semi-transparent magenta overlay
on the original/reference region. Masks are magenta wherever the editor shows them, as in CDMD:
in the mask list of the left sidebar, on the canvas and in the drawing preview.

A comparison mask has one pixel per original pixel. In a project with 256x64 original frames
the masks are 256x64, and the overlay, the tools, flood fill, erasing and undo all work on those
256x64 pixels of the original/reference region. In a project with 192x64 original frames the
same holds for its 192x64 masks.

The Inspector exposes per-frame comparison metadata:

- The `Comparison Mask` section shows the visually selected mask from the left sidebar as `Selected: <ID>`, with
  `(assigned)` when it is the mask of the current frame. Selecting a mask in the sidebar only
  selects it, in the same way as selecting a background or a sprite does; no frame is changed. The
  selection starts as the mask of the first frame and then stays until another mask is selected.
- `Assign Mask` assigns the selected mask to all selected frames.
- The picture of a mask in the left sidebar follows every edit of that mask, its Undo and its
  Redo (redrawn once when the editor is idle again, not per pixel).
- `Edit Mask` enables the mask overlay and returns the canvas to the frame preview. If the sprite
  editor is open, it is closed first (`Edit Sprite` is switched off). It edits the mask assigned to
  the current frame, which is not necessarily the mask selected in the sidebar.
- `Mask ID` shows the comparison mask assigned to the current frame and assigns the entered mask
  directly to all selected frames. `None` stores the legacy no-mask assignment. Its range is the
  masks the project has; a mask number that does not exist is refused, also by the project
  itself (`comparison mask 5 does not exist; the project has 2`). The same holds for the
  `Background` ID and backgrounds. A loaded frame whose ID names no mask or background keeps it
  and is marked `ref. problem` (see "Frame List"); the field then shows `None`, which is what the
  runtime does with it.
- `Shape` selects `Full comparison` or `Shape mode` for all selected frames.
- Both fields are saved back into `.cROM`.
- Mask ID and shape mode edits can be undone, see "Undo And Redo".
- `New` and `Delete` above the mask list of the left sidebar add an empty mask and delete the
  selected one, see "Adding And Deleting Records". `Mask ID` offers the masks the project has.
- Below these controls the page holds the comparison tools: which frames are the same as the
  current one under its mask, the pixels that differ among the selected frames and a mask made
  of them, and the frames that use a mask. See "Same Frames And Common Pixels". The page
  scrolls if the window is too low for all of it.

Editing rules:

- Mask editing is active only while the `Mask` toggle is on.
- Colorized-frame drawing is paused while the `Mask` toggle is on.
- Click the original/reference region to set that mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill also edit the assigned mask from the original/reference region.
- Shape tools show a magenta live preview while drawing on the mask.
- Hold `Shift` while clicking, drawing, or flood filling the original/reference region to erase mask pixels back to `0`.
- Shift-erase shape tools show a dark live preview.
- Edits apply to the assigned comparison mask, not just the current frame.
- If the current frame has no valid comparison mask assignment, clicking the original/reference region does nothing and shows a short status message.
- Mask pixels are saved back into `.cROM`.
