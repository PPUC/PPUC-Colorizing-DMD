# ui_dump: UI regression dump for the Qt editor

`ui_dump` answers one question: **did this change alter anything a user can see or do in the
main window?**

It builds the real `app/MainWindow.cpp` into a small executable, runs it offscreen, drives it
through about 530 scripted states and after each one prints every widget and the relevant
document and canvas state. Two such dumps, one of a git revision and one of the working tree,
are then compared.

Use it

- to prove that a refactoring (moving code between classes, extracting panels, renaming) changes
  nothing visible: the comparison must end with `REAL DIFFERENCES: 0`;
- to review an intended UI change as a diff: the comparison lists exactly which widgets, texts,
  states or focus positions changed, and nothing else should show up.

It is a developer tool, not a test. It is not part of `ctest` and is not built by default.

## Running it

Requirements: the normal build requirements of `qt_rewrite_v2` (CMake, Qt 6 Widgets, GCC or
Clang, a libserum checkout), plus `git`, `tar` and `python3`.

All commands are run from the repository; nothing is written into it. Builds and output go to
`$UI_DUMP_WORK_DIR`, by default `${TMPDIR:-/tmp}/colorizingdmd_ui_dump`.

```sh
# 1. Dump the committed state (HEAD). The working tree is not touched: the revision is
#    exported with "git archive" into the work directory and built there.
qt_rewrite_v2/tools/ui_dump/run.sh baseline

# 2. Change the code, then dump the working tree and compare it with the baseline.
qt_rewrite_v2/tools/ui_dump/run.sh after

# Compare again without rebuilding, e.g. with other options.
qt_rewrite_v2/tools/ui_dump/run.sh compare
```

Variations:

```sh
# Baseline of another revision.
qt_rewrite_v2/tools/ui_dump/run.sh baseline HEAD~3

# Another work directory (must be outside qt_rewrite_v2).
UI_DUMP_WORK_DIR=$HOME/ui_dump_work qt_rewrite_v2/tools/ui_dump/run.sh baseline

# Another libserum checkout than the one next to the repository. An exported baseline tree has
# none beside it, so run.sh always passes the checkout to both builds.
COLORIZINGDMD_LIBSERUM_DIR=$HOME/libserum qt_rewrite_v2/tools/ui_dump/run.sh baseline

# Arguments after "after" or "compare" go to compare.py.
qt_rewrite_v2/tools/ui_dump/run.sh after --allow-removed '^\[hidden sprite selection combo\] '
```

`baseline` runs the dump twice and fails if the two runs differ, so a baseline is known to be
deterministic. `after` and `compare` exit with status 1 when there are real differences.

Files in the work directory:

| File | Content |
| --- | --- |
| `before.txt`, `after.txt` | the dumps, about 40 MB each |
| `before.err`, `after.err` | stderr of the dump runs (Qt offscreen warnings, script errors) |
| `build_baseline.log`, `build_after.log` | configure and build output |
| `baseline_tree/` | the exported revision |
| `build_baseline/`, `build_after/` | build directories |

A dump depends on the Qt version, the platform and the fonts. Only compare dumps made on the
same machine, and do not keep them in the repository.

