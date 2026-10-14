# ui_dump: UI regression dump for the Qt editor

`ui_dump` answers one question: **did this change alter anything a user can see or do in the
main window?**

It builds the real `app/MainWindow.cpp` into a small executable, runs it offscreen, drives it
through about 850 scripted states and after each one prints every widget and the relevant
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

# The change appends states to the end of the script that the baseline cannot run.
qt_rewrite_v2/tools/ui_dump/run.sh after --allow-appended

# The change rewrites one group of states on purpose; everything else must stay the same.
qt_rewrite_v2/tools/ui_dump/run.sh after --ignore-sections '^===== 256 original: '

# The change adds a widget to the toolbar or makes the window wider in another way.
qt_rewrite_v2/tools/ui_dump/run.sh after --ignore-geometry
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
- `---` lines with state that is not a widget: status bar text, one hash over the thumbnails
  of all frame list rows (see "Frame list thumbnails"), undo/redo availability, hashes
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

### `--allow-appended`

For a change that adds states for something the baseline revision cannot do, e.g. open a new
kind of project. Take the baseline with the script as it was, append the new states at the very
end of the script (after everything else, see `big_original_script.inc`), and compare with this
option: the working tree dump may then have more sections at its end. They are counted as
"appended sections, not compared"; all sections the two dumps share are compared as usual, so
`REAL DIFFERENCES: 0` still means that no state the baseline has changed.

### `--ignore-sections REGEX`

For a change that alters the behaviour of one kind of project on purpose and has to rewrite its
scripted states, while all other states must not change. Sections whose title line
(`===== title =====`) matches the expression are removed from both dumps before they are
compared, and counted as "ignored sections, not compared: before / after". The remaining
sections are compared as usual, including their number and order.

This only separates the two groups if the ignored states cannot influence the others. The states
of `big_original_script.inc` qualify: they all carry the title prefix `256 original: `, run after
everything else on a window whose project was closed, and print nothing before their first
section. Take the baseline before touching the script (the baseline is made with the script as it
is then), rewrite the script, and compare with

```sh
qt_rewrite_v2/tools/ui_dump/run.sh after --ignore-sections '^===== 256 original: '
```

`REAL DIFFERENCES: 0` then means: no state of an ordinary project changed. The ignored states
are reviewed by reading them in `after.txt`.

### `--ignore-geometry`

For a change that makes the main window wider or moves widgets on purpose. The window is as wide
as its toolbar row needs, so one more toolbar button, or a longer text in the frame status label
at the end of that row, widens the window and with it the canvas: every widget right of the
change gets another position in every state, and every canvas grab another size and hash. That
is hundreds of thousands of reported differences with nothing to read in them. With this option
the position and size of every widget (`@x,y WxH`) and the `--- canvas grab` lines are left out
of the comparison, and the summary says `widget geometry and canvas grabs: not compared`.
Everything else is compared as usual: texts, tool tips, states, item lists, the focus order,
document hashes, thumbnails and what the scripts print.

Because the canvas pictures are not compared then, keep such a change apart from changes to what
is drawn: make the widening change alone, compare it with `--ignore-geometry`, and take the
working tree's dump as the baseline for what follows (below).

### A baseline of the working tree

`run.sh baseline` dumps a git revision. To compare two states of the working tree, for example
before and after the second of several uncommitted changes, dump the first with `run.sh after`
and copy `after.txt` over `before.txt` in the work directory (keep the old `before.txt` under
another name if it is still needed); `run.sh after` and `run.sh compare` only read that file.

Lines of widgets inside the hidden sprite selection combo box start with the tag
`[hidden sprite selection combo] ` so that they can be matched by such an expression; see
`ownerTag()` in `dump.cpp` to tag another group of widgets.

### Frame list thumbnails

The application builds a frame list thumbnail when its row comes into view
(`app/LazyListIcons`), so in an offscreen dump most rows of a long list would still show the
placeholder, and which ones would depend on the window size. Before every dump the tool
therefore calls `LazyListIcons::ensureBuilt()` for all rows, the same function the list uses
for the rows in view, and prints one hash over all thumbnails as
`--- frame thumbnails: <rows> hash=<n>`. A tree that builds its thumbnails up front has no
`LazyListIcons.h`; `dump.cpp` checks for the header with `__has_include` and then hashes the
icons as they are. The line is therefore the same for both kinds of tree whenever every row
ends up with the same picture.

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
  enabled, and each menu as one hidden `QMenu` line. A toolbar button made from an action shows
  as the button.
