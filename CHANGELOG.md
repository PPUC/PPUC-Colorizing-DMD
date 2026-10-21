# PPUC Colorizing DMD – Changelog

The changelog follows the git tags. A section takes a version number when that version is
tagged; the changes since the last tag are listed under *Next version*.

## Next version

- A project is made for libserum 2.8 or libserum 3.0 (*Project Settings*, *Made for*).
  libserum 3.0 allows 128 sprites per frame and practically any number of sprites and
  backgrounds; such a project plays with libserum 3.0 and later only. Projects for libserum
  2.8 are saved as before.
- The Frame Sprites section warns when a frame uses more than 32 sprites, which can make the
  sprite detection noticeably slow.
- Speaker lights: named light effects (the 80 WS2812FX effects with colors, speed, options,
  brightness and duration) in the new Light Effects section; per frame and channel an effect, a
  static color or off in the Speaker Lights section, with ambient lighting colors suggested from the
  frame; a default per channel in Project Settings. A preview beside the canvas and in the frame
  tester plays them with the firmware's effect code (Settings -> Appearance). Needs libserum 3.0
  at runtime.
- *View -> Speaker Light Preview* switches the speakers beside the canvas on and off; they sit
  level with the colorized frame and are sized by dragging their border.
- The frame tester draws the pixel shape and size of the canvas and shows its three DMDs one above
  the other: the 32-row plane, the ROM frame, the 64-row plane.
- The Speaker Lights and Light Effect sections are the last ones of the right sidebar.
- *Ambient Lighting For Frames Without Speaker Lights* gives every frame without speaker light settings
  the ambient lighting colors of its left and right part, in one undo step, for existing projects.

## 3.99.6

- Frames can be tagged (e.g. "Settings menu", "Multiball", "Wizard Mode") in the Frame
  section's *Tags* field; a frame can have several tags. The timeline filter lists every tag,
  and the thumbnails show them as `#Multiball`.
- A scene dragged onto the canvas becomes the trigger of the selected frames, as a background
  dragged there is assigned to them.
- The frame tester's SD and HD pictures grow with the window.
- A double click on a frame in the timeline selects that frame alone (#18).
- A canvas selection stays visible on another frame instead of reappearing with the next
  Ctrl+click (#17).

## 3.99.5

- *Outline Sprites* draws a green outline around the sprites found on the frame.
- *Show Sprites* hides the found sprites on the canvas, the DMD output and the GIF export.
- The color picker can take colors from any palette of the project.
- "BG" is written out as "Background" everywhere; the Frame section's *Target* field is gone
  (*Edit Background* does that).
- The frame's sprite list scrolls instead of stretching the section.
- The separate 100% zoom button is gone again: clicking the zoom value resets it.
- Fixes:
  - Names from Win32 projects with umlauts (e.g. "Grün") display correctly.
  - A dump with a damaged last frame is imported without that frame.
  - Holding Space to pan keeps the hand cursor over the pixels.
- The editor is built as C++20.

## 3.99.4

**Workspace**
- The sidebars show only the sections that fit the canvas shown: the frame canvas, background
  editing or the sprite editor. Frame Background and Frame Sprites (what ties a record to the
  frames) are separate from Background and Sprite (the record itself), each with *Back to
  Frame*.
- `<` / `>` in the toolbar step back and forward through what the canvas showed: frames,
  backgrounds, sprites and the timeline filter.
- The left sidebar's thumbnails have their captions below them; sprite and background
  thumbnails show the SD picture with an HD badge.
- Helper Lines buttons match the other sections.
- A *100%* button in the toolbar resets the zoom.

**Scenes**
- The Scenes list is in the left sidebar, with the first frame of each scene as its
  thumbnail. A double click lists the scene's frames and shows the first one; *Leave Scene*
  returns to the timeline before in one step.
- The Scene section in the right sidebar edits the settings of the scene shown. Comparison
  mask, sprite and dynamic sections are hidden on scene frames.
- Deleting frames of a scene shrinks the scene and renumbers the frames after them, markers
  included, so FRAME NUMBER has no gaps.
- The frame filter no longer has one entry per scene.

**Sprites and backgrounds**
- The sprite editor shows the colorized and the original sprite in two halves, with scroll
  bars and its zoom; Space + drag pans it.
- *Edit BG* opens the current frame's own background; HD backgrounds can be edited on frames
  without HD.
- Create HD for sprites and backgrounds asks for the scaling filter; Create and Remove HD sit
  in the Sprite and Background sections.
- A double click on a frame goes back to editing the frame from the sprite editor or a
  background.

**Images**
- The Images window of the Win32 editor: a zone with the selection's proportions, resized
  with the wheel, start and end zones (scroll copy), brightness, contrast and blur, Nearest or
  Smooth scaling, into frames or a background, SD or HD. Named zones are saved in the project.

## 3.99.3

- Projects are saved as `.cRZ` only, compressed while written; no `.cROM` or temporary file.
- Large projects need less memory: identical data is shared and idle data compressed.
- Project Settings is a dialog, and the right sidebar uses the full height.
- Plugins can change the project in one undoable step, including resetting frame pixels to a
  fill color.

## 3.99.2

Changes since `v3.17.3`, the last version of the Win32 ColorizingDMD editor.

**A new editor**
- **PPUC Colorizing DMD** replaces the Win32 editor: a Qt application for Windows, macOS
  (Apple silicon) and Linux, with an application icon, an About dialog and a built-in handbook
  (`F1`).
- It opens the projects of the Win32 editor 3.17.3; the editor's own data is kept in a `.cRPX`
  beside the project, which the Win32 editor cannot open again.

**Scenes**
- Scenes are kept in the project (no `pup.csv` / `scaling.txt` needed with libserum 2.8.2) and
  are added, changed and removed together with their frames; IDs 50000–61999.
- A frame's trigger is set by kind: PUP trigger, scene, or monochrome fallback.
- The frame tester and the Runtime view play the project's scenes. The frame tester steps one
  frame back or forward, names the scene playing and can test without scenes.
- Scaling settings ("Upscaled shadow offset") are part of the project's settings.

**Editing**
- Open sidebar sections can be resized by dragging the border between them; every section has
  the same layout.
- Frames can be reordered by dragging them in the strip.
- Frame dumps can be loaded compressed as `.gz` or `.zip`.
- Create/Remove HD works on all selected frames.
- Sprites can be colored with color set replace; the sprite editor zooms.
- Dynamic sets have one color per original color, and palette colors can be dropped onto them.
- Shapes draw from their centre with Alt.
- The selection works as in the Win32 editor: across frames, and in the Dynamic and Background
  modes.
- The project clock stops after a minute without input.
- DMDs are blanked when the DMD output stops or the editor closes.