Both dumps are made with the scripts of the working tree; only the application sources come
from the revision. The scripts therefore have to compile against both trees (see "When
MainWindow changes").

### Building it with the project

```sh
cmake -S qt_rewrite_v2 -B <build dir> -DCOLORIZINGDMD_QT_V2_BUILD_UI_DUMP=ON
cmake --build <build dir> --target colorizing_qt_v2_ui_dump
QT_QPA_PLATFORM=offscreen <build dir>/tools/ui_dump/colorizing_qt_v2_ui_dump > dump.txt
```

The option is off by default. This builds the dump of the current tree only and is mainly useful
to keep the scripts compiling while working on `MainWindow`, or to compare by hand with
`compare.py before.txt after.txt`. `run.sh` does not use it: it configures
`tools/ui_dump/CMakeLists.txt` as a standalone project with `-DUI_DUMP_TREE=<tree>`, which is
what allows it to build against an exported revision.

## Reading the output

A dump is a sequence of sections. A section starts with `===== title =====` and contains

- one line per widget below the main window: class, position and size relative to the window,
  visible/enabled, size constraints, focus policy, tool tip, style sheet, and depending on the
  class the text, check state, value and range, list or combo items, current tab and a hash of
  a label pixmap. The lines of a section are sorted;
- `---` lines with state that is not a widget: status bar text, undo/redo availability, hashes
  of the document (palettes, rotations, pixels, sprites, masks, backgrounds, dynamic data), the
  private state of the main window, the preview and the sprite editor, and a hash of a grab of
  the canvas;
- lines printed by the script between two dumps, e.g. the title, text and buttons of a dialog
  that came up, or the size and hash of an exported image.

`===== focus chain =====` sections list every widget in the order of Qt's focus chain, which is
the order the Tab key follows. `fp=` is the focus policy; `fp=0` widgets cannot get the focus.

`compare.py` prints every difference with the section it is in (`<` baseline, `>` working tree)
and ends with a summary:

```
sections: 498 / 498
focus chain sections with moved entries: 0
focus chain sections with a different focusable order: 0
differences outside the focus order: 0
allowed removed lines: 0
REAL DIFFERENCES: 0
```

- **sections**: number of sections in both dumps. If the titles differ, `SECTION LIST DIFFERS`
  is reported; the script was changed between the two dumps or one of them aborted.
- **focus chain sections with moved entries**: focus chain sections that are not identical line
  by line. This alone is *not* counted as a difference. Creating widgets in another order moves
  labels and other `fp=0` widgets in the chain, which nobody can notice, so focus chain sections
  are compared as the sequence of their focusable entries only. A non-zero count here together
  with `REAL DIFFERENCES: 0` means: widgets are created in another order, the Tab order is the
  same.
- **focus chain sections with a different focusable order**: the Tab order changed. Each such
  section counts as one real difference and is reported with the first position that differs.
  To see the whole new order, look at the section in `after.txt`.
- **differences outside the focus order**: everything else. All other sections are compared as
  multisets of lines; each line that exists on one side only counts.
- **allowed removed lines**: see below.
- **REAL DIFFERENCES**: the sum of the two difference counts. 0 means no visible change in
  any scripted state.

### `--allow-removed REGEX`

For a change that intentionally removes widgets. Lines of the baseline that match the regular
expression may be missing in the working tree dump; they are counted and listed under "allowed
removed lines" instead of as differences. Everything else is still compared strictly, including
lines that match but changed instead of disappearing (the new line is reported as added). The
option can be given several times.

Lines of widgets inside the hidden sprite selection combo box start with the tag
`[hidden sprite selection combo] ` so that they can be matched by such an expression; see
`ownerTag()` in `dump.cpp` to tag another group of widgets.

## What it covers and what it does not

Covered, in every scripted state: geometry, texts, tool tips, enabled and visible state, values
and ranges of all widgets, item lists, the focus chain at a few points, the document content,
undo/redo state and the rendered canvas.

Not covered:

- **No real mouse or keyboard events.** The script calls `click()`, `setValue()`,
  `setCurrentIndex()`, `trigger()` and, for the canvas, the handlers the canvas widgets would
  call (`m_preview->m_strokeFinishedHandler`, the sprite editor handlers) and single steps of
  the `CanvasEditController` (`setSelectionFromStroke`). Event filters, mouse tracking, hit testing in the canvas widgets, shortcuts and key handling
  are not exercised.
- **Timers do not fire.** Playback and the color rotation preview are only observed as "timer
  active, interval n"; no frame advances. Only zero-delay timers run, when the script processes
  events.
- **No drag and drop.**
- **Modal dialogs are answered by the script**, not operated: a zero-delay timer finds the
  dialog, prints what it shows and accepts, rejects or presses a button. Native dialogs are
  never shown.
- Only the offscreen platform, only one window size, no high-DPI variants, no styling
  differences between platforms.
- Menu entries are not dumped (actions are not widgets); only whether undo and redo are
  enabled.
- States nobody scripted. A change in a code path the scripts do not reach is invisible.

The tool uses its own `QSettings` organisation and application name, so it does not read or
change the settings of the real application.

## Adding scripted states

The script is plain C++ in three places, executed in this order:

| File | Content |
| --- | --- |
| `dump.cpp`, `main()` | empty window, color rotation page, palette page, gradient tools |
| `sprite_script.inc` | sprite page, sprite editor canvas, capture selection, detection areas |
| `inspector_script.inc` | frame, mask, background and dynamic pages, image import/export, dialogs, undo/redo, playback controls |

The `.inc` files are included at the end of `main()` and each form one block.

A state is: do something, then call `dump("title", w)` (`D("title")` in `inspector_script.inc`).
Widgets are looked up by tool tip with `byTip<QSpinBox>(w, "...")`, grid buttons with
`gridButton(w, "grid tool tip", index)`, buttons with a changing tool tip with
`toolButtonByTextPrefix()`. A missing or ambiguous tool tip aborts the run with a message in
the `.err` file. `dumpFocusChain(w)` adds a focus chain section. For dialogs see
`answerMessageBox`, `answerFileDialog`, `answerColorDialog` and `guarded` in
`inspector_script.inc`.

Fixtures are built in code, there are no fixture files in the repository:

- the project documents are `ProjectSnapshot` values assembled at the top of each script
  (`snapshot` in `dump.cpp`, `ss` in `sprite_script.inc`, `is` and `loadInspector()` /
  `loadPlain()` in `inspector_script.inc`) and loaded with `ProjectDocument::fromSnapshot`;
- images for the import tests are written to a temporary directory by `inspector_script.inc`
  and removed at the end.

Rules:

- **Append, do not insert.** The window is not reset between states, so every state depends on
  all states before it. A state inserted in the middle changes the ones after it; one added at
  the end of a script block changes nothing else.
- **Extend the script before taking the baseline**, not between `baseline` and `after`. Both
  dumps must come from the same script, otherwise the section lists differ. After editing a
  script, run `baseline` again.
- Keep everything deterministic: no time, no random numbers, no absolute paths in the output
  (print file names, not paths).
- New states can only use what exists in both trees that are compared.
- `dump()` processes pending events once. A change of a button text that changes the width of
  the inspector (the Create/Remove HD buttons while the sprite canvas is toggled) can need more
  rounds before the layout is final; process events a few more times before such a state, as
  `settle()` at the end of `inspector_script.inc` does, or the geometry of the following
  sections depends on timing instead of on the code under test.

## How it reaches MainWindow internals

`MainWindow` has no test interface, and most of the state worth comparing is private. `dump.cpp`
therefore includes the application headers like this:

```cpp
#define private public
#include "FramePreviewWidget.h"
#include "SpriteEditorWidget.h"
#include "MainWindow.h"
#undef private
```

and compiles `app/MainWindow.cpp` of the tree under test into its own executable (the widgets
and core come from the regular `colorizing_qt_v2_widgets` library of that tree).

This is a hack and should be read as one:

- Redefining a keyword is not valid C++. It works with GCC and Clang because access specifiers
  affect neither the object layout nor the symbol names there. It is not expected to link with
  MSVC, where the access level is part of the mangled name.
- Qt and standard library headers are included before the `#define`, so only the three
  application headers and the application headers they include first (`CanvasEditController.h`)
  are affected.
- The application itself is not changed in any way for the tool.

### When MainWindow changes

The scripts use private member and method names of `MainWindow`, `CanvasEditController`,
`FramePreviewWidget` and `SpriteEditorWidget` (`m_document`, `m_frameList`, `m_undoAction`,
`refreshProjectSummary()`, `m_canvasEdit.setSelectionDirection()`, ...) and the tool tips of the
controls. When one of them is renamed, moved
to another class or removed, the tool stops compiling or aborts with `missing tip ...`, and has
to be adapted in the same change.

Because the baseline is built with the same scripts, such an adaptation must still compile
against the baseline revision. For a rename that is not possible directly; either take the
baseline with the old scripts first and keep `before.txt` (`run.sh after` only needs that file),
or split the change so that the rename is a step of its own, checked by building with
`-DCOLORIZINGDMD_QT_V2_BUILD_UI_DUMP=ON`.

The target is built with the project's warning flags, minus `-Wmissing-field-initializers`,
which the fixtures trigger on purpose like the test fixtures do.
