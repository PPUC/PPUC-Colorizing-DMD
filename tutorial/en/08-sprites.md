# Sprites

## What a sprite is for

A comparison mask and a dynamic zone handle content that changes in one place. A sprite handles
an object that moves: a ball, a character walking across, the digits of a score in a font of
their own. libserum looks for the sprite's picture in every frame, within bounds you set, and
draws it in the colors you gave it wherever it finds it.

A sprite has an original (the shades it is found by) and colors, and it lives in the project
once. Frames use it through their sprite slots: each slot names a sprite and the area of the
frame it is looked for in.

## Capturing a sprite

1. Go to a frame where the object is colored the way you want it.
2. In the Inspector's `Frame Sprites` section, choose a slot in the slot selector (an empty one
   for a new sprite) and press `Select Capture`. Paint over the object on
   the original frame with the drawing tools; `Shift` erases. Only the selected pixels become
   the sprite.
3. Press `Capture`. `Create New` asks for a name and makes a new sprite of the selected pixels,
   with their colors, assigned to the current frame.

Without a capture selection, `Capture` takes the selected slot's rectangle, and black original
pixels become transparent.

A sprite is found by a row of at least four pixels next to each other. The editor warns when a
sprite has none.

## Putting it on frames

- Select the frames and the sprite, and press `Assign`. Each frame gets the sprite in a slot.
- The slot's bounds (`Min X` to `Max Y`) limit where the sprite is looked for. Smaller bounds
  find it faster and avoid false finds.
- `Move Up` and `Move Down` order the slots: where sprites overlap, the later one is drawn on top.
- `Remove` takes the sprite off the selected frames.

For the ten digits of a score: capture the digits as ten sprites, set up the slot of one digit
place (its bounds) on a frame, select the other nine digits in the sprite list and press
`Assign`. Repeat for the next place of the score.

Stay within 32 sprites per frame. A frame can hold more, but every sprite costs libserum time
on each frame.

## The sprite editor

`Edit Sprite` opens the selected sprite on the canvas: its colors above, its original below.
Paint its colors there as on a frame. In the `Sprite` section:

- detection areas (`Show Det Areas`) are the parts of the sprite libserum searches for; drag one
  in the sprite editor to set it;
- `Shape mode` finds the sprite by its shape instead of its shades;
- a sprite can have dynamic sets of its own: with `Dynamic` on, strokes put sprite pixels into
  the set chosen in `Set`, and those pixels take the set's color for the shade of the frame
  under them;
- `Back to Frame` (or `Edit Sprite` again) returns to the frame.

`Show Sprites` in the `Frame` section and `Outline Sprites` show where the sprites are found on
the canvas.

Handbook: "Sprites".
