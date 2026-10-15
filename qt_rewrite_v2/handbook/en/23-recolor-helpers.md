# Recolor Helpers

- `Recolor`: `Alt`+click with `Flood` (Win32: Alt+click with the fill tool), or `Edit ->
  Recolor...` and then a click: every pixel that has the clicked pixel's stored color, in the
  current frame or in every selected frame, gets the draw color, or in `Set Replace` mode the edit
  color of its original shade. Refused on a dynamic pixel ("works only for static content", as
  in the Win32 editor). One undoable step per changed frame.
- `Black Original To Black` (`Ctrl+Shift+O`; Win32 `Ctrl+O`, which is `Open` here): pixels
  whose original shade is 0 become black on every selected frame, in the selection if there is
  one on the plane, else everywhere. The original pixel is the ROM pixel the plane pixel shows,
  as libserum lays the planes out; the Win32 editor's handling of 256x64 originals is not
  followed.
- `Remove Dynamic Zones` (`Ctrl+E` or `Alt+D`, as in the Win32 editor) and `Assign Dynamic Set`
  (`Alt+A`), see "Dynamic Masks".
- `Dynamic To Static` (CDMD's `Dyn>Stat`): the dynamic pixels of the selection, or of the whole
  frame, on every selected frame, leave their zone and keep, as a fixed color, the color their
  dynamic set gives their original shade. A dynamic shadow the runtime draws around the zone is
  not kept. Works on the plane dynamic zones are on (128x32 in an ordinary project).
- `Brightness...` (CDMD's brightness box): asks for a percentage (0 to 2000; the last one is
  offered again) and multiplies the red, green and blue of the selected pixels on every selected
  frame by it, rounded down; a channel the factor would leave unchanged still moves one step up
  (above 100 %) or down (below), as in CDMD. Needs a selection.
- `Shadow Next To Selection` (CDMD's shadow buttons): `All Around` or a direction; every pixel
  next to the selection on that side that is neither selected nor dynamic gets the draw color,
  on every selected frame. Needs a selection.
- `Grow Selection` (CDMD's shadow buttons with `Ctrl`): adds the pixels next to the selection,
  all around or on one side. A dynamic shadow (CDMD's shadow buttons with `Alt`) is set on the
  `Dynamic` page.

Each is one undoable step per changed frame. CDMD's `Black And White` (the whole project to
grey, not undoable there) is not offered.
