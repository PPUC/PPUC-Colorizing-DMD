# PPUC Colorizing DMD – Changelog

Changes since the last tagged version. A section takes a version number when that version is
tagged; until then the changes are listed under *Next version*.

## Next version

Changes since `v3.17.3`, the last version of the Win32 ColorizingDMD editor.

**A new editor**
- **PPUC Colorizing DMD** replaces the Win32 editor: a Qt application for Windows, macOS
  (Apple silicon) and Linux, with an application icon, an About dialog and a built-in handbook
  (`F1`).
- It opens the projects of the Win32 editor 3.17.3. Names with umlauts (e.g. "Grün") are read
  as the Win32 editor wrote them.
- Projects are saved as `.cRZ`, compressed while written, with the editor's data in a `.cRPX`
  beside it; no `.cROM` or temporary file is written. A saved project cannot be opened by the
  Win32 editor again.
- Large projects need less memory: identical data is shared and idle data compressed.
- Plugins can change the project in one undoable step, including resetting frame pixels to a
  fill color.

**Workspace**
- The sidebars show only the sections that fit the canvas shown: the frame canvas, background
  editing or the sprite editor. Frame Background and Frame Sprites (what ties a record to the
  frames) are separate from Background and Sprite (the record itself), each with *Back to
  Frame*.
- `<` / `>` in the toolbar step back and forward through what the canvas showed: frames,
  backgrounds, sprites and the timeline filter.
- Open sidebar sections can be resized by dragging the border between them; every section has
  the same layout. Project Settings is a dialog, and the right sidebar uses the full height.
- The left sidebar's thumbnails have their captions below them; sprite and background
  thumbnails show the SD picture with an HD badge.
- Space + drag pans the canvas and the sprite editor; clicking the zoom value goes back to 100%.
- The project clock stops after a minute without input.

**Timeline**
- Frames can be tagged (e.g. "Settings menu", "Multiball", "Wizard Mode") in the Frame
  section's *Tags* field; a frame can have several tags. The timeline filter lists every tag,
  and the thumbnails show them as `#Multiball`.
- Frames can be reordered by dragging them in the strip.
- A double click on a frame selects that frame alone (#18) and goes back to editing the frame
  from the sprite editor or a background.
- Frame dumps can be loaded compressed as `.gz` or `.zip`. A dump whose last frame is damaged
  is imported without that frame.

**Scenes**
- Scenes are kept in the project (no `pup.csv` / `scaling.txt` needed with libserum 2.8.2) and
  are added, changed and removed together with their frames; IDs 50000–61999.
- The Scenes list in the left sidebar shows each scene's first frame. A double click lists the
  scene's frames and shows the first one; *Leave Scene* returns to the timeline before in one
  step.
- The Scene section in the right sidebar edits the settings of the scene shown. Comparison
  mask, sprite and dynamic sections are hidden on scene frames.
- Deleting frames of a scene shrinks the scene and renumbers the frames after them, markers
  included, so FRAME NUMBER has no gaps.
- A scene dragged onto the canvas becomes the trigger of the selected frames, as a background
  dragged there is assigned to them.
- A frame's trigger is set by kind: PUP trigger, scene, or monochrome fallback.
- Scaling settings ("Upscaled shadow offset") are part of the project's settings.

**Sprites**
- *Outline Sprites* draws a green outline around the sprites found on the frame.
- *Show Sprites* hides found sprites on the canvas, the DMD output and the GIF export.
- The sprite editor zooms and scrolls, with the colorized and the original sprite in two
  halves.
- Sprites can be colored with color set replace.
- The frame's sprite list scrolls instead of stretching the section.

**Backgrounds**
- *Edit Background* opens the current frame's own background.
- HD backgrounds can be edited on frames without HD.
- Create HD for sprites and backgrounds asks for the scaling filter; Create and Remove HD sit
  in the Sprite and Background sections. Create/Remove HD works on all selected frames.

**Images**
- The Images window of the Win32 editor: a zone with the selection's proportions, resized
  with the wheel, start and end zones (scroll copy), brightness, contrast and blur, Nearest or
  Smooth scaling, into frames or a background, SD or HD. Named zones are saved in the project.

**Colors and drawing**
- The color picker can take colors from any palette of the project.
- Dynamic sets have one color per original color, and palette colors can be dropped onto them.
- Shapes draw from their centre with Alt.
- The selection works as in the Win32 editor: across frames (visible on each frame, #17) and
  in the Dynamic and Background modes.

**Frame tester and output**
- The frame tester steps one frame back or forward, names the scene playing, can test without
  scenes and plays the project's scenes, as the Runtime view does. Its SD and HD pictures grow
  with the window.
- DMDs are blanked when the DMD output stops or the editor closes.
