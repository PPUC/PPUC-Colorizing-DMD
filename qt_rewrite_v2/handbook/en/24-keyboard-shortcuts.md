# Keyboard Shortcuts

`Ctrl` is `Cmd` and `Alt` is `Option` on macOS.

| Keys | Action | Win32 |
|---|---|---|
| `Ctrl+C` | Copy the selection | same |
| `Ctrl+V` | Paste | same |
| `Ctrl+Shift+V` | Paste where the originals match | `Shift`+click in paste mode |
| `Ctrl+Alt+V` | Paste the dynamic mask only | `Alt`+click in paste mode |
| Arrows, `Shift`+arrows, keypad `2 4 6 8` | Move the paste (1 or 10 pixels) | keypad |
| `K`, `L` | Mirror the paste | held when paste starts |
| Click, `Enter` | Stamp the paste (`Shift`, `Alt`: variants) | same |
| `Esc` | Cancel paste mode or a pending `Select By Color` / `Recolor` click | - |
| `Ctrl+A` | Select all pixels | same |
| `Ctrl+Shift+A`, `Ctrl+D` | Select none | `Ctrl+D` |
| `Ctrl+Shift+I` | Invert the selection | button |
| `Ctrl+B` | Selection from the background mask | same |
| `Ctrl+K`, `Ctrl+L` | Mirror the selection | same |
| `Ctrl`(+`Shift`)+click or drag | Add to (remove from) the selection | same |
| hold `W` | Select non-black pixels only | same |
| `Ctrl+Alt`+click, fill tool | Select by color | same |
| `Alt`+click, fill tool | Recolor every pixel of the color | same |
| `Ctrl+Shift+O` | Black original to black | `Ctrl+O` |
| `Ctrl+E`, `Alt+D` | Remove dynamic zones | same |
| `Alt+A` | Assign the dynamic set | same |

With a text field, a spin box or a list in focus, `Ctrl+C`, `Ctrl+V` and `Ctrl+A` do what they do
there (`Ctrl+A` in the frame list selects every frame). A single letter cannot be a menu
shortcut (a native macOS menu bar takes it from every widget), so `Invert Selection` is not `I`;
`K`, `L` and `W` act only while the frame canvas has the focus. Other shortcuts of the editor:
the platform's keys for Open (`Ctrl+O`), Save, Save As, Quit, Undo and Redo, the platform's
help key for `Help -> Handbook` (`F1`; `Cmd+?` on macOS), `Ctrl+R` Runtime
view, `Ctrl+3` / `Ctrl+6` its planes, `Delete` / `Backspace` delete frames in the frame list,
and the edit color keys on the canvas (see "Keys for the edit colors"). No two actions share a
key (`tools/ui_dump` prints every shortcut of the window and the shared ones).

## macOS menu entries

On macOS the `Edit` menu ends with `Writing Tools`, `AutoFill`, `Start Dictation...` and
`Emoji & Symbols`. They are not the editor's: macOS adds them to the `Edit` menu of every
application. They work on text being typed (a section or palette name, for example) and do
nothing on the canvas.
