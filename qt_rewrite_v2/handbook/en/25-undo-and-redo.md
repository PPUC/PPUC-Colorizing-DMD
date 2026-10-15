# Undo And Redo

Every edited object has its own undo/redo history. Undo and Redo act on the histories that belong
to the current editing context; a new edit clears redo only in the history it is recorded in.

Histories:

- Colorized frame pixels: per frame and per SD/HD resolution.
- Frame settings: per frame, for both resolutions together. This history holds the comparison mask
  ID, shape mode, background ID, trigger ID (including `Switch mono`), and duration edits of the
  frame, and its `Create HD` and `Remove HD`.
- Comparison masks: per mask. Frames that share a mask share its history.
- Dynamic masks, dynamic colors, and dynamic shadows: three separate histories per frame. They
  belong to the plane the dynamic data is edited on, which is one per project (see "Dynamic
  Masks"): an ordinary project has no histories for HD dynamic data, which cannot be edited.
- Background masks: per frame and per SD/HD resolution.
- Background images: per background and per SD/HD resolution.
- Sprite color images: per sprite and per SD/HD resolution.
- Sprite HD: per sprite, for both resolutions together. It holds `Create HD Sprite` and
  `Remove HD Sprite`.
- Palette colors: per palette.
- Records: one history for the project. It holds the deletion of frames and the addition and
  deletion of sprites, backgrounds and comparison masks.
- Edit colors and color sets: one history for the project. It holds edit color changes (picker,
  dropped colors, `Dyn > Edit`) and `Store`, `Load`, renaming and `Deactivate` of color sets.
  Each step restores the edit colors and the color sets as a whole, so Undo puts back every byte.

Which histories Undo and Redo use, by context. The record history belongs to every one of
them:

1. `Edit Sprite` active with a sprite selected: the selected sprite's color image history for the
   active resolution (its SD history when the sprite has no HD data), its sprite HD history and
   the edit color history.
2. `Mask` active: the comparison mask assigned to the current frame and the current frame's
   settings.
3. `Dynamic` active: the current frame's dynamic-mask, dynamic-color, and dynamic-shadow
   histories and the edit color history.
4. `BG` active: the current frame's background-mask history for the active resolution and the
   current frame's settings.
5. Otherwise: the current frame's pixel history for the active resolution, the current frame's
   settings, the selected palette, the current frame's dynamic-color and dynamic-shadow histories
   (the same ones in SD and in HD mode), the image history of the selected background for the
   active resolution and the edit color history. This is the same whether Inspector `Target` is `Frame` or `Background`, so a
   `Frame -> BG` copy or a background image import can be undone without switching the target.

Within a context, Undo reverts the edit that was made most recently among all of the context's
histories, whichever history it is in. For example, after a frame pixel edit followed by a palette
edit, Undo reverts the palette edit first and the pixel edit second. Redo reapplies the edit that
was undone most recently, so Undo followed by Redo changes nothing and repeated Redo replays the
edits in the order in which they were originally made. An edit that has just been redone counts as
the most recent edit again.

Only the histories of the current context take part. Edits made in other contexts, on other frames,
or in the other SD/HD mode stay in their own histories and are skipped; switch to that context to
undo them. Switching frame, SD/HD mode, edit mode, or selected background or palette updates
whether Undo and Redo are available.

An edit applied to several selected frames is recorded once per changed frame, in that frame's own
history. Undo therefore reverts it only on the current frame; select another frame to undo it
there. This applies to drawing (also in `Set Replace` mode), to dynamic shadow direction and color
changes, to `Edit > Dyn`, and to all frame settings.

Paste, `Auto Copy...`, `Scrolling Auto Fill...`, `Recolor`, `Black Original To Black`, `Remove
Dynamic Zones` and `Assign Dynamic Set` are recorded the same way, one step per changed frame,
even when a step changes the frame's colors, its dynamic mask and its dynamic set colors
together. The step goes into the frame's pixel history of the plane it was made on, or, when it
was made in the `Dynamic` mode, into the frame's dynamic mask history, so that it is undone in
the context it was made in. Auto copy and the scrolling fill change other frames than the current
one: their steps are undone on those frames. The selection itself is not part of Undo (see
"Selection").

Undoing `Create HD` removes the HD frame again and restores the HD data the dialog options had
overwritten: the frame's HD background mask and the HD image of the background that was assigned
at the time (or its absence). Undoing `Remove HD`
restores the HD pixels that were removed. Undo and Redo of these two switch the canvas back to SD
when the current frame is left without HD data; Redo of `Create HD` does not switch to HD by
itself. The same holds for `Create HD Sprite` and `Remove HD Sprite`, which restore the sprite's HD
mask and HD colors.

Histories are dropped when the data they refer to is replaced as a whole:

- Opening a project drops all histories.
- Adding and deleting records drops the histories that name a renumbered record, see below.
- `Remove HD` drops the frame's HD pixel history.
- `Create HD` drops the frame's HD pixel history. Each related item that is upscaled in the dialog
  also drops its HD history: the frame's background mask and the image of the assigned
  background. Dynamic histories are not touched.
- `Create HD Sprite` and `Remove HD Sprite` drop the sprite's HD color image history.
- Undoing or redoing any of these four drops the same histories again, because the HD data is
  replaced as a whole in both directions. Pixel edits made on HD data in between can therefore not
  be undone separately afterwards.
- Sprite `Capture` drops the sprite's SD and HD color image histories. The capture itself is one
  undoable step (see "Sprites" below).

All other histories stay available. Importing an image into a background and copying a frame to a
background are ordinary undoable edits and do not drop any history.

Adding and deleting records:

- Undo puts back exactly what was there: the records with their content, their numbers, the
  references of the frames (sprite slots at their old places, background and mask assignments),
  the sections, the sprite tables and mask names of the `.cRP`, and the imported palettes. Undo
  of an addition removes the record again and takes it away from the frames that were given it
  in between; Redo brings it back with those assignments and with what was edited on it.
- Redo of a deletion deletes the records as they are then, so edits made to them after the Undo
  are not lost if the deletion is undone again.
- These operations are undone in the reverse order in which they were made, whatever the
  context: they are all in the one record history.
- Adding the frames of a dump is one such operation: Undo removes exactly the added frames,
  Redo appends them again with what was edited on them in between. Deleting same frames is a
  deletion of frames like any other.
- Other histories refer to records by number. Whenever one of these operations runs (when it is
  made, undone or redone), every history that names a record whose number changes or that
  appears or disappears is dropped, so that no edit can ever be undone on another record than
  the one it was made on:
  - frames: the histories of the deleted frames and of all frames behind the first deleted one
    (pixels, settings, dynamic mask, colors and shadows, background mask). Histories of frames
    before it stay;
  - a sprite: the color image and HD histories of that sprite and of all sprites behind it;
  - a background: the image histories of that background and of all behind it, and every
    frame's settings history that holds a background assignment to one of them or a `Create HD`
    that upscaled one of them;
  - a comparison mask: the histories of that mask and of all behind it, and every frame's
    settings history that holds an assignment of one of them;
  - the settings history of each frame that lost its mask or background through the deletion.
  A history dropped by a deletion stays dropped when the deletion is undone. An edit made
  between an operation and its Undo on a record with a changed number can therefore not be
  undone by itself afterwards; the edit itself stays.
- A deleted record is kept by the history until the history is dropped (a project is opened) or
  the step can no longer be redone. Nothing is copied for that; the memory of deleted frames is
  simply not released before. A deleted frame holds what it held in the project (its original
  and colorized pictures and whatever of its other parts was edited), typically 12 to 45 KB.
- The add and delete steps keep at most 1 GB of deleted records. When a step goes beyond
  that, the oldest add and delete steps are dropped (never the one just made, however large):
  they can no longer be undone, and the status bar says so after the result of the step, for
  example `Deleted 3000 frame(s), 2 remain (undo keeps at most 1024 MB of deleted records: the 1
  oldest add or delete steps can no longer be undone)`. The steps that remain undo exactly as
  before. The other histories keep only the changes and are not limited.

When an edit cannot be undone or redone because it no longer fits the project (it throws), the
project is left as it was, the step stays where it is in its history, and the status bar says
`Undo failed, the step stays in the history: <reason>`. In particular a deletion whose Undo fails
stays undoable: it is not dropped, so it never becomes permanent that way.

Undoable besides: sprite slot assignment, removal and bounds and color rotation settings (in the
history of each frame they change, one step per frame), sprite capture (one step in the history
of the frame it was made on: the sprite's picture, detection area `0`, source frame and the
frame's slot together), sprite detection areas and sprite names (in the selected sprite's
history, which Undo reaches while that sprite is selected).

Not undoable: palette names and the draw color mode (a setting, see "Draw color modes").
The rotation `Length` and `Delay` boxes count a typed value as one change when it is confirmed
(Enter or leaving the box); each arrow click is a change of its own.
