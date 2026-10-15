# Adding And Deleting Records

Frames can be deleted; sprites, backgrounds and comparison masks can be added and deleted. Every
number that refers to such a record follows, in the project and in the `.cRP`, and every one of
these operations can be undone (see "Undo And Redo").

Where:

| Operation | Where |
| --- | --- |
| Delete the selected frames | `Edit -> Delete Frames...`; the frame list's context menu; `Delete` or `Backspace` in the frame list |
| Delete the selected frames that are shorter than N ms | `Edit -> Delete Frames Shorter Than...`; the frame list's context menu |
| Add the frames of a dump | `File -> Add Frames From Dump...`, see "Adding Frames From A Dump" |
| Delete frames that are the same as others | the `Comparison Mask` section of the Inspector, see "Same Frames And Common Pixels" |
| New comparison mask, delete the selected one | `New` and `Delete` above the list on the `Comparison Masks` section of the left browser |
| New background from the current frame, delete the selected one | `New` and `Delete` on the `Backgrounds` section |
| New sprite, delete the selected one | `New` and `Delete` on the `Sprites` section |

Limits, which are the file format's and the Win32 editor's: at least 1 frame, at most 64
comparison masks, 255 sprites and 255 backgrounds. `New` is disabled at the limit, `Delete`
while nothing is selected, the two frame entries while no frame is selected or the project has
one frame only.

Every deletion asks first. The question names what goes and what follows from it (how many
frames remain, how many frames use the record), `Cancel` is the default button, and the status
bar reports the result with its counts, for example `Deleted 3 frame(s), 12 remain` or
`Deleted background 1, 4 frame(s) have no background now; 6 background(s) remain`. Adding asks
nothing, except for the name of a new sprite.

After an operation, and after its undo or redo, the selection is on the new or restored
record, or on the record that took the place of a deleted one (the last one if the deleted one
was the last). Restored frames are all selected. The frame filter stays as it is: if the frame
that would be selected is filtered out, the next listed frame is. Playback stops, a canvas
selection and a pending free gradient are dropped, and after a change of the frames the sprite
capture selection as well.

## Moving frames

The selected frames can be put elsewhere in the project, so a project can be sorted without
another tool. They go as one block, in their order, and are renumbered with the frames between;
everything a frame has goes with it. One undo step.

- Drag the selected frames in the frame strip and drop them: on the left half of a frame they
  go before it, on the right half behind it, behind the last frame behind the last one. A
  vertical line between the frames shows where they go; near the ends of the strip it scrolls.
  (Pressing on a selected frame and moving the mouse starts the drag; select ranges with
  Shift+click.)
- `Edit -> Move Frames Earlier` (`Alt+Left` or `Alt+Up` in the frame strip) puts them before the
  frame shown before the first selected one; `Move Frames Later` (`Alt+Right` or `Alt+Down`)
  behind the frame shown after the last selected one. With a frame filter that is the neighbour
  in the strip, not necessarily in the project.
- `Edit -> Move Frames To...` asks for the frame number to put them before; the frame count puts
  them behind the last frame.
- Afterwards the moved frames are selected where they are now; the status bar gives their new
  numbers.

Sections follow their frames:

- a section whose frames all move goes with them;
- a section whose first frame moves without the rest starts at its first frame that stays;
- frames dropped (or moved with `Move Frames To...`) right before the first frame of a section
  join that section, which then starts at the first moved frame, unless the frames bring a
  section of their own; `Move Frames Later` past the last frame of a section keeps them in it;
- otherwise moved frames belong to the section of the frame before them.

The source frames of sprites follow their frames. A project with imported palettes (old
Win32 projects) keeps them for its first frames only: a move that would put a frame without one
before a frame with one is refused.

## Deleting frames

Everything a frame has goes with it: original, colorized frame, dynamic data, background mask,
rotations, sprite slots, mask and background assignment, trigger and duration. The frames behind
it move up, so frame numbers change. Comparison masks, backgrounds and sprites stay, also those
no remaining frame uses.

All selected frames cannot be deleted: the status bar says
`Not deleted: all N frame(s) would go, and a project keeps at least one frame`.

`Delete Frames Shorter Than...` asks for a duration in milliseconds (preset: 15, the duration
below which an uncolorized frame is inactive) and deletes those of the selected frames whose
duration is lower. A frame of exactly that duration stays. The question then says how many of
the selected frames that is; if it is none the status bar says so and nothing is asked.

What follows a deleted frame in the `.cRP`, by the Win32 editor's rules:

- its imported palette, if the project has imported palettes for it;
- sections: a section whose first frame is deleted starts at the next remaining frame. If
  another section starts between its old first frame and that frame (the section has no frame
  left), it is removed. First frames behind a deleted frame move up. One case differs from the
  Win32 editor: a section whose frames reach to the end of the project and are all deleted is
  removed too, where the Win32 editor keeps it with a first frame behind the last frame;
- sprite source frames: the `.cRP` records for each sprite the frame it was taken from and the
  rectangle. A source frame number behind a deleted frame moves up; a sprite whose source frame
  is deleted loses its source rectangle (the first value is set to `0xFFFF`), as in the Win32
  editor.

## Sprites

`New` asks for a name and adds an empty sprite behind the last one: no pixel, no detection
area, shape mode off, no dynamic content. A name has 1 to 31 characters and must not be the
name of another sprite; otherwise nothing is added and the status bar says why. The new sprite
is selected. `Capture` then gives it its picture. The `.cRP` records the current frame as its
source frame and no source rectangle.

What a new sprite has besides, by project:

| Project | New sprite |
| --- | --- |
| ordinary | no HD (as in the Win32 editor); `Create HD Sprite` adds it |
| 256x64 original | its 256x64 data, empty, because the runtime renders a frame only if every sprite assigned to it has it |
| 192x64 original | no extra-plane data, like every sprite there |

`Delete` removes the sprite from the sprite slots of every frame. The slots behind it close up,
so that the remaining sprites of the frame are still looked for (the runtime's list ends at the
first empty slot); the emptied slot at the end keeps the bounds of the last slot. Sprites with a
higher number get a number one lower, in every frame and in the `.cRP` tables (names, source
frames, source rectangles and mirror flags).

## Backgrounds

`New` adds a background with the colorized picture of the current frame, as it is stored: not
the composed picture with the frame's own background, dynamic content and sprites. No frame is
assigned to it.

| Project | New background |
| --- | --- |
| ordinary | the SD picture; no HD image (as in the Win32 editor) |
| 256x64 original | the 256x64 picture, and needs a current frame that has one: otherwise `Not added: frame N has no 256x64 frame to make a background of` |
| 192x64 original | the 192x64 picture |

`Delete` takes the background away from the frames that use it (they have no background then)
and gives backgrounds with a higher number a number one lower in every frame.

## Comparison masks

`New` adds an empty mask (nothing masked) behind the last one and selects it. No frame uses it:
`Assign Mask` assigns it, as does dragging the mask from the `Comparison Masks` list onto the canvas (see
"Drag And Drop Onto The Canvas"). The mask has the name its slot has in the `.cRP`.

`Delete` deletes the selected mask. If frames use it, the question says how many and which, and
that they will be compared without a mask, which changes how the runtime identifies them; they
are set to `None`. Masks with a higher number get a number one lower in every frame, and the
mask names of the `.cRP` move with them. (The Win32 editor has no way to delete a mask. There
the 64 mask slots always exist and the number of masks in the file is the highest assigned
number plus one. The Qt editor writes the masks the project has; a frame never refers to a mask
that is not written.)
