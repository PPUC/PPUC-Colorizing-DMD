# Keyboard Shortcuts

`Ctrl/Cmd` is `Ctrl` on Windows and Linux and `Cmd` (`⌘`) on macOS; `Alt` is `Option` (`⌥`)
on macOS. The menus show each shortcut the platform's way, and so do the tooltips that name a
key. The `Win32` column is CDMD's, a Windows program.

| Keys | Action | Win32 |
|---|---|---|
| `Ctrl/Cmd+C` | Copy the selection | same |
| `Ctrl/Cmd+V` | Paste | same |
| `Ctrl/Cmd+Shift+V` | Paste where the originals match | `Shift`+click in paste mode |
| `Ctrl/Cmd+Alt+V` | Paste the dynamic mask only | `Alt`+click in paste mode |
| `Left`, `Right` (`Shift`: extend the selection) | Previous, next frame of the strip, also on the canvas | same |
| `Ctrl/Cmd+Left`, `Ctrl/Cmd+Right` | Scroll the strip to the previous, next frame that is the same as the current one; the current frame stays | `Ctrl+Left`, `Ctrl+Right` |
| Arrows, `Shift`+arrows, keypad `2 4 6 8` | Move the paste (1 or 10 pixels) | keypad |
| `K`, `L` | Mirror the paste | held when paste starts |
| Click, `Enter` | Stamp the paste (`Shift`, `Alt`: variants) | same |
| `Esc` | End paste mode (also with `Stay In Paste Mode`) or cancel a pending `Select By Color` / `Recolor` click; with nothing to cancel, reduce a selection of frames to the frame shown | - |
| `Ctrl/Cmd+A` | Select all pixels | same |
| `Ctrl/Cmd+Shift+A`, `Ctrl/Cmd+D` | Select none | `Ctrl+D` |
| `Ctrl/Cmd+Shift+I` | Invert the selection | button |
| `Alt+Z` (`Option+Z`), `Alt+Shift+Z` | Undo, redo a change of the selection | `Ctrl+Z` (one undo for everything) |
| `Ctrl/Cmd+B` | Selection from the background mask | same |
| `Ctrl/Cmd+K`, `Ctrl/Cmd+L` | Mirror the selection | same |
| `Ctrl/Cmd`(+`Shift`)+click or drag | Add to (remove from) the selection | same |
| hold `W` | Select non-black pixels only | same |
| `Ctrl/Cmd+Alt`+click, fill tool | Select by color | same |
| `Alt`+click, fill tool | Recolor every pixel of the color | same |
| `Ctrl/Cmd+Shift+O` | Black original to black | `Ctrl+O` |
| `Ctrl/Cmd+E`, `Alt+D` | Remove dynamic zones | same |
| `Alt+A` | Assign the dynamic set | same |
| `Ctrl/Cmd+Alt+Z` | Undo the most recent edit anywhere in the project | `Ctrl+Z` |
| `Ctrl/Cmd+Alt+Shift+Z` | Redo the edit undone most recently anywhere | - |
| `Ctrl/Cmd+Shift+H` | Show or hide all helper lines | - |
| `Ctrl/Cmd+X` | Switch between SD and HD (`View -> Switch SD/HD`) | same |
| `Ctrl/Cmd+Shift+B` | Set or remove the current frame's bookmark | - |
| `Alt+B`, `Alt+Shift+B` | Next, previous frame with a bookmark | - |
| `Alt+R` | Go to where the frames moved last were | - |
| `Esc` while dragging a helper line | Put the line back | - |

With a text field, a spin box or a list in focus, `Ctrl/Cmd+C`, `Ctrl/Cmd+V`, `Ctrl/Cmd+X` and
`Ctrl/Cmd+A` do what they do there (`Ctrl/Cmd+A` in the frame list selects every frame). A single
letter cannot be a menu shortcut (a native macOS menu bar takes it from every widget), so `Invert
Selection` is not `I`; `K`, `L` and `W` act only while the frame canvas has the focus. Other
shortcuts of the editor: the platform's keys for Open (`Ctrl/Cmd+O`), Save, Save As, Quit, Undo and
Redo, the platform's help key for `Help -> Handbook` (`F1`; `Cmd+?` on macOS), `Ctrl/Cmd+R` Runtime
view, `Ctrl/Cmd+3` / `Ctrl/Cmd+6` its planes, `Delete` / `Backspace` delete frames in the frame
list, and the edit color keys on the canvas (see "Keys for the edit colors"). No two actions share a
key (`tools/ui_dump` prints every shortcut of the window and the shared ones).

The table names the keys the editor comes with.

## Changing the keys

`Settings` -> `Shortcuts` lists every entry of the menus with its keys; the search field above finds
one by its name or key. Select an entry, click the `Keys` field and press the keys: they replace
what the entry had. `None` leaves it without a key, `Default` gives its own back, `Restore All
Defaults` does so for all of them. `OK` applies the changes at once, and they are kept for the next
start. They are the editor's, not a project's.

Not every key can be given, and the note below the field says why:

- A key alone, also with `Shift`, needs `Ctrl/Cmd` or `Alt`: without them it acts on the canvas and
  in text fields. Function keys (`F1`, `F2`, ...) work alone. On a Mac keyboard the function keys
  often change the brightness or the volume instead, unless `Fn` is held or the system's keyboard
  settings say otherwise, and macOS itself uses some of them.
- A key another entry has stays there: change that entry first.
- `Ctrl/Cmd+Left`, `Ctrl/Cmd+Right` and `Alt` with an arrow key belong to the frame strip. `Back`
  and `Forward` keep theirs on Windows and Linux, as before: in the strip `Alt+Left` and `Alt+Right`
  move frames.

What is not in a menu keeps its keys: the arrow keys, `Esc`, `Enter`, `W`, `K`, `L`, the paste keys
and the edit color keys. The tooltips of the toolbar's `<`, `>` and zoom buttons name the key you
chose. Entries of `Open Recent` and of plugins change with what is there, so they have none to set.

## macOS menu entries

On macOS the `Edit` menu ends with `Writing Tools`, `AutoFill`, `Start Dictation...` and
`Emoji & Symbols`. They are not the editor's: macOS adds them to the `Edit` menu of every
application. They work on text being typed (a section or palette name, for example) and do
nothing on the canvas.