- States nobody scripted. A change in a code path the scripts do not reach is invisible.

The tool uses its own `QSettings` organisation and application name, so it does not read or
change the settings of the real application.

## Adding scripted states

The script is plain C++ in eleven places, executed in this order:

| File | Content |
| --- | --- |
| `dump.cpp`, `main()` | empty window, color rotation page, palette page, gradient tools |
| `sprite_script.inc` | sprite page, sprite editor canvas, capture selection, detection areas |
| `inspector_script.inc` | frame, mask, background and dynamic pages, image import/export, dialogs, undo/redo, playback controls |
| `big_original_script.inc` | a project with 256x64 original frames, which has the HD plane only: every inspector page, no SD and HD buttons, frames, a background and sprites without HD data and their repair with `Create 256x64 Frame` / `Create 256x64 Sprite`, the `Not rendered` filter, comparison mask editing and undo on the original, dynamic overlay, background layer, sprite assignment and capture on the native grid, the sprite canvas |
| `native_original_script.inc` | a project with 192x64 original frames, which is edited on its 192x64 plane only: every inspector page, no SD/HD buttons and no Create/Remove HD buttons, painting, comparison mask and dynamic mask editing with undo at coordinates beyond 128x32, sprite capture and slot bounds on the native grid, the sprite canvas; it prints whether a save writes the discarded 96x32 plane as "no extra data" and whether a second save is identical |

