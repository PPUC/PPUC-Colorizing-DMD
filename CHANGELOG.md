# PPUC Colorizing DMD – Changelog

The changelog follows the git tags. A section takes a version number when that version is
tagged; the changes since the last tag are listed under *Next version*.

## Next version

## 3.99.14

- Projects are made for libserum 2.9 and later only: `Project Settings` no longer chooses a
  libserum. Every project allows 128 sprites per frame and 65535 sprites and backgrounds;
  projects of libserum 2.8 and of the Win32 editor are opened as such projects.
- `File -> Export For Earlier Editors` is gone: the editor writes no `.cRZ` and `.cRPX` any more.
  A machine gets the `.cROMc` (`File -> Export .cROMc`) and needs libserum 2.9 or later.
- Several sprites can be selected in the sprite list: `Assign` puts each on the selected frames
  (first empty slots, as in the Win32 editor), `Delete` deletes them together (issue #38).
- `Used by the current frame` also lists the scene a frame starts by its trigger, not only the
  scene a frame is part of.
- The comparison masks, backgrounds, sprites and scenes on the left show their total beside
  `New` and `Delete`, and how many are listed while `Used by the current frame` is on (issue #37).
- The frame strip always ends on a whole frame, so the first frame shown is shown in full: the
  mouse wheel moves it by one frame, other scrolling settles on the next one (issue #34).
- The box of a paste is framed by a fine line between the pixels that covers none of them, at
  any zoom (issue #30).
- `Shadow Next To Selection` and `Grow Selection` have `All Around, Stern Style`: the four
  sides without the corners; the shadows all around have `Alt+S` (Stern style) and `Alt+Shift+S`
  (issue #35).

## 3.99.13

- Another frame chosen alone drops the canvas selection (`Undo Selection` brings it back); it
  stays when stepping with `Left`/`Right` and while several frames are selected (issue #31).
  `Select None` shows `Ctrl+D` in the menu.

## 3.99.12

- Teams: a frame (or the frame order, the settings) whose change you have merged is no longer
  locked for you until the partner merges your next shared changes.
- The time spent on a project is kept per author too (the clock's tool tip); in a team project
  shared changes carry it, so the project time is the time of all authors.
- `Pick` picks once and gives the tool chosen before it back (issue #29).
- Teams: frames a partner works on show the partner's GitHub picture (or a lock) in the frame
  list.

## 3.99.11

- A team project has a `Team` section in the right side bar: the partners' shared changes
  waiting to be merged, with their GitHub pictures and comments, a bell and their number on its
  header, and the buttons to merge and to share. Sharing asks for a short comment.
- Shared changes over 20 MB can go as a file another way (a cloud drive): GitHub gets only a
  notice, and the partners add the file with `Add File...` in the `Team` section.
- With a selection, one drag of the `Grad` tool fills it with a gradient on every selected
  frame (linear along the drag, or a circle or ellipse around its start), as the Win32 editor's
  gradient draw did; `Ctrl+drag` across the palette colors picks `From` and `To`.

## 3.99.10

- `Edit -> Invert Selection` inverts the comparison mask in the Mask mode, as in the Win32
  editor (issue #27).
- `New Project From Dump...` and `Add Frames From Dump...` list zip and gzip dumps in their file
  dialog too (issue #26).
- With several frames selected, the frame status line says how long they last together
  (issue #28).
- Team projects (`Team` menu): sign in to GitHub, start a team project with a repository for the
  partners, share your changes and merge theirs per frame, with a choice for every frame both
  changed; what you edit is claimed at once and kept in step in the background, frames a
  partner works on stay read-only, shared changes are downloaded before you merge them, frames
  can be reserved, and anyone can break the lock and go on alone.
- `Edit -> Copy Frames` and `Paste Frames` carry frames with what they use from one editor window
  to another, as a package does.
- `File -> Export Selected Frames As Package...` writes the selected frames with the masks,
  backgrounds, sprites and light effects they use as a package (`.cdmdpkg`); `File -> Import
  Package...` adds them to another project, using what it has already, and frames it has already
  (the same ROM frame under the same mask) can be updated, skipped or added again.
- A project is now one file, its project file (`.cdmd`): `Save` writes it, with every frame,
  mask, background and sprite in a part of its own that keeps its ID and notes who changed it
  last and when (`Settings -> Projects -> Your name`). Projects of the Win32 editor and of
  earlier versions (`.cROM`, `.cRZ` with `.cRP` or `.cRPX`) are opened and converted; their files
  are no longer written or renamed. `File -> Export For Earlier Editors...` writes a `.cRZ` and
  `.cRPX` for libserum 2.8 and 2.9 and earlier editor versions. The autosave is
  `<name>(auto).cdmd`.
- `File -> Project Settings...` has a license for the colorization (all rights reserved, the
  Creative Commons licenses or your own text), with an attribution and a link. It is saved in
  the `.cRPX`.
- The frame tester and the cROMc export hand the project to libserum without writing a cROM
  first: the frame tester opens large projects faster (GoT: libserum's part 3.7 s instead of
  6.7 s).
- The `Color Rotations` section says how many of the frame's pixels the selected slot moves, so
  that a frame whose colors are not the slot's (a palette changed after drawing) shows at once.
- `Edit -> Undo Selection` (`Alt+Z`) and `Redo Selection` (`Alt+Shift+Z`) bring back an earlier
  selection; the selection has its own history of the last 50 changes, apart from the
  project's Undo.

## 3.99.9

- One action is one undo step: an edit of several frames (drawing on selected frames, paste,
  `Auto Copy...`, `Scrolling Auto Fill...`, image and movie clip import, the recolor helpers,
  frame settings and the like) is undone and redone on all of them at once.
- `Edit -> Stay In Paste Mode`: a paste can be stamped several times, until `Esc` ends paste
  mode.
- `Common To New Mask` in the `Comparison Mask` section adds a mask that covers the pixels in
  which the selected frames differ and assigns it to them, in one undoable step.
- The search area of a sprite slot can be drawn: with `Show Bounds` on, a rectangle drawn on the
  original frame sets the slot's `Min X` to `Max Y`.
- Dynamic sets can color the SD picture only (`Only on the SD Picture` on the `Dynamic Sets`
  section, libserum 2.9): on a frame with its own HD frame, such as an imported image or video,
  their zones show that HD frame instead of the upscaled set, while other sets such as a score
  stay on it.
- In a 256x64 project, `Create 256x64 Frame` asks before it gives a background without a
  256x64 image a black one, and can create the frames only.
- `Spot Common` can mark the pixels the selected frames have in common, as the Win32 editor
  does, instead of the differing ones (`Spot Common marks` in the `Comparison Mask` section).
- The frame status line shows how many frames are the same as the current one (`| N same`),
  also while the `Comparison Mask` section is not shown.
- Saving a 192x64 project whose files have 96x32 pictures in use asks first: the editor does
  not keep that plane, and the question says how many frames, backgrounds and sprites lose them.
- While the `Comparison Mask` section is shown, the frames that are the same as the current one
  are outlined in green in the frame strip and ticked on its scroll bar, and
  `Ctrl/Cmd+Left`/`Right` scroll the strip through them while the current frame stays, as in the
  Win32 editor (issue #24).

## 3.99.8

- `Esc` reduces a selection of several frames to the frame shown, in the strip and on the
  canvas (after cancelling a paste or a pending click). It joins the ways that were there
  already: a double click on a frame selects it alone, `Ctrl/Cmd`+click takes a frame out of
  the selection, `Left`/`Right` select the next frame alone, and a click on a frame outside the
  selection selects that one.
- The section list drops down as wide as its longest name. The trigger list below it is gone:
  the frame filter's *With triggers* lists the frames with a trigger.
- *Using the selected asset* with a scene selected lists the frames that start the scene (also
  as a background scene) instead of the scene's own frames; a double click on a scene lists
  those under a filter entry of their own, *Frames of scene ...*. With a light effect selected
  it lists the frames that start the effect.
- The editor opens where and as large as it was left, maximized or not; so do the Handbook,
  the Frame Tester, the PuP Tester, the plugin log, the Images window and the dump import (#2).
- `Left` and `Right` step through the frames also after a click on the canvas (#20).
- The frame filter has *With triggers*: frames with a PUP trigger, a scene or the monochrome
  fallback (#21).
- A new or opened project starts with the frame filter on *All frames* (#22).
- Long drop-down lists such as the sections scroll instead of running past the screen, so their
  last entries can be chosen (#23).
- The sections of both sidebars can be arranged: drag a header to move its section, or use the
  header's context menu, which also hides optional sections (speaker lights, helper lines,
  scenes, color rotations, color sets) and shows them again. The arrangement is kept.

## 3.99.7

- A project is made for libserum 2.8 or libserum 2.9 (*Project Settings*, *Made for*).
  libserum 2.9 allows 128 sprites per frame and practically any number of sprites and
  backgrounds; such a project plays with libserum 2.9 and later only. Projects for libserum
  2.8 are saved as before.
- More than 32 sprites on a frame stay the exception: the editor asks before a frame gets its 33rd
  sprite and warns while a frame has more, since every sprite more costs runtime (fine for almost
  static System 3-6 colorizations, to be avoided in a video mode). Sprites themselves work as
  before.
- Speaker lights: named light effects (the 80 WS2812FX effects with colors, speed, options,
  brightness and duration) in the new Light Effects section; per frame and channel an effect, a
  static color or off in the Speaker Lights section, with ambient lighting colors suggested from
  the frame; a default per channel in Project Settings. A preview beside the canvas and in the frame
  tester plays them with the firmware's effect code (Settings -> Appearance). Needs libserum 2.9
  at runtime.
- *View -> Speaker Light Preview* switches the speakers beside the canvas on and off; they sit
  level with the colorized frame and are sized by dragging their border.
- The frame tester draws the pixel shape and size of the canvas and shows its three DMDs one above
  the other, at the same size: the 32-row plane, the ROM frame, the 64-row plane.
- The Speaker Lights and Light Effect sections are the last ones of the right sidebar.
- Text in front of a background scene no longer flickers in the 64-row plane while the scene
  plays (libserum 2.9; the frame tester and the Runtime view show it as a machine does).
- Ambient lighting colors are marked *automatic* in the Speaker Lights section (*out of date*
  once the frame changed), the timeline filter lists them (*Ambient lighting*, *Ambient lighting
  out of date*), and *Edit -> Update Ambient Lighting* updates them all, leaving the colors the
  author chose.
- *Ambient Lighting For Frames Without Speaker Lights* gives every frame without speaker light
  settings the ambient lighting colors of its left and right part, in one undo step, for existing
  projects.

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
