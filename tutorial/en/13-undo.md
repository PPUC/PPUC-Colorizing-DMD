# Undo

## Two ways back

The `Edit` menu has two pairs of undo and redo.

- **`Undo`** (`Ctrl/Cmd+Z`) takes back your most recent edit, wherever you made it: another
  frame, a mask, a sprite, a palette. If it belongs to a frame, the editor goes there first, so
  you see what changes. **`Redo`** (`Ctrl/Cmd+Shift+Z`) brings back what was undone last. This is
  the undo of other programs and of the Win32 editor.
- **`Undo Here`** (`Ctrl/Cmd+Alt+Z`) takes back the most recent edit of what is on the canvas
  now: this frame, in the mode that is on (colors, `Mask`, `Dynamic`, `Background`), or the
  sprite in the sprite editor. Edits made elsewhere stay. **`Redo Here`**
  (`Ctrl/Cmd+Alt+Shift+Z`) brings back what was undone here.

Why the second pair: every frame, mask, sprite and background keeps a history of its own. You
can work on frame 10, then on frame 200, go back to frame 10 and take back its last stroke with
`Undo Here` without losing the work on frame 200.

## One action is one step

A stroke over ten selected frames, a paste on several frames, an `Auto Copy`: each is one action,
and `Undo` takes it back on every frame it changed.

To keep such an action on some frames only, undo it, then go to each frame that should keep it
and press `Redo Here`. It comes back on that frame alone; `Redo` brings back the rest.

## What has no undo

- The selection has its own undo: `Alt+Z` (`Option+Z`).
- Opening a project starts with empty histories.
- Settings such as the draw color mode are not edits.

Handbook: "Undo And Redo".
