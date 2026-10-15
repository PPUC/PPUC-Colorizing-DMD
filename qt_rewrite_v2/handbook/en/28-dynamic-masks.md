# Dynamic Masks

The `Dynamic` toolbar toggle shows the current frame's dynamic zones: outlined on the colorized
picture, also where two zones of different sets meet, and solid on the original, as a comparison
mask is. The selected set (the one `Dyn Set` paints and edits) is yellow, the others cyan, both
over a dark edge so they show on any picture. Each area of a set (its pixels that touch each other) carries the set's number at its top
left once a DMD pixel is at least 5 screen pixels wide (zoom in for that); the mouse over a zone
names its set and its number of pixels. The pixels stay visible in the colors the runtime gives
them: each one in its set's color for its original shade (the canvas always shows them so),
without switching to the Runtime view.

With `Dynamic` on, the zones are painted on either picture: the point, line, rectangle, circle and
ellipse tools, the flood fill and the pipette work on the original as on the colorized frame,
in the selected set (with `Shift` they erase), as comparison masks are painted on the original.
Both pictures show the same pixels of the plane the zones are on.

The `Dynamic Sets` section lists the sets the frame uses: per set a picture of the frame with that set's
zones in their colors and the rest dimmed, its number and its number of pixels. A click selects
the set, as `Dyn Set` does; the list follows a set chosen there or picked on the canvas. The
last row, `New set (n)`, selects the lowest set the frame does not use yet, to paint a new zone.
Every dynamic mask edit, Undo and Redo also redraws the frame's thumbnail and its `Dyn` flag in
the frame list.

**Dynamic zones belong to one plane: the one with the size of the original frame.** That is the
128x32 (SD) plane in an ordinary project, the 256x64 plane in a project with 256x64 original
frames and the 192x64 plane in one with 192x64 original frames. It is the plane the runtime
takes them from. In an ordinary project libserum renders the 64-row picture in layers: the HD
frame and HD background as they are, and the dynamic content at 128x32, with the SD dynamic
mask and the SD dynamic colors, upscaled and put on top. It does not read the HD dynamic mask
and the HD dynamic colors at all (libserum, `Colorize_Framev2`: with a 64-row request a 128x32
colorization always runs in "layer mode", where `frameHasDynamicExtra` is forced false;
libserum's AGENTS.md, "Resolution scaling and the two-layer renderer": "HD dynamic masks are
ignored outright").

So in an ordinary project:

- Dynamic zones are shown and painted in SD mode. In HD mode the `Dynamic` button is disabled and
  its tool tip says `Dynamic zones are defined on the 128x32 plane: switch to SD to see and paint
  them`. Switching to HD while `Dynamic` is on switches it off, with a message in the status bar.
  The zones are not drawn over the HD frame: at twice the size they would look like something
  that can be painted there, and the HD picture on the canvas already shows what they do.
- The `Dynamic Sets` section works in both modes and always shows and edits the SD plane's dynamic colors
  and shadows, which are the ones both pictures use. In HD mode a note at the top of the page
  says so. An edit made there in HD mode shows in the HD picture at once and is undone from
  either mode.
- The editing canvas and `Frame -> BG` in HD mode render the HD plane as libserum does:
  dynamic pixels have the SD set's color for the original's shade, every SD pixel doubled.
  libserum rounds the edges of that upscale (Scale2x); the Runtime view with
  `Runtime Plane: 64 Rows` shows the exact picture.
- HD dynamic masks, colors and shadows a project already holds are kept: they are loaded,
  written back unchanged and never shown or edited. The Win32 editor writes them, as a
  line-doubled copy of the SD ones.
- One thing of the stored HD data does reach the picture: **shadows**. A dynamic set that has no
  shadow direction on the SD plane takes its direction and color from the HD shadow tables when
  libserum renders the 64-row plane (they are settings per set, not pictures). The editor's HD
  rendering does the same. The `Dynamic Sets` section shows and edits the SD shadow only: a shadow that
  comes from the HD tables can be replaced by setting an SD direction for the set, and cannot be
  switched off in this editor.
- The save rule that writes colorized pixels under a dynamic mask as 0 still uses the stored HD
  mask for the HD frame, see "Saving".

Helpers of the `Edit` menu, on every selected frame and on the dynamic plane only (in HD mode of
an ordinary project they are refused with a message), in the selection if there is one on the
plane and otherwise on the whole frame (the Win32 editor always takes the whole frame):

- `Remove Dynamic Zones` (`Ctrl/Cmd+E`, `Alt+D`): the pixels leave their zones (mask `255`) and get
  the plain color of their original shade, as in the Win32 editor.
- `Assign Dynamic Set` (`Alt+A`): the pixels join the `Dyn Set` of the `Dynamic` page.
- Copy and paste carry the dynamic mask and the colors and shadow of the sets used; `Paste
  Dynamic Mask Only` pastes the mask alone; auto copy and the scrolling fill copy the mask with
  the colors. See "Copy And Paste" and "Auto Copy And Scrolling Fill".

Rules:

- The `Dyn Set` field in the Inspector's `Dynamic` topic selects the dynamic set written by drawing tools.
- The Inspector shows editable dynamic color swatches for the active frame and the selected dynamic set.
- Click a dynamic color swatch to choose a replacement RGB565 color for that original color index.
- The dynamic mask is rendered over the colorized frame of its plane.
- `Mask` and `Dynamic` are mutually exclusive overlay modes.
- Click the colorized frame to set that dynamic-mask pixel to the selected `Dyn Set`.
- With the `Pick` tool, click the colorized frame to select the dynamic set stored under the cursor.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the dynamic mask.
- The flood fill takes its area from the frame, not from the mask: the pixels connected to the
  clicked one that have its color in the frame's colorized picture, or its shade when clicked on
  the original frame. They get the selected set (with `Shift`, none), whatever they held before.
- Hold `Shift` while clicking, drawing, or flood filling to erase dynamic-mask pixels back to `255`.
- Dynamic-mask edits are undoable per frame.
- Dynamic color changes are undoable per frame.
- Dynamic shadow direction and color changes apply to all selected frames that have dynamic data
  and are undoable per frame.
- `Edit > Dyn` under the toolbar copies the edit colors into the selected `Dyn Set` of all selected
  frames that have dynamic data, one undoable step per frame; `Dyn > Edit` copies the current
  frame's set into the edit colors (see "Edit colors and dynamic sets" under "Colors").
- The color swatches and `Shadow Color` open the editor's color picker (see "Color picker").

Dynamic shadows are loaded from and saved back to the legacy `.cROM` dynamic-shadow blocks. The
canvas and thumbnails render shadows after dynamic-color replacement and before sprite
composition. Shadow pixels are written only into neighboring pixels that are not themselves dynamic,
matching the legacy renderer behavior. On the HD plane of an ordinary project a shadow is one HD
pixel wide around the doubled zone, as libserum generates it there.

The Inspector's `Dynamic` topic provides shadow controls for the active dynamic set:

- Direction buttons `TL`, `T`, `TR`, `L`, `R`, `BL`, `B`, and `BR` toggle where shadows are cast.
- `Shadow Color` opens a color picker for the active set's shadow color.
- Shadow edits apply to all selected frames that have dynamic data.
