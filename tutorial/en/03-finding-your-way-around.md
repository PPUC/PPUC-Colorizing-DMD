# Finding Your Way Around

## The window

- **The canvas**, in the middle: the colorized frame above, the original frame below it. You
  paint on the colorized one; the original is your reference, and masks are drawn on it.
- **The frame strip**, along the bottom: every frame with its thumbnail (colorized above,
  original below), its number and a line of flags such as its duration, `HD`, `Dyn` or its
  background. The mouse wheel moves it one frame per turn.
- **The left sidebar**: the project's comparison masks, backgrounds, sprites and scenes.
- **The Inspector**, on the right: sections for the current frame (`Frame`, `Comparison Mask`,
  `Background`, `Sprite`, `Dynamic Sets`, `Palette`, `Color Rotations`, `Color Sets`). A click on
  a header opens or closes a section; several can be open at once. `Quick Actions`, the first,
  holds the menu entries you use most as buttons; `Choose...` there picks them.
- **The toolbar**: the drawing tools, the four favorite colors and the draw color, the modes
  (`SD`, `HD`, `Mask`, `Dynamic`, `Background`), playback and the `Runtime` switch.
- **The row of edit colors** under the toolbar: one color per shade of the original.

## Moving through the frames

- A click on a frame in the strip makes it the current frame. `Left` and `Right` step through
  the frames.
- `Ctrl/Cmd`+click adds a frame to the selection or takes it out, `Shift`+click selects a range.
  Most edits apply to every selected frame: painting, masks, triggers, durations. A plain click
  on a frame inside the selection keeps the selection and only shows that frame, so you can check
  each of them. `Esc` reduces the selection to the frame shown.
- The toolbar's `<` and `>` go back and forward through what the canvas showed, like a web
  browser.
- The frame filter at the left end of the strip shows only some frames: those with HD, with a
  dynamic zone, with sprites, with triggers, the `Inactive` ones, the frames using the mask,
  background or sprite selected on the left (`Using the selected asset`), and more.

## Sections

A game has parts: attract mode, the start of a ball, modes, the match. Name them. Go to the
first frame of a part and press `Name` at the left end of the strip. The section box then jumps
to any part, and the strip shows the name on the frame that starts it. Sections are only for
you: libserum does not use them.

## Watching the frames move

`Play` steps through the visible frames with their recorded durations; `Loop` starts again at
the end. `Stay` keeps the current frame and plays only its color rotations and its background
scene, so you can watch them while you paint.

## Zooming

`Ctrl/Cmd++` and `Ctrl/Cmd+-` zoom the canvas, `Ctrl/Cmd+0` shows the whole frame again. With
`Ctrl/Cmd` the mouse wheel zooms around the mouse, as does a pinch on a trackpad. Hold `Space`
and drag to move the view.

## Settings

`Settings` holds the look of the DMD pixels (`Pixel Shape`, `Pixel Size`), the language, your
name, the keyboard shortcuts and more. Every shortcut of the menus can be changed there.

Handbook: "Canvas", "Frame List", "Sections", "Playback", "Settings", "Keyboard Shortcuts".
