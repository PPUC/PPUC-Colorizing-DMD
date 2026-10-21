# Settings

`File -> Settings...` (on macOS `PPUC Colorizing DMD -> Settings...`; `Ctrl/Cmd+,`)
holds the editor's settings, one page per topic. `OK` stores and applies them, `Cancel` leaves
them as they were. They are the editor's, not a project's: kept for the next start.

`Help -> About PPUC Colorizing DMD` (on macOS in the application menu) shows the editor's
version, with the versions of libserum and Qt it was built with. Name the version when you
report a problem.

- `Appearance`: `Theme` is `Follow the system` (the default: light or dark as the operating
  system has it, also when that changes while the editor runs), `Light` or `Dark`, whatever the
  system uses. A forced theme applies at once. Where the platform cannot switch its own style
  (Windows 10's classic style, some Linux desktops, Qt before 6.8), the editor uses Qt's Fusion
  style in the chosen colors; `Follow the system` brings the platform's style back. In every
  theme a button that is on (the drawing tool, `Show Background`, `SD`/`HD`, ...) is filled with the
  system's accent color, so the active one is seen at a glance.
  `Language` is `Follow the system` (the default: the operating system's first language that
  the editor has, English otherwise), `English`, `Deutsch` or `Français`. It changes the menus,
  panels, dialogs and the handbook at the next start. The German and French translations are
  drafts; corrections are welcome (see `translations/README.md` in the sources).
  Under `Canvas`:
  - `Pixel Shape` draws square pixels or round dot pixels, on the canvas, in the sprite editor
    and in PNG exports;
  - `Pixel Size` sets the pixel fill size in 10% steps (`80%` until set otherwise). Values below
    `100%` leave black spacing between pixels, which makes the DMD grid visible.
  They are view and export settings only: no project data and no file changes with them.
  Under `Speaker Light Preview`: whether two speakers show beside the canvas and in the frame
  tester (also `View -> Speaker Light Preview`), their `Shape`, their number of `LEDs` and the channel each shows, see "Speaker
  Lights".
- `DMD Output`: the settings of the DMD output, see "DMD Output".
- `Plugins`: which of the editor's plugins load, see "Plugins".
