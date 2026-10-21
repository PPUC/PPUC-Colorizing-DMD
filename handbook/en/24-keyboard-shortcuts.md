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
| Arrows, `Shift`+arrows, keypad `2 4 6 8` | Move the paste (1 or 10 pixels) | keypad |
| `K`, `L` | Mirror the paste | held when paste starts |
| Click, `Enter` | Stamp the paste (`Shift`, `Alt`: variants) | same |
| `Esc` | Cancel paste mode or a pending `Select By Color` / `Recolor` click | - |
| `Ctrl/Cmd+A` | Select all pixels | same |
| `Ctrl/Cmd+Shift+A`, `Ctrl/Cmd+D` | Select none | `Ctrl+D` |
| `Ctrl/Cmd+Shift+I` | Invert the selection | button |
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
| `Esc` while dragging a helper line | Put the line back | - |

With a text field, a spin box or a list in focus, `Ctrl/Cmd+C`, `Ctrl/Cmd+V` and `Ctrl/Cmd+A` do what they do
there (`Ctrl/Cmd+A` in the frame list selects every frame). A single letter cannot be a menu
shortcut (a native macOS menu bar takes it from every widget), so `Invert Selection` is not `I`;
`K`, `L` and `W` act only while the frame canvas has the focus. Other shortcuts of the editor:
the platform's keys for Open (`Ctrl/Cmd+O`), Save, Save As, Quit, Undo and Redo, the platform's
help key for `Help -> Handbook` (`F1`; `Cmd+?` on macOS), `Ctrl/Cmd+R` Runtime
view, `Ctrl/Cmd+3` / `Ctrl/Cmd+6` its planes, `Delete` / `Backspace` delete frames in the frame list,
and the edit color keys on the canvas (see "Keys for the edit colors"). No two actions share a
key (`tools/ui_dump` prints every shortcut of the window and the shared ones).

## macOS menu entries

On macOS the `Edit` menu ends with `Writing Tools`, `AutoFill`, `Start Dictation...` and
`Emoji & Symbols`. They are not the editor's: macOS adds them to the `Edit` menu of every
application. They work on text being typed (a section or palette name, for example) and do
nothing on the canvas.
