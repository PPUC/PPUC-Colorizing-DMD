# Colors

## Draw color, favorites and edit colors

The active draw color is what `Point`, the shapes and `Flood` paint with in single color mode.
It is shown in the read-only box at the end of the favorite swatches of the toolbar. It is set
by a click on a favorite, a palette color, an edit color or with the `Pick` tool.

- **Favorites**: four toolbar swatches (red `0xf800`, green `0x07e0`, blue `0x001f`, black
  `0x0000`). A click makes one the draw color; drag a palette color onto one, or use its context
  menu `Edit Color...`, to change it. Favorites are not saved in the project.
- **Edit colors**: the row under the toolbar shows the project's edit colors, the 16 colors the
  `.cRP` keeps (`acEditColorsS` of the Win32 editor). A project with 4 colors shows 4 swatches,
  one with 16 colors 16: one per shade of the original frames. Under each swatch a strip in the
  shade's own color (the amber ramp of the original frame) shows which shade the edit color
  stands for in color set replace mode.
  - Click a swatch to select it: its edit color becomes the draw color, and stays the draw color
    when the edit color changes (undo, a loaded color set, `Dyn > Edit`). Choosing another draw
    color (favorite, palette, `Pick`) leaves no swatch selected.
  - Double-click a swatch, or use its context menu `Edit Color...`, to change the edit color in
    the color picker. Drag a palette color onto a swatch to set it.
  - The tool tip names the color, the shade it replaces and its key.
- Edit colors are project data: they are saved in the `.cRP`, and every change is undoable (see
  "Undo And Redo").

## Draw color modes

The two buttons at the start of the row choose how `Point`, the shapes, `Flood` and multi-frame
edits color the frame (the Win32 editor's `IDC_1COLMODE` / `IDC_4COLMODE`):

- `1 Color`: every painted pixel takes the draw color.
- `Set Replace` (color set replace, the Win32 "4 color mode"): every painted pixel takes the edit
  color of **its own original shade**: a pixel whose original frame pixel has shade 2 becomes edit
  color 2. In a 16-color project shades 0 to 15 take edit colors 0 to 15.
  - On several selected frames each frame uses its own original frame, so the same stroke can
    give each frame other colors. (The Win32 editor's `Point` tool takes the shades of the current
    frame for all selected frames, its shapes and fills each frame's own; here all tools take each
    frame's own.)
  - On the HD plane of an ordinary project an HD pixel takes the shade of the original pixel it
    shows (the one at half its coordinates). On the 256x64 plane of a project with 256x64
    originals and on the 192x64 plane of a project with 192x64 originals the plane lines up with
    the original frame pixel for pixel.
  - `Flood` fills the same area as in `1 Color` mode: the pixels of the current frame connected to
    the clicked one that have its color (the Win32 fill does not depend on the mode either). Each
    selected frame then gets its own shades' edit colors in that area. Unlike `1 Color`, a fill
    on an area that already has the draw color still runs.
  - While a background is edited (`Edit BG`), the background takes the edit colors of the
    current frame's shades (a background has no original frame of its own).
  - The `Grad` tool and `Grad Sel` always paint palette gradients; the Win32 editor's third mode
    (gradient) is that tool here. The sprite canvas always paints with the draw color, as in the
    Win32 editor. The preview while dragging shows the shape in the draw color.
  - `Shift` does not erase frame pixels in either mode.

The mode is stored in the `.cRP` (`DrawColMode`), as the Win32 editor stores it: a project saved
in color set replace mode opens in it. A stored `2` (the Win32 gradient mode) opens in `1 Color`
mode, as the Win32 loader does, and is kept in the file until the mode is changed. Changing the
mode is not an edit: it is neither undoable nor does it mark the project as changed; the next
save writes it.

## Keys for the edit colors

While the frame canvas has the keyboard focus (click it), keys select an edit color slot. The keys
are chosen by their place on the keyboard, as in the Win32 editor:

| Slots | QWERTY | AZERTY (Win32 names) |
|---|---|---|
| 0-3 | `1` `2` `3` `4` | `1` `2` `3` `4` |
| 4-7 | `Q` `W` `E` `R` | `A` `Z` `E` `R` |
| 8-11 | `A` `S` `D` `F` | `Q` `S` `D` `F` |
| 12-15 | `Z` `X` `C` `V` | `W` `X` `C` `V` |

The letters work in 16-color projects only, and not with `Shift`; no key works with `Ctrl`,
`Alt` or `Cmd`. None of them is a shortcut elsewhere in the editor. The place of a key is read
from the native key code where the platform gives one (the macOS virtual key, the Windows scan
code, the X11/Wayland key code); elsewhere the key is taken by its letter on a QWERTY layout. The
keys are not configurable yet.

## Color sets

The Inspector's `Color Sets` section lists the 64 color sets of the `.cRP` (the Win32 color set dialog):
the number, the name and the colors of each active set, `(inactive)` for the others.

- `Store` (Win32 `Set`): the selected set takes the edit colors and becomes active. Only as many
  colors as the project has are stored (4 or 16); the other entries of the set stay as they
  were.
- `Load`, or a double click on an active set (Win32: a click): the edit colors take the set's
  colors, again only as many as the project has.
- The name field renames the selected active set when it is left or `Enter` is pressed (63
  characters at most). Inactive sets cannot be renamed, as in the Win32 dialog.
- `Deactivate` makes the set inactive and keeps its colors and name; `Store` activates it again.
  The Win32 editor has no way to deactivate a set.
- All of these are undoable. Sets, names and active flags are saved in the `.cRP`; a project that
  is opened and saved without changing them writes their bytes unchanged, including what the
  Win32 editor left behind the end of a name.
- The `.cRP` also stores the first set the Win32 dialog showed: the list scrolls to it when a
  project is opened, and scrolling the list by hand updates it (not counted as a change, like the
  draw color mode). The current set the file names is selected when a project is opened; the
  Win32 editor never changes that value, and neither does this one.

## Color picker

Everywhere a color is chosen (edit colors, favorites, palette colors, dynamic colors, the
dynamic shadow color, color rotation colors) the editor's color picker opens. It has the fields
of the Win32 picker, all kept in step with each other:

- a hue/saturation square and a value bar,
- `RGB888` as six hex digits,
- `R8 G8 B8` (0-255),
- `RGB565` as one decimal number (0-65535),
- `R5 G6 B5` (0-31, 0-63, 0-31), each of which changes its own component only,
- `H S V`: hue in degrees, saturation and value in percent, with one decimal,
- the last 8 picked colors (most recent first; a click takes one), kept in the application
  settings and shared by all projects,
- `Before` and `Now`.

The result is the RGB565 color of the fields. A color that is shown and entered again in any field
gives the same RGB565 value back; that holds for all 65536 RGB565 colors (with whole numbers for
H, S and V, as the Win32 picker shows them, about 10,000 colors would come back different).

## Edit colors and dynamic sets

`Edit > Dyn` (Win32 `IDC_COLTODYNA`) copies the edit colors into the current dynamic set
(`Dyn Set` on the `Dynamic Sets` section) of every selected frame that has dynamic data; `Dyn > Edit`
(`IDC_DYNATOCOL`) copies the current frame's dynamic set into the edit colors. Both use the plane
dynamic data is edited on (see "Dynamic Masks") and as many colors as the project has. `Edit >
Dyn` is one undoable step per frame, in the frame's dynamic color history; `Dyn > Edit` is one step
in the edit color history.
