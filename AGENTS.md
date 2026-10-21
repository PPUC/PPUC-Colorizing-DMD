# AGENTS.md

Guidance for coding agents working on PPUC Colorizing DMD, the Qt editor for Serum
colorizations. See `readme.md` for what the editor is and how to build it.

## Layout

- `core/`: the project model and everything without a user interface.
  - `document/`: `ProjectDocument`, frames, sprites, backgrounds, palettes, scenes.
  - `edit/`: edit commands and undo histories.
  - `legacy/`: reading and writing `.cROM`, `.cRZ`, `.cRP` and `.cRPX`.
  - `import/`: frame dumps.
  - `runtime/`: libserum, the frame tester and the Runtime view.
  - `render/`: canvas geometry.
- `app/`: the Qt widgets. `MainWindow`, the canvas `FramePreviewWidget`, the sprite editor,
  `panels/` (the sidebar sections), `dialogs/`, and `vpx/` (VPX plugin hosting, the only
  target that sees the plugin SDK).
- `plugin-api/`: the editor API for plugins.
- `tests/`: `colorizing_qt_v2_tests` (core) and `colorizing_qt_v2_widget_tests` (widgets). Each
  file has a `run…Tests()` function that the main of its executable calls.
- `handbook/en/`: the user handbook, one Markdown file per chapter, built into the app
  (`Help -> Handbook`). `handbook/en/README.md` lists the chapters in order.
- `translations/`: Qt Linguist files for German and French.
- `docs/status.md`: the detailed working notes (decisions, state, open work). Read it first
  when resuming work.
- `CHANGELOG.md`: user-facing changes per version.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- The code is C++20.
- CI (`.github/workflows/qt-editor.yml`) builds Windows (MinGW GCC), Linux (GCC) and macOS
  (Apple clang) with warnings as errors.
- Build locally with `CMAKE_COMPILE_WARNING_AS_ERROR=ON` as well; a warning that only scrolls
  by locally fails CI.
- libserum is expected as a checkout beside this repository (`../libserum`). Edit it only in
  that repository, following its own `AGENTS.md`.

## Every change

A change is complete only with all of these, in the same commit:

- **Tests** for the behaviour. Check that a new test fails without the fix.
- **Handbook**: update the chapter in `handbook/en/` for every user-visible change, and list a
  new chapter in `handbook/en/README.md`.
- **Changelog**: add a short, user-facing line to `CHANGELOG.md` under *Next version*. The
  changelog follows the git tags, not the version in `CMakeLists.txt`: *Next version* lists
  what changed since the last tag, and only a tagged version gets a section with its number
  (when the release is tagged, *Next version* becomes that section and a new, empty *Next
  version* starts above it).
- **Translations**: run `cmake --build build --target update_translations` and translate every
  new or changed string into German and French. An entry lupdate pre-filled is still marked
  `type="unfinished"`; it counts as done only once that mark is removed. No
  `type="unfinished"` may remain.
- **`docs/status.md`**: a short technical note (what changed, where, why, which test).

## Conventions

- Match the surrounding code: naming, comment style (comments say what and why, in full
  sentences), Qt idioms. Keep lines within 100 columns.
- User-interface text goes through `tr()`; use full words, no abbreviations such as "BG".
- libserum is the source of truth for rendering and identification: the editor shows what
  libserum does.
- The Win32 editor 3.17.3 is the compatibility target for opening projects. Its sources are at
  the tag `v3.17.3` (`git show v3.17.3:<file>`); they are not on the branch.
- New editor-only project data goes into a new chunk of the `.cRPX`
  (`core/legacy/CrpxFile.h` documents the layout). Never change the cROM layout libserum
  reads.
- Keep repository text (code, commits, docs) technical.

## Commits and versions

- Commit in small, self-contained steps with a message that says what changed and why. Add no
  AI co-author trailers.
- The version is `project(... VERSION 3.99.x)` in `CMakeLists.txt`; the About dialog's test
  comment in `app/dialogs/AboutDialog.h` names it too.
- Raise the patch version before every push, in a commit of its own ("Bump the version to
  3.99.x"). This does not touch the changelog. 4.0.0 will be the first release.
