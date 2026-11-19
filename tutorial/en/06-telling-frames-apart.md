# Telling Frames Apart

## The problem

libserum identifies a frame by a hash of its original. A frame that shows a score of 1,250,000
and the same frame with 1,260,000 are two different frames to it. A game shows millions of
scores; no project could hold a frame for each.

## Comparison masks

A comparison mask hides pixels from the comparison. libserum then compares only the pixels the
mask leaves open. Hide the score, and every frame with that picture is found, whatever score it
shows. One colorized frame stands for all of them.

A mask is shared: many frames can use the same one. Masks are magenta wherever the editor shows
them.

## Making and using a mask

1. In the left sidebar's `Comparison Masks` list, press `New`: an empty mask.
2. Select the frame (or frames) the mask is for and press `Assign Mask` in the Inspector's
   `Comparison Mask` section. A mask dragged from the list onto the canvas does the same.
3. Press `Mask` in the toolbar (or `Edit Mask`). The mask shows over the original frame.
4. Paint on the original frame with any tool: the painted pixels are hidden. `Shift` erases.
   Cover the score's whole area, including the digits it could show, not only those it shows
   now.
5. Press `Mask` again to go back to painting colors.

The mask pictures in the sidebar show the current frame's original under each mask. The mask that
fits a new frame is easy to find there.

## Same frames

The lower part of the `Comparison Mask` section answers the question masks are made for: which
frames can libserum not tell apart?

- `Same frames: N` counts the frames that are the same as the current frame under its mask. The
  frame strip outlines them in green, and `Ctrl/Cmd+Left` and `Ctrl/Cmd+Right` go through them.
- `Delete Same As Current...` deletes those frames: with the mask, they are no longer needed,
  since the current frame stands for them.
- `Delete All Duplicates...` does that for the whole project.

The usual way: paint a mask, look at the same frames, delete them. A dump of thousands of score
frames shrinks to the few pictures that really differ.

## Too much or too little

- A mask that hides too little leaves frames that never match: the score shows a digit outside
  the hidden area.
- A mask that hides too much makes different pictures the same: libserum shows one of the
  frames for all of them. Check the same frames before deleting them.

## Shape mode

`Shape` in the `Comparison Mask` section has `Shape mode`: the frames are compared by the shape
of what is lit, not by its shades. For pictures that the game shows in several brightnesses, a
fade or a blinking text, one frame then stands for all of them.

## The hidden pixels still need colors

A mask decides which frames match. It does not color the pixels it hides: under the mask the
frame still shows its own colored picture, with the score of the frame you painted. The next
chapter colors what changes.

Handbook: "Comparison Masks", "Same Frames And Common Pixels".
