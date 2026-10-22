# Same Frames And Common Pixels

The lower part of the Inspector's `Comparison Mask` section. It answers what a comparison mask is drawn for:
which frames the runtime cannot tell apart.

Two frames are "the same" under a comparison mask and a shape mode if their originals have the
same hash under them: without the pixels the mask covers, and in shape mode with every shade
above 1 taken as 1. That is the hash libserum identifies frames by, so "the same" means exactly
"the runtime identifies one as the other". (The Win32 editor compares the pixels instead; the
two differ only if two different pictures have the same CRC.) Checked on `afm_113b` against
libserum with a colorization of one frame alone: 45 combinations of frame, mask and shape mode,
the same list of frames each time.

- `Same frames: N` is the number of frames that are the same as the current frame under its own
  mask and shape mode. It follows the frame, its `Mask ID` and `Shape`, and every stroke on its
  mask. The box beside it lists them; choosing one goes to that frame. From 10000 on the label
  reads `>=10000` and the box lists the first 10000, as in the Win32 editor.
- The frame status line below the canvas shows the same count, `| N same`, wherever the
  section is: once a frame has stayed current for a moment (a quick scroll through the frames
  does not wait for it), or at once while the section is shown.
- While the section is shown, the same frames are outlined in green in the frame strip, and the
  strip's scroll bar has a green tick where each of them is in the project, so a part with same
  frames can be scrolled to. A frame the filter hides is not marked.
- `Ctrl/Cmd+Left` and `Ctrl/Cmd+Right` scroll the strip to the previous and the next same frame
  and outline it more strongly, from the strip or the canvas. The current frame and the selection
  stay, so the canvas keeps the frame whose mask is drawn while the same frames are looked
  through, e.g. before `Delete Same As Current...`. The keys can be held down. Going to another
  frame starts from that frame again.
- `Update Same List` counts the same frames of the current frame for no mask and for every mask
  of the project, each as `full comparison / shape N`. It helps to choose a mask. The list is
  emptied when the frame or anything the comparison depends on changes.
- `Delete Same As Current...` deletes the frames that are the same as the current frame.
- `Delete Same As Selected...` does that for every selected frame, each under its own mask and
  shape mode.
- `Delete All Duplicates...` does it for every frame of the project, from the first frame on.
- `Keep selected frames`: none of the three deletes a selected frame.

The three deletions work as in the Win32 editor: the frames are taken in ascending order, each
one that is still there removes every other frame that is the same as it, and a frame that was
removed removes nothing. "The same" is not symmetric when frames have different masks, so a
frame can remove an earlier one. At least one frame always remains. Each deletion asks first,
naming the frames, and is one step for `Undo`, like every deletion of frames (see "Adding And
Deleting Records"). Unlike the Win32 editor, a deletion is not limited to the first 10000 same
frames of a frame.

- `Spot Common` marks, in yellow on the original/reference region, the pixels in which the
  originals of the selected frames differ. With fewer than two frames selected nothing is
  marked. The marking follows the selection.
- `Spot Common marks` chooses which pixels: `Differing Pixels` (the default) or `Common
  Pixels`, the pixels in which all selected originals are the same, as the Win32 editor tints
  them. The choice is kept for every project. `Common To Mask` is not affected by it.
- `Common To Mask` makes the comparison mask of the current frame cover exactly those pixels,
  replacing what the mask held. It needs at least two selected frames and a mask assigned to
  the current frame; it does not need `Spot Common` to be on. The `Mask` toggle is switched on,
  and the step is undone with `Undo` like a stroke on the mask. The mask is shared: every frame
  that uses it is compared with it, and the status bar says how many do.

- `Common To New Mask` does it with a new mask instead: it adds a comparison mask that covers
  exactly those pixels and assigns it to all selected frames, whatever masks they had. It needs
  at least two selected frames and no mask of the current frame. It is one step for `Undo`,
  which removes the mask and gives the frames back the masks they had. The new mask is selected
  and shown.

  A typical use: select the frames that show the same scene with a changing score and press
  `Common To New Mask`. `Same frames` then counts them, and `Delete Same As Current...` leaves
  one of them. (With `Common To Mask` the same takes a mask added with `New` on the `Comparison
  Masks` section and assigned to the frames with `Mask ID` first.)

- `Frames using` lists the frames that have the chosen mask assigned, or no mask; the entries
  show how many there are. A click on a frame goes to it. `Select Them` selects them in the
  frame list.

A frame that the frame filter hides cannot be gone to or selected; the status bar says so.

The same frames are worked out only while the `Comparison Mask` section is shown. The first question about a
mask reads every original once (a fraction of a second for a few thousand frames); after that
the answers are kept until an original, a mask or an assignment changes. `Update Same List`
does that for every mask and can take seconds in a large project with many masks.
