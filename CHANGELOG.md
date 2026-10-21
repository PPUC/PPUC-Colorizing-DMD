# PPUC Colorizing DMD – Changelog

## 3.99.5

**Frame tags**
- Frames can be tagged (e.g. "Settings menu", "Multiball", "Wizard Mode") in the Frame
  section's new *Tags* field; a frame can have several tags.
- The timeline filter lists every tag, and the thumbnails show them as `#Multiball`.

**Workspace**
- Sidebar sections fit the canvas you're on:
  - Frame canvas, background editing and sprite editor each show only their own sections.
  - Frame Background and Frame Sprites (links to the frame) are separate from Background and
    Sprite (the record itself).
  - Each record section has *Back to Frame*.
- New `<` / `>` buttons step back and forward through what the canvas showed: frames,
  backgrounds, sprites and the timeline filter.
- "BG" is now written out as "Background" everywhere.
- The left sidebar shows captions below its thumbnails, and sprite and background thumbnails
  stay on SD with an HD badge.
- Helper Lines buttons now match the other sections.
- The frame's sprite list now scrolls instead of stretching the section.

**Scenes**
- Scenes moved to the left sidebar and now have thumbnails.
- A scene dragged onto the canvas becomes the trigger of the selected frames, as a background
  dragged there is assigned to them.
- Double-clicking a scene lists its frames and shows its first frame. *Leave Scene* returns to
  your previous timeline in one step.
- A new Scene section in the right sidebar edits the settings of the scene shown. Comparison
  mask, sprite and dynamic sections are hidden on scene frames.
- Deleting scene frames now shrinks the scene and renumbers the frames after them, markers
  included, so FRAME NUMBER has no gaps.
- The frame filter no longer has one entry per scene.

**Sprites**
- *Outline Sprites* draws a green outline around the sprites found on the frame.
- *Show Sprites* hides found sprites on the canvas, the DMD output and the GIF export.
- The sprite editor is split into colorized and original halves, with scroll bars.
- Double-clicking a frame leaves the sprite editor.

**Backgrounds**
- *Edit Background* opens the current frame's own background.
- HD backgrounds can be edited on frames without HD.
- Create HD for sprites and backgrounds now asks for the scaling filter.
- The redundant *Target* field is gone.

**Colors**
- The color picker can take colors from any palette of the project.

**Frame tester**
- The SD and HD pictures grow with the window.

**Fixes**
- A canvas selection stays visible on another frame instead of reappearing on the next Ctrl+click
  (#17).
- A double click on a frame in the timeline selects that frame alone (#18).
- Names from Win32 projects with umlauts (e.g. "Grün") display correctly.
- A dump with a damaged last frame now imports, dropping that frame.
- Holding Space to pan keeps the hand cursor over the pixels.
- A separate 100% zoom button came and went; clicking the zoom value resets it.

## 3.99.4

- Images window ported from the Win32 editor:
  - zone with the selection's proportions, wheel resize, start and end zones (scroll copy)
  - brightness, contrast, blur
  - Nearest/Smooth scaling
  - frames or background, SD/HD
- Named zones are saved in the project.
- The Linear/Cubic/Lanczos scaling options are gone.
- Sprite canvas shows its zoom, and Space + drag pans the canvas.
- Double-clicking a frame while editing a background goes back to frame editing.
- Create/Remove HD buttons sit in the Sprite and Background sections.

## 3.99.3

- Projects are saved as `.cRZ` only, compressed while written; no `.cROM` or temporary file.
- Lower memory use for large projects: identical data is shared and idle data compressed.
- Project Settings moved into a dialog, and the right sidebar now uses the full height.
- Plugins can change the project in one undoable step, including resetting frame pixels to a
  fill color.

## 3.99.2

- Frame tester:
  - steps one frame back or forward
  - names the scene playing
  - can test without scenes
  - shows the picture after a scene step
- Frame dumps can be loaded compressed as `.gz` or `.zip`.
- The project clock stops after a minute without input.
- Sprites can be colored with color set replace.
- Create/Remove HD works on all selected frames.
- Scaling settings got a Project Settings section; the setting is now called "Upscaled shadow
  offset".
- Sidebar sections share one layout.
- macOS builds are named *PPUC Colorizing DMD.app*.

## 3.99.1

- The editor is named **PPUC Colorizing DMD**, with an application icon and an About dialog.
- Scenes:
  - kept in the project (no `pup.csv` / `scaling.txt` needed with libserum 2.8.2)
  - added, changed and removed together with their frames
  - IDs 50000–61999
  - played in the frame tester and the Runtime view
- A frame's trigger is set by kind: PUP trigger, scene, or monochrome fallback.
- Shapes draw from their centre with Alt.
- Open sidebar sections can be resized by dragging the border between them.
- Frames can be reordered by dragging in the strip.
- DMDs are blanked when DMD output stops or the editor closes.
- The sprite editor zooms.
- Dynamic sets have one color per original color, and palette colors can be dropped onto them.
- Selection works as in the Win32 editor: across frames, and in the Dynamic and Background
  modes.
