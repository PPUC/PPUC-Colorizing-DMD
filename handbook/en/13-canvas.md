# Canvas

The central canvas shows:

- The active colorized frame region.
- The original/reference frame region below it. It always shows every pixel of the original
  frame and has the same on-screen size as the SD frame: 128x32 pixels, or 256x64 pixels of
  half the size in a project with 256x64 original frames, where the colorized region above it is
  always the 256x64 plane. In a project with 192x64 original frames both regions are 192x64
  pixels of that half size, so they are three quarters as wide.

The canvas does not draw text labels or white frame outlines over the image data.

## Drag And Drop Onto The Canvas

A comparison mask dragged from the `Comparison Masks` list, or a background from the `Backgrounds` list, and
dropped onto the canvas is assigned to all selected frames, as `Assign Mask` and `Assign Background` do;
a scene dropped there becomes the selected frames' trigger (see "Scenes and Scaling").
While it is over the canvas the status bar says what the drop will do. It is undoable like those
buttons: one step, its part of each frame in that frame's settings history, so Undo takes it
back on all of them (see "Undo And Redo"). Frames without background data are skipped for a background.
Nothing else can be dropped onto the canvas.

## Back and forward

The toolbar's `<` and `>` go back and forward through what the canvas showed, as in a web
browser: frames, backgrounds being edited and sprites in the sprite editor (`Cmd+[` and `Cmd+]`
on macOS, `Alt+Left` and `Alt+Right` elsewhere). A frame counts once it has stayed a quarter of
a second, so stepping through frames notes only where it stopped; playback notes nothing. A
background or a sprite counts once, whichever frame is current meanwhile; going back to it
shows it with the frame that was current. A place includes the timeline, the frame filter
(with the scene for a scene's frames): after a double click on a scene, `<` goes back to the
frame and the timeline before, and changing the filter is a step of its own. While the strip
lists a scene's frames, `Leave Scene` below the frame filter goes back to the last frame and
timeline outside the scene in one step, however many of its frames were visited (all frames,
when the history has none from before). Going somewhere
new drops the places ahead, as a browser does. Deleted frames, backgrounds and sprites are skipped. Opening a project starts a
new history.

## Zoom

The canvas is split into two halves, one above the other: the colorized frame in the upper,
the original in the lower. At `100%` each frame fills its half (in whole screen pixels per DMD
pixel). Zooming in, up to `3200%`, the frames grow beyond their halves, and both halves show the
same part, so the author always sees the same pixels in both. Drawing, selecting, mask and pipette clicks hit the pixel
shown under the mouse at any zoom.

- `View -> Zoom In` (`Ctrl/Cmd++` or `Ctrl/Cmd+=`) and `Zoom Out` (`Ctrl/Cmd+-`) zoom by a quarter around
  the middle of the shown part; `View -> Show Whole Frame` (`Ctrl/Cmd+0`) goes back to `100%`. The
  toolbar has `-`, the zoom (a click goes straight back to the whole frame) and `+`.
- `Ctrl/Cmd` with the mouse wheel, and a trackpad pinch, zoom around the mouse:
  the pixel under it stays there.
- When the frame is larger than its half, scroll bars at the bottom and the right of the canvas
  move the shown part;
  so do the mouse wheel (with `Shift` sideways) and two fingers on a trackpad.
- Hold `Space` with the mouse over the canvas, as in image editors: the pointer becomes an open
  hand, and dragging with the left button moves the shown part instead of drawing. Letting go
  of `Space` returns to the drawing tool. `Space` does this only when no text field has the
  keyboard focus.
- The zoom and the shown part stay when the frame, the plane or the view changes; opening or
  creating a project starts at the whole frame.

Drawing previews are transient overlays. They do not modify project data until mouse release or click commit.

The original/reference region shows the ROM frame's shades on an amber ramp from dark to
bright: 4 steps in a project with 4 original colors, 16 in one with 16 (shades 0, 5, 10 and 15 of
16 have the colors of shades 0 to 3 of 4); the frame thumbnails and the sprite editor's original
use the same colors.

Use the `Mask` toolbar toggle to show the current frame's assigned comparison mask over the original/reference region.
When `Mask` is active, clicks on the original/reference region add pixels to the assigned comparison mask.

Use the `Dynamic` toolbar toggle to show and edit dynamic-mask pixels over the colorized frame.
Dynamic zones belong to the plane that has the size of the original frame; in an ordinary project
that is the SD plane, and the toggle is disabled in HD mode. See "Dynamic Masks".

Use the `Background` toolbar toggle to show and edit the current frame's background-mask pixels over the active colorized frame.
The background-mask overlay and edits follow the active SD/HD mode.

The right end of the status bar shows the pixel under the mouse on the canvas: `Original: 34, 12`
on the original frame, `SD: 34, 12` or `HD: 68, 24` on the colorized frame (in a project with
one plane, that plane's size instead of SD or HD), always in the pixels of the frame the mouse
is on, counted from 0 at the top left. While dragging (a shape, a selection) it adds where the
drag started and the size of the dragged area, `| from 30, 10: 5x3`. So a place can be measured,
and found again on another frame.

Beside it, `Time: Project 113:19:06 / Session 00:00:19` counts the time spent on the project and
in this session: the time you work on it. Both clocks stop when there was no input (mouse, keys,
wheel) in the editor's windows for a minute, and show `(paused)`; the next input starts them
again. A running playback counts as work. (The Win32 editor counted the whole time a project
was open.) The project time is saved with the project (in its project file, also by the
autosave) and goes on from there the next time it is opened; a project made from a dump starts
at 0. Counting does not mark the project as changed.

The time is also kept per author, under the name a save gives you (`Settings -> Projects -> Your
name`, in a team project without one your GitHub login); the clock's tool tip lists each
author's time. In a team project your time goes to the partners with your shared changes, and
theirs comes with a merge: the project time is then the time all of you spent (see "Working As
A Team").

The left sidebar is the visual component browser (the frames run along the bottom of the window
under it and the canvas, see "Frame List"; the right sidebar takes the full height). The choice
above its lists says what they show: `All` records; `Used by the current frame`, only the
comparison mask, the background, the sprites and the scene the current frame uses, following the
current frame once it has stayed a quarter of a second, so that scrolling through the frames
stays quick; or `Not used by any frame`, the comparison masks, backgrounds and sprites no frame
names and the scenes no frame of the game starts (a scene's own frames do not count), to find
what can be deleted. The choice is kept for the next start. Beside `New` and `Delete` each list
says how many there are, `Total: 255`, and while only some are listed how many of them it shows,
`2 of 255`. As in
the frame strip, each row shows the thumbnail with its caption below it (the number and name,
`(HD)`, or a scene's details), wrapped to the sidebar's width, so a narrow sidebar still shows all
of it.

- `Masks` shows comparison-mask thumbnails; selecting one selects that mask in the Inspector `Mask`
  topic for assignment. Selecting does not change any frame, and the selection stays when another
  frame becomes current.
- `Backgrounds` shows background thumbnails; selecting one selects the same background in the
  Inspector for assignment or editing. A double click opens it on the canvas for editing, as the
  Inspector's `Edit Background` does.
- `Sprites` shows sprite thumbnails; selecting one selects the same sprite in the Inspector. A
  double click opens it on the sprite canvas for editing, as the Inspector's `Edit Sprite` does.
- `Scenes` lists the project's scenes (see "Scenes and Scaling"); with a scene selected,
  `Using the selected asset` of the frame filter lists the frames that start it, and a double
  click lists the scene's own frames.
- `Light Effects` lists the speaker light effects (see "Speaker Lights"); with one selected,
  `Using the selected asset` lists the frames that start it.

Frame metadata, dynamic colors, and sprite controls live in selectable topics in the right-side Inspector:

- `Frame` contains frame/background assignment, trigger IDs, DMD render settings, and the HD frame
  actions (`Create 256x64 Frame` alone in a project with 256x64 original frames, none in one with
  192x64 original frames).
- `Mask` contains comparison-mask assignment, comparison mode, and a shortcut into mask editing.
- `Frame Background` contains the selected background ID, its assignment to the frames, copying
  between frame and background, and `Edit Background`; `Background`, shown while the background is
  edited, its import, export and HD image (see "Backgrounds").
- `Dynamic` contains the dynamic set selector and dynamic color swatches. In HD mode of an
  ordinary project it shows the SD plane's colors and shadows, with a note saying so.
- `Rotation` contains color rotation slots for the current frame and active SD/HD mode.
- `Palette` contains the selected cRP palette name and all 64 palette colors.
- `Frame Sprites` contains the frame's sprite slots, assignment, capture, bounds, and
  `Edit Sprite`; `Sprite`, shown in the sprite editor, the sprite's name, detection areas,
  shape mode, dynamic sets and HD data (see "Sprites").

The Inspector shows only the sections that serve the canvas shown, to leave them the room:

- On the frame canvas, every section but `Background` and `Sprite`; `Scene` only for a frame of
  a scene (see "Scenes and Scaling"), and it opens when it comes. For a frame of a scene,
  `Comparison Mask`, `Frame Sprites` and `Dynamic Sets` are hidden: libserum identifies such a
  frame by its marker, finds no sprites on it and takes no dynamic zones from its original.
- While a background is edited (`Edit Background`): `Background`,
  `Palette`, `Color Sets` and `Helper Lines`. `Back to Frame` there, or a double click on a
  frame in the frame strip, goes back to the frame.
- In the sprite editor (`Edit Sprite`): `Sprite`, `Palette`, `Color Sets` and `Helper Lines`.
  `Back to Frame` there, or a double click on a frame, goes back to the frame.

A hidden section stays open or closed as it was and comes back so when the frame canvas returns.

The `Frame` section exposes:

- `Background ID` assigns the shared background for all selected frames. Selected frames without
  background data are skipped.
- A frame has one trigger ID at most (stored as `0xFFFFFFFF` for none, the value the cROM uses).
  Its range says what it does, and each kind has an input of its own:
  - `PUP trigger ID`: a trigger for a PuP pack, 0 to 49999. `None` clears it; `0` is an ordinary
    trigger ID.
  - `Scene`: starts a scene of the project, 50000 to 61999 (see "Scenes and Scaling"). Only the
    project's scenes can be chosen; a frame whose ID names no scene of the project shows it as
    `(no such scene)`.
  - `Monochrome fallback` (trigger ID `65432`): from this frame on, libserum shows the frames in
    monochrome.
  - `Palette-based monochrome fallback` (trigger ID `65431`): from this frame on, libserum shows
    the frames in the colors of this frame's palette.

  While the frame has a trigger of one kind, the inputs of the other kinds are disabled; clear it
  first to choose another kind. An ID of no kind (from an old project) is named below the inputs
  and kept until another trigger is chosen. An input acts on all selected frames: setting a
  trigger leaves frames with a trigger of another kind alone (they are counted as skipped);
  clearing one clears it on the frames that have one of that kind. New frames start without a
  trigger.
- `Duration` edits the per-frame `.cRP` duration for all selected frames.
- `Tags` names the groups the frame is in, separated by commas; a change adds or removes those
  tags on all selected frames (see "Frame List").
- `DOF events` lists what the frame sends to DOF when it shows, as `S5=255 L12=1;0`; a change
  replaces them on all selected frames (see "DOF Events").
- `Background ID`, trigger and `Duration` edits can be undone, see "Undo And Redo".
- `Show Background` renders the assigned background layer into the colorized frame preview and is enabled
  by default.
- `Show Sprites`, beside it, draws the sprites libserum finds on the frame over the colorized
  frame, on by default; off, the frame shows without them, to see or paint what is under a
  sprite. It is enabled when a sprite is in one of the frame's slots. Both also decide what the
  DMD output and the animated GIF export show; the Runtime view always shows libserum's
  picture.
- `Import Image` puts a picture into the selection of the selected frames (see "Importing
  images and movie clips" in "Saving").

How DMD pixels are drawn (`Pixel Shape`, `Pixel Size`) is in the Settings, see "Settings".

The `Background` topic exposes:

- Backgrounds are selected visually from the left sidebar.
- The tab shows the selected background ID.
- `Assign Background` assigns the selected background to all selected frames. Dragging a background
  from the `Backgrounds` list onto the canvas does the same; see "Drag And Drop Onto The Canvas".
- `Remove Background` takes the background off all selected frames (they then have none; the background
  itself stays). It is enabled while one of the selected frames has a background, and undoable.
  The Inspector's `Background ID` set to `None` does the same.
- `Edit Background` switches ordinary drawing tools to the selected background image. If the sprite editor
  is open, it is closed first (`Edit Sprite` is switched off). A background belongs to no frame:
  while one is edited, the original frame's half of the canvas stays black, without the frame's
  overlays.
- `Frame -> Background` replaces the selected background image with the current frame image in the active SD/HD mode.
- `Background -> Frame` copies the selected background image into all selected frames in the active SD/HD mode.
- `Import Image` puts a picture into the selected background (see "Importing images and movie
  clips" in "Saving").
  In HD mode, a background without HD data gets its HD image from the import; undoing the import
  removes that HD image again.
- `Export Background` writes the selected background image as a PNG using the current DMD dot/square render settings.