| `runtime_view_script.inc` | the Runtime view, in a 128x32, a 256x64-original and a 192x64-original project: on and off through its menu actions, both runtime planes of a frame with and without HD, the PNG export (`File -> Export Current Frame PNG...`, which writes libserum's output), an edit, undo and redo while the view is on, a click on the canvas, playback through libserum and one playback step, the sprite canvas ending the view, a frame libserum has no picture for; after each dump it prints the view's state, which is not a widget. Then, with the title prefix `identification: `, a project with an inactive frame and three frames that share an original: the `inactive` marker in the frame list and the status line, the `Inactive` filter and a frame leaving it when it is colorized, the export refusing the inactive frame, the hint in the status line when a machine would identify a frame's original as another frame, and playback that identifies another frame than the one fed |
| `structure_script.inc` | adding and deleting records, title prefix `structure: `: deleting the selected frames through the Edit menu action (cancelled, accepted, undone, redone, a section start, the last frame, all frames refused), `Delete Frames Shorter Than...` with its input dialog, a deletion under a frame filter, a stroke history behind and before a deleted frame, then `New` and `Delete` on the `Masks`, `Backgrounds` and `Sprites` pages of the left browser with undo and redo, a mask, a background and a sprite in use, the refused sprite names, a deletion while the sprite canvas is on; then briefly a 256x64-original project (a new sprite and background with their 256x64 data, no background from a frame without it) and a 192x64-original one. After each dump it prints what the dump does not hold: record counts, the sprite slots of every frame, sections, the sprite tables of the cRP, the browser selections and the frame list's menu actions |
| `dump_import_script.inc` | frame dumps and the same-frame tools, title prefix `dump import: `: `File -> New Project From Dump...` (no file chosen, the import dialog cancelled, a dump with a bad line, a project created with a ROM name of its own and the duration filter), the first save of the new project (cancelled, answered, the second save without a question), `File -> Add Frames From Dump...` with undo and redo, a dump of another size refused; then the comparison side of the `Mask` page: `Spot Common`, `Common To Mask` with undo and redo, `Update Same List`, going to a same frame, `Frames using` and `Select Them`, the three deletions of same frames with `Keep selected frames` and their undo; then the question before a project with changes is replaced and a 192x64 and a 256x64 project from a dump. The dumps are generated into a temporary directory. The script answers the import dialog after `waitForAnalysis()`, so no timer is involved, and prints what the dialog shows. After each dump it prints the window title, whether the project has files, every frame's duration, mask and shape mode, the masks and the selected frames |
| `fixes_script.inc` | the bug fixes of 2026-10-14, title prefix `fixes: `, in a small 128x32 project whose frames all carry dynamic data and one of which has a gap in its sprite slots: `Dyn` and the `Dynamic` filter only for a frame with a dynamic pixel, the thumbnail and `Dyn` after a dynamic mask edit and its undo, the mask browser icon after a mask edit, undo and redo (printed as a hash), a mask ID without a mask refused, the single selection after `selectFrameNear`, `Remove` closing the slot list up, `Assign` into an empty slot behind the first one, the `ref. problem` marker, and the question about unsaved changes for Quit, Open Project, New Project From Dump and Open Recent (answered through `m_unsavedChangesAnswer`: Cancel, Save that fails, Discard). After each dump it prints the mask icon hash, the selection, the questions asked and every frame's sprite slots |
| `colors_script.inc` | edit colors, color sets and color set replace drawing, title prefix `colors: `, in a 4-color and a 16-color 128x32 project whose three frames have other shades at the same places: selecting an edit color, `Set Replace` with a rectangle on two frames, a line on the HD plane and a flood fill on two frames, their undo, the pipette with and without `Shift`, a dropped edit color and its undo, an edit color from the picker (the script prints the picker's fields before and after entering a hex value and an R5 value) and a cancelled picker, the keys of the slots, `Store`, renaming, `Load`, `Deactivate` and undo on the `Sets` page, scrolling the set list by a program and by the user, `Edit > Dyn` on three frames and `Dyn > Edit` with undo, a 16-color project stored in `Set Replace` mode, a palette color from the picker. After each dump it prints the mode, the selected slot, the draw color, the stored mode and first displayed set, the edit colors, sets 0 to 3, some pixels of every frame and dynamic set 0 |
| `selection_script.inc` | the selection, copy and paste and the helpers of the `Edit` menu, title prefix `selection: `, in a 128x32 project of six frames (three with the same flat original, one with a dynamic zone and HD, one with a background, three that are a vertical scroll): every shortcut of the window and the shared ones, selecting with `Ctrl` (and macOS `Control`, which is Meta to Qt) and `Shift` with a rectangle, a point and the fill tool, `Select All`, `Invert`, `Select None`, mirroring, the background mask, `Auto Select...` with its dialog, `Select By Color` from the menu and with `Ctrl+Alt`+click, `Copy`, paste mode (following the mouse, arrow and keypad keys, `K` and `L`, `Esc`), a paste on two frames with undo and redo, the paste variants by menu, `Enter` and `Alt`+click, the refusal on the HD plane, `Auto Copy...` applied and cancelled with the dialog's text, `Scrolling Auto Fill...` with its question and without a run, `Recolor` from the menu, refused on a dynamic pixel and with `Alt`+click, `Black Original To Black`, `Assign Dynamic Set` in a selection, `Remove Dynamic Zones` with undo, the dynamic helpers refused on HD. Clicks and drags go through the canvas handlers with the press modifiers set (`m_pressModifiers`), keys through the canvas's event filter. After each dump it prints the selection, the clipboard, the paste mode and placement, the pending click, and sample pixels and dynamic mask values of every frame |

`selection_script.inc` is compiled only for a tree that has `app/PasteModeController.h` and runs
last; a baseline of an older revision is compared with `--allow-appended`. The tree it was
introduced in added only menu entries, which are not widgets, so no state before it changed
(`REAL DIFFERENCES: 0` against the tree before, with and without the new states).

`fixes_script.inc` is compiled only for a tree that has `app/UnsavedChanges.h`, so a baseline from
before it is compared with `--allow-appended`.

`colors_script.inc` is compiled only for a tree that has `app/panels/ColorSetPanel.h`. The tree it
was introduced in also added the edit color row under the toolbar and the `Sets` page of the
inspector, which show in every state: the row's 22 widgets and the page's widgets as added lines,
the window taller and the canvas smaller, the inspector's tab list with `Sets` (its scroll arrow
enabled, scroll bars of some pages no longer shown), the focus chain with the row's buttons after
`Runtime`, and one more history key (`12/0/sd/00`, the edit colors) at the end of the `keys=` list
of the `--- sprite state` line. No document, status or private-state line differs. To see
`REAL DIFFERENCES: 0` for the states before, dump that tree once with the include of
`colors_script.inc` taken out of `dump.cpp`, make that dump the baseline (see "A baseline of the
working tree") and compare the complete script against it with `--allow-appended`. The color
dialog answers of `inspector_script.inc` handle both the `QColorDialog` of older trees and the
editor's color picker (found by its object name `colorPickerDialog`) and print the same line.

`dump_import_script.inc` is compiled only for a tree that has `app/dialogs/DumpImportDialog.h`;
a baseline of an older revision ends with the states before it and is compared with
`--allow-appended`. The tree it was introduced in also added the comparison tools to the `Mask`
page of the inspector, which show in every state: the 13 controls of `SameFramesPanel` with
their labels and list internals, a `QScrollArea` around the page, and the page's former controls
at other positions inside it (the page itself keeps its place and the inspector its width); the
focus chain has the new controls behind `Edit Mask`. Nothing else differs: with
`--ignore-geometry` such a comparison lists added lines only, and no `---` line (status,
document, canvas grab) differs without it. To see `REAL DIFFERENCES: 0` for the states before,
dump that tree once with the include of `dump_import_script.inc` taken out of `dump.cpp`, make
that dump the baseline (see "A baseline of the working tree") and compare the complete script
against it with `--allow-appended`.

`structure_script.inc` is compiled only for a tree that has `core/edit/StructureCommand.h`; a
baseline of an older revision ends with the states before it and is compared with
`--allow-appended`. The tree it was introduced in also added the `New` and `Delete` buttons of
the browser pages, which show in every state: six `QToolButton` lines and three page `QWidget`
lines more, the three lists 28 pixels lower, and one more history key (`11/0/sd/..`) in the
`keys=` list of the `--- sprite state` line.

`runtime_view_script.inc` is compiled only for a tree that has `app/RuntimeView.h`; a baseline of
an older revision ends with the states before it, and is compared with `--allow-appended`. Its
`identification: ` states use what a tree before them does not have (`exportCurrentFramePng()`
as the runtime export, the `Inactive` filter entry); a baseline of such a tree has to be taken
with the scripts of that tree.

`inspector_script.inc` walks through the first five entries of the frame filter only, the ones an
ordinary project has always had; an entry added later gets states of its own at the end of the
scripts, so that it does not insert a section in the middle. Its dynamic mask states switch to HD
with the `Dynamic` mode on: a tree that switches the mode off in HD (an ordinary project has no
HD dynamic zones to edit) skips the click and the undo there and prints that it did.

Both of these scripts also run against a tree that still showed SD/HD controls for such
projects: they look up the repair button under either name and print, instead of assuming,
whether the SD/HD and Create/Remove buttons are hidden.

The `.inc` files are included at the end of `main()` and each form one block.
`big_original_script.inc` runs after the window was closed (`closed at the end`),
`native_original_script.inc` after that one closed its project, `runtime_view_script.inc`
after that, `structure_script.inc`, `dump_import_script.inc` and `fixes_script.inc` after that, then `colors_script.inc` and `selection_script.inc` last. A line a script prints lands in the section of the dump before it, so a script
appended after others prints nothing before its first dump.

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
  `loadPlain()` in `inspector_script.inc`, `bs` in `big_original_script.inc`, `ns` in
  `native_original_script.inc`) and loaded with
  `ProjectDocument::fromSnapshot`;
- images for the import tests are written to a temporary directory by `inspector_script.inc`
  and removed at the end.

Rules:

- **Append, do not insert.** The window is not reset between states, so every state depends on
  all states before it. A state inserted in the middle changes the ones after it; one added at
  the end of a script block changes nothing else.
- **Extend the script before taking the baseline**, not between `baseline` and `after`. Both
  dumps must come from the same script, otherwise the section lists differ. After editing a
  script, run `baseline` again. The exception is states the baseline cannot run, see
  `--allow-appended`.
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
