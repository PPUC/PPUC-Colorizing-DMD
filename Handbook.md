# ColorizingDMD Qt Rewrite v2 Handbook

This handbook describes the implemented behavior of `qt_rewrite_v2`.
Update it in the same change set whenever a user-visible feature changes.

## Project Files

The Qt rewrite edits legacy Serum editor projects directly:

- `.cROM` stores colorized frame data and runtime project data.
- `.cRP` stores original/reference frames and editor-side metadata such as sprite names and frame durations.
- `.cRZ` is a zip archive holding the `.cROM`, in one entry named `<ROM name>.cRom`. It is the
  form in which a colorization is usually handed around, together with the `.cRP`. The editor
  opens it directly and writes it on every save.
- `.cROMc` is not edited directly.

Saving writes complete `.cROM` and `.cRP` files from the in-memory project in the legacy file
layout, plus the `.cRZ` archive of that `.cROM`. Opening is verified with cROM header revision 19 and 20 projects. Revision 20 projects are
saved as revision 20, everything older as revision 19, so a save never moves a project to a
revision an older libserum cannot read.

A project file stores two planes of colorized data, an "original" and an "extra" one. Which sizes
they have, and which of them has the size of the ROM's own frame, depends on the project. The
editor opens three layouts and refuses every other combination of sizes with a message that names
the sizes it found:

| ROM frame | Original plane | Extra plane | Shown and edited |
| --- | --- | --- | --- |
| 128x32 (ordinary project) | 128x32, the editor's SD | 256x64, the editor's HD | both |
| 256x64 (revision 20 flag "256x64 original") | 128x32, kept unchanged | 256x64 | the 256x64 plane only |
| 192x64 | 192x64 | 96x32, discarded | the 192x64 plane only |

A plane that is smaller than the ROM frame is never shown or edited. The 128x32 data of a 256x64
project is loaded and written back as it is. The 96x32 data of a 192x64 project is discarded when
the project is opened and written as "no extra data" on save. See "Projects With 256x64 Original
Frames" and "Projects With 192x64 Original Frames".

"SD" and "HD" are names the editor uses only in ordinary projects, where HD is an extra, upscaled
resolution next to SD. A project of one of the other two layouts has one plane, and the editor
shows no SD or HD wording, badge, counter, filter or button for it; where the plane needs a name
it is called by its size, `256x64` or `192x64`. Everywhere else this handbook describes ordinary
projects, where SD means 128x32 and HD means 256x64.

The in-memory project model currently carries structured comparison-mask data from `.cROM`:

- Per-frame shape comparison mode.
- Per-frame comparison mask ID.
- Comparison mask bitmaps, in the size of the original frames (128x32, 256x64 or 192x64, see the
  table above).

It also carries structured dynamic-mask data from `.cROM`:

- Per-frame SD and HD dynamic mask bitmaps.
- Per-frame SD and HD dynamic color sets.
- Per-frame SD and HD color rotation slots.

It also carries structured background data from `.cROM`:

- Shared SD and HD background images.
- Per-frame background assignments.
- Per-frame SD and HD background masks.

## Opening Projects

Use `File -> Open Project...` and select a `.cROM` or a `.cRZ` file.

The app looks for a matching `.cRP` next to it with the same base name. If it cannot find that file, it asks for the `.cRP` path.

Opening a `.cRZ` reads the colorization from the archive; nothing is unpacked to disk. The
archive's first file entry is taken, whatever it is named. If a `.cROM` with the same base name
lies next to the `.cRZ`, the file that was selected is the one that is loaded; the other is not
looked at.

A project opened from `<name>.cRZ` is from then on the project `<name>.cROM` in the same
folder: `Save` creates that `.cROM` next to the archive (replacing one that is already there) and
rewrites the archive, and `Save As...` proposes `<name>_copy.cROM`.

After a successful open, the project is added to `File -> Open Recent`.

Opening a project always starts in the frame view: `Edit Sprite` and `Select Capture` are switched
off, a pending sprite capture selection is dropped, and the mask browser selection starts again at
the first frame's mask.
Recent-project entries show the full path of the file that was opened (`.cROM` or `.cRZ`) and
internally remember that path and the `.cRP` path. If that file no longer exists when the entry
is used, the file with the same base name and the other of the two extensions is opened instead,
so an entry keeps working whether only the archive or only the loose `.cROM` is left.

## Saving

Use `File -> Save` or the standard Save shortcut.

Every save writes three files with the same base name into the project folder: `<name>.cROM`,
`<name>.cRP` and `<name>.cRZ`. The archive holds exactly the bytes of the `.cROM` just written,
deflate-compressed, in one entry named `<ROM name>.cRom` (the ROM name stored in the project,
which need not be the file name), as the Win32 editor writes it. An archive that already exists
under another spelling of the extension (`.crz`) is the one that is replaced.

Use `File -> Save As...` to save the project under a new name. The app asks for a target `.cROM`
path and writes the `.cROM`, a `.cRP` and a `.cRZ` with the same base name there.

Use `File -> Export Current Frame PNG...` to export the current frame as a PNG. The picture is
libserum's output for the frame, the one a machine shows, not the editor's own rendering; see
"Runtime View" for how it is made. Which plane is exported:

- with the Runtime view on, the plane it shows;
- otherwise the plane of the editing mode: the 32-row plane in SD mode and the 64-row plane in HD
  mode. The 64-row plane exists for every frame; for a frame without HD it is libserum's upscale
  of the 128x32 picture;
- the one plane of a project with 256x64 or 192x64 original frames.

The proposed file name ends in `_sd` or `_hd`, or in `_256x64` or `_192x64`. `DMD Shape` and
`DMD Size` apply. The assigned background is always part of the picture: `Show BG` is a setting
of the editing canvas and has no effect on the export.

If libserum has no picture for the frame, nothing is written and no file dialog comes up. The
status bar says why: `Frame N is not exported:` followed by the reason the Runtime view gives for
the frame (the frame is inactive and skipped, it lacks its 256x64 data, or libserum's own error
text). The editor's own rendering is never written in its place: a file would then show what no
machine shows. There is no separate export of the editor's rendering any more. It served no
purpose the runtime export does not: the frames libserum renders nothing for are frames with
nothing colorized to show (an inactive frame is one that is not colorized, a 256x64 frame without
its data is black), and the image of a background is exported by `Export BG`.

Use `File -> Import Image Into Selected Frames...` or the Frame topic's `Import Image` button to
replace selected frame pixels from a PNG/BMP/JPEG. The image is scaled to the active SD/HD DMD size.
In HD mode, selected frames without HD data are skipped. The import is undoable per frame.

Save behavior:

- All three files are written completely next to their targets first (as `<file>.saving`) and
  then swapped in, so a failed or interrupted save does not leave a truncated project behind. If
  any of them cannot be written, including the archive, the save fails as a whole, the temporary
  files are removed and the existing files stay as they were.
- Writing the archive compresses the whole `.cROM` and makes saving a large project noticeably
  slower (about 5 extra seconds for a 450 MB `.cROM`).
- The `.cRZ` is a plain zip without zip64 as long as the `.cROM` is smaller than 4 GiB. It is not
  byte-identical to the Win32 editor's archive of the same project (other deflate settings, no
  timestamp extra field); the `.cROM` inside is.
- A ROM name containing `/`, `\` or `:` cannot be used as an archive entry name; saving such a
  project fails with a message.
- An unedited project saves back byte-identical to what the Win32 editor wrote. This is checked
  against real projects with the `colorizing_qt_v2_roundtrip` tool (see Testing Checklist). The
  one exception is the 96x32 data of a project with 192x64 original frames, which is discarded:
  the blocks of the extra plane are written as "no extra data" and differ from the file if the
  file held anything else there. All other blocks and the `.cRP` are identical.
- Frame identification hashes are recalculated from the original frame, its comparison mask, and
  its shape mode.
- The "active frame" flag is recalculated: a frame shorter than 15 ms stays inactive until it is
  colorized or gets dynamic content. The frame list marks the frames the runtime skips for that
  reason, see "Frame List".
- Colorized pixels covered by a dynamic mask are written as 0, as the Win32 editor does. The
  in-memory frame is not changed, so those pixels show their old color until the project is
  reopened. On the HD plane of an ordinary project this is the stored HD dynamic mask, which the
  runtime does not use and the editor does not show (see "Dynamic Masks"): an HD pixel painted
  under it is still written as 0, and is black on a machine wherever no SD dynamic zone covers
  it. The Runtime view shows that; the editing canvas shows the painted color.
- Editor metadata the app does not expose yet (color sets, mask names, sections, sprite source
  frames, image crop presets, imported palettes, project time, PuP pack) is carried through.
- HD pixels that the Win32 editor left in a frame or background whose HD is switched off are
  carried through until HD is created or removed for it here.
- Marks the document clean after successful save.

Files written by earlier Qt editor builds are not supported; only projects saved by the Win32
editor (currently 3.17.3) or by this serializer are.

## Playback

The top toolbar contains editor-only playback controls:

- `Play` advances through the currently visible frame list.
- `Pause` stops playback.
- `Loop` restarts at the first visible frame after the last visible frame.
- `cRP` uses the per-frame durations loaded from the `.cRP` file.
- The interval field controls the fallback frame advance interval in milliseconds.

Playback respects the current frame filter in the left `Frames` tab. When `cRP` timing is enabled,
frames with missing, zero, or unusually large durations use the fallback interval.

During playback, the central preview advances color-rotation slots for the displayed frame using
each slot's stored delay. This is an editor preview; libserum remains the runtime source of truth for
final composition order.

With the Runtime view on, playback is done by libserum instead: the frames are fed to it in the
order of the list, with the same timings, libserum rotates the colors, and it identifies every
frame as it would on a machine, which depends on the frames before it. See "Runtime View".

Rotation timing is not a visual preference. The per-slot delay stored in the project controls both
runtime behavior and editor playback, so future rotation-editing UI must keep timing edits explicit
and must be validated against libserum playback behavior.

## Frame List

The left frame list shows one thumbnail per loaded frame plus compact metadata.

Thumbnails are drawn when their rows come into view, not when the project is opened: a row that
was never scrolled to shows an empty dark box of the thumbnail's size, which is replaced by the
picture as soon as the row is in view (rows just above and below the visible part are prepared as
well). Rows have the same height with and without a picture, so scrolling, selecting, filtering
and keyboard navigation do not depend on it. A thumbnail that was drawn is kept and redrawn when
its frame changes; nothing has to be done to refresh it. Opening a large project therefore no
longer waits for one thumbnail per frame.

Frame items show the project frame number, the loaded cRP duration, and compact flags such as
`HD`, `Dyn`, assigned `BG`, and sprite assignment count when present.
Selecting a frame updates the central canvas and the frame status line.

A frame the runtime skips has the line `inactive`, and its tool tip and the tool tip of the
frame status line say `inactive: shorter than 15 ms and not colorized — the runtime skips it`.
These are the frames a save flags as inactive (shorter than 15 ms, every pixel still the plain
color of the original, no dynamic zone; the rule of the serializer, which is the Win32 editor's)
that have no assigned background either: libserum identifies such a frame and renders nothing
for it, so a machine keeps the picture it shows. An inactive frame with a background is rendered
by libserum and is not marked. The marker follows edits: painting the frame, giving it a dynamic
zone or a background, or a duration of 15 ms or more removes it.

The filter above the frame list can show all frames, only frames with HD data (`HD frames`),
frames with an assigned background, frames with dynamic data, frames with sprite assignments, or
the frames marked `inactive` (`Inactive`).
Editing still targets the real project frame number shown in the item label, not the visible row
number.

In a project with 256x64 or 192x64 original frames there is no `HD` flag in the items, no blue
`HD` badge on the thumbnails and no `HD frames` filter entry, and the project status line above
the list leaves out the HD count (`<rom>: 5 frames, 2 backgrounds, 3 sprites`). The frame status
line names the plane by its size: `Frame <n> | 256x64 | <duration> ms`.
In a project with 256x64 original frames the filter has the entry `Not rendered` instead, and
frames the runtime shows nothing for are marked in the list; see "Projects With 256x64 Original
Frames".

The frame list supports native multi-selection. Frame drawing, comparison-mask drawing,
dynamic-mask drawing, background-mask drawing, and all frame settings (comparison mask ID, shape
mode, background ID, trigger ID, switch-to-monochrome, duration) apply to all selected frames where
the operation is valid. Comparison-mask edits are grouped by assigned mask ID, so a shared mask is
edited once even if multiple selected frames use it. Undo/redo history is still stored per frame,
per resolution, or per shared mask as appropriate.

When an edit cannot apply to some of the selected frames, those frames are skipped and the others
are edited. The status bar then reports both counts, for example
`Assigned background 1 on 3 frame(s), 1 skipped (no background data)`. Frames that already have the
requested value are neither changed nor counted.

## SD And HD Modes

Use the `SD` and `HD` toolbar buttons to switch the active editing resolution.

SD and HD exist in ordinary projects only (128x32 original frames with an optional 256x64 HD
plane). What the window shows for each kind of project:

| | Ordinary project | 256x64 original frames | 192x64 original frames |
| --- | --- | --- | --- |
| `SD` and `HD` toolbar buttons | shown | hidden | hidden |
| Frame list item | `HD` line for frames with HD | no `HD` line; `no picture`, `BG: no picture`, `Spr: no picture` for what is missing | no `HD` line |
| Thumbnail badge | blue `HD` | red `no picture` on frames that are not rendered | none |
| Frame filter | `HD frames` | `Not rendered` | neither |
| Project status line | `..., <n> HD, ...` | no HD count; `, <n> not rendered at runtime` | no HD count |
| Frame status line | `SD 128x32 \| HD 256x64` or `HD unavailable` | `256x64`, and `\| not rendered` | `192x64` |
| Frame page buttons | `Create HD`, `Remove HD` | `Create 256x64 Frame` | none |
| The same with `Edit Sprite` on | `Create HD Sprite`, `Remove HD Sprite` | `Create 256x64 Sprite` | none |
| Sprite and background lists | `(HD)` | `(no picture)` on records without 256x64 data | no suffix |
| Status texts, export file names | `SD`, `HD`, `_sd`, `_hd` | `256x64`, `_256x64` | `192x64`, `_192x64` |

Buttons and entries that do not apply are hidden, not disabled, and take no room: the toolbar
starts with `Mask` in a project without SD and HD, and the `Frame` page ends after `Import Image`
in a 192x64 project. While no project is loaded the window shows the ordinary set, disabled.

Rules:

- SD edits target the colorized frame pixels of the original plane, 128x32 in an ordinary
  project.
- HD edits target the colorized frame pixels of the extra plane, 256x64 in an ordinary project.
- A project with 256x64 original frames has one plane, the 256x64 one. See "Projects With 256x64
  Original Frames".
- A project with 192x64 original frames has one plane, the 192x64 one. See "Projects With 192x64
  Original Frames".
- The rest of this section is about ordinary projects.
- If a frame has no HD data, the canvas falls back to SD display/editing.
- Canvas hit testing maps mouse coordinates explicitly to SD or HD pixel coordinates.
- The Inspector `Create HD` button creates HD data for the current frame by scaling the SD colorized frame to 2x.
- When creating HD, a dialog asks whether to also upscale the frame background mask and the assigned background image.
- `Create HD` does not create HD dynamic data and the dialog does not offer it: the runtime renders
  the dynamic zones of the SD plane into the HD picture and does not read an HD dynamic mask. The
  frame's stored HD dynamic mask, colors and shadows are written as they are: for a frame that
  never had any that is a mask of 255 everywhere (no zone) and colors and shadows of 0, which is
  what the Win32 editor gives a frame without HD.
- In HD mode the `Dynamic` toolbar button is disabled, and the `Dyn` page shows and edits the
  dynamic colors and shadows of the SD plane. See "Dynamic Masks".
- The Inspector `Remove HD` button removes HD data from the current frame and switches the canvas back to SD when needed.
- `Create HD` and `Remove HD` can be undone. Undo puts back exactly what was there before: the
  frame's HD pixels (or their absence) and, for `Create HD`, every related item the dialog upscaled,
  including the previous HD image of the assigned background. See "Undo And Redo".

TODO: offer selectable scaling algorithms for HD creation instead of always using nearest-neighbor 2x scaling.

## Projects With 256x64 Original Frames

Most projects are made from 128x32 ROM frames. A revision 20 project can instead be made from
256x64 ROM frames (cROM header flag "256x64 original"). The frame status line of such a project
reads `Frame <n> | 256x64 | ...`.

The editor does not call anything in such a project "HD" or "SD": there is one plane, `256x64`.
This section still says "HD plane" and "HD data" where it describes the stored data, because
that is what the file format and libserum call the 256x64 plane.

The rule, which is libserum's: everything happens on the ROM frame at its own resolution.
Sprites are detected on the 256x64 original, the picture is rendered at 256x64, nothing is scaled
down, and a plane smaller than the ROM frame does not exist. So such a project has exactly one
rendered plane, the 256x64 one the file format calls "HD" or "extra". It lines up with the ROM
frame pixel for pixel: plane pixel (x, y) belongs to original pixel (x, y).

| Original | SD plane | HD plane |
| --- | --- | --- |
| 128x32 (ordinary project) | pixel (x, y) is original (x, y) | pixel (x, y) is original (x/2, y/2) |
| 256x64 | does not exist | pixel (x, y) is original (x, y) |

### What is edited

Everything the runtime uses, all of it on the 256x64 grid:

- the original frames and the comparison masks (frame identification hashes the whole 256x64
  original with the 256x64 comparison mask),
- per frame: the HD colorized frame, the HD dynamic mask, dynamic colors and shadows, the HD color
  rotations, the HD background mask, trigger, duration, mask and shape settings,
- the HD background images,
- sprites: the sprite original (what is detected), the detection areas, the shape mode, the HD
  sprite mask (what is drawn) and the HD sprite colors,
- per frame: the sprite slots and their bounds.

The canvas, the thumbnails, the frame list, playback, PNG export, image import, `Frame -> BG`,
`BG -> Frame`, background editing, the `Dynamic` and `BG` mask modes and the rotation and dynamic
pages all work on that plane.

### What is ignored

The file format still has room for 128x32 "SD" data, and the Win32 editor fills it: an SD
colorized frame, SD dynamic mask, colors and shadows, SD rotations and an SD background mask per
frame, SD background images, SD sprite colors and SD sprite dynamic data. libserum loads these
and never reads them. The editor does the same:

- They are loaded and written back unchanged, with their usual sizes, and never shown or edited.
  The file layout does not change.
- Nothing is derived from them: no upscaling of SD data into HD, no SD fallback for a missing HD
  item.
- Where the editor creates a sprite picture itself (`Capture`), it resets that sprite's unused SD
  data to neutral content: SD sprite colors all 0, SD sprite dynamic mask all 255 ("not dynamic"),
  SD sprite dynamic colors all 0. `Create HD` leaves the SD data of the frame, background or
  sprite as it is.
- On save a frame counts as colorized ("active") if its HD frame differs from the plain original
  colors or it has HD dynamic content. The SD frame does not count.

### No SD mode

The `SD` and `HD` toolbar buttons are hidden: there is nothing to switch. Every place where an
ordinary project falls back to SD when HD data is missing instead reports the missing 256x64
data in the status bar and does nothing (PNG export of a frame or background, `Frame -> BG`,
`BG -> Frame`, `Edit BG`, sprite `Capture`). The messages name the data by its size, for example
`Frame 4 has no 256x64 frame: there is no picture to export`.

### The 256x64 data is required everywhere

libserum renders a frame of such a project only if the frame has HD data, its assigned
background (if any) has HD data and every sprite assigned to it has HD data. Otherwise it still
identifies the frame and reports its trigger, but produces no picture: the display keeps what it
showed before. There is nothing it could fall back to.

Therefore:

- There is no `Remove` button, neither for a frame nor for a sprite: the ordinary project's
  `Remove HD` and `Remove HD Sprite` are hidden.
- A project in which that data is missing still opens. Each affected frame is flagged:
  - a red `no picture` badge on its thumbnail,
  - `no picture`, `BG: no picture` and `Spr: no picture` lines in the frame list item, with the
    full reason (which background, which sprites) as the item's tool tip, for example
    `Not rendered at runtime: background 1 has no 256x64 image; sprite 1 without 256x64 picture`,
  - `| not rendered` at the end of the frame status line, with the reason as its tool tip,
  - the project status line counts them: `..., 3 not rendered at runtime`,
  - the `Not rendered` filter of the frame list shows only these frames,
  - the sprite page lists an assigned sprite without its 256x64 data as
    `no picture: frame not rendered`, and the sprite and background lists, the background
    selector and the `Selected:` lines mark such records as `(no picture)`.
- A frame without its 256x64 frame is drawn as a black plane with the note `No 256x64 frame: the
  runtime shows nothing for this frame. Use Create 256x64 Frame.`, and cannot be painted on. Its
  original/reference region, comparison mask and settings can still be edited.
- Assigning a background or a sprite without 256x64 data to a frame is allowed and flags the
  frame the same way; `Assign` says so in the status bar.

Repairing:

- `Create 256x64 Frame` on the `Frame` page (the ordinary project's `Create HD`) is enabled for a
  frame that lacks something. For a frame without its 256x64 frame it creates that frame from the
  256x64 original, each pixel in the plain color of its original color index: the same picture an
  uncolorized frame has. It does not ask anything and upscales nothing; the frame's dynamic mask,
  dynamic colors, background mask and rotations stay as they are stored.
- `Create 256x64 Frame` is also enabled when only the assigned background lacks its 256x64
  image, and in both cases gives that background a black 256x64 image. An existing background
  image is never replaced. `Import Image` on the `BG` page is the other way to give a background
  its image.
- `Create 256x64 Sprite` (in the same place while `Edit Sprite` is on) creates the 256x64 sprite
  from the sprite original: its mask gets the shape of the sprite original, its colors are the
  plain colors of its color indices.
- All three can be undone.
- A button is disabled when there is nothing to create.

### Sprites

Sprite coordinates are pixels of the 256x64 original. That holds for the sprite original, the
detection areas, the detection dwords and for the `Min X`/`Min Y`/`Max X`/`Max Y` bounds of a
frame's sprite slot, which range over 0..255 and 0..63 here.

- A sprite is looked for on the 256x64 original, at every position inside the slot bounds, odd
  rows and columns included.
- A sprite that is found is drawn one to one where it was found: HD sprite pixel (i, j) lands on
  plane pixel (found x + i, found y + j). The HD sprite mask says which pixels are drawn, the HD
  sprite colors with what. Both line up with the sprite original pixel for pixel, and so does
  the sprite editor: the colorized sprite on top is as large as the original sprite below it.
  (In an ordinary project the HD sprite is twice the size of its original.)
- `Show Bounds` draws the bounds over the original as they are.
- `Select Capture` marks single pixels of the 256x64 original.
- `Capture` takes the marked pixels, or the slot bounds, of the 256x64 original as the sprite
  original, the HD colors of the same plane pixels as the HD sprite colors, and sets the HD
  sprite mask to the captured shape. The sprite has HD data afterwards. `Capture` needs a frame
  with HD data.

### On a 128x32 display

There is no 128x32 version of such a colorization. A host that asks libserum for 32-row output
only gets no picture (frames are still identified and triggers reported); a host that asks for
32-row output "with fallback", for 64-row output or for both gets the 256x64 picture. Whether and
how that is shown on a 128x32 panel is the host's business, not the colorization's.

### What the other parts of the editor do differently

- The original/reference region shows all 256x64 original pixels, in the same on-screen size as
  for a 128x32 original. One original pixel is exactly as large as one pixel of the plane above
  it.
- The comparison mask overlay and all comparison mask tools work on the 256x64 pixels of that
  region.

### Status

Verified with constructed projects only (the automated tests and `tools/ui_dump`); no real
project with 256x64 original frames was available. The model follows libserum (branch
`native_hd`, "The ROM frame, and `is256x64` projects" in its `AGENTS.md`), which is the source of
truth. The Win32 editor's handling of such projects is not a reference: it treats the 128x32
data as a live plane, which the runtime does not.

Where the editor's picture can still differ from libserum's for these projects:

- A frame with HD data whose background or sprite lacks HD data is still drawn, without that
  background or sprite, so that it can be edited. The runtime draws nothing for it. The frame is
  flagged as described above.
- The Runtime view shows libserum's own picture of such a frame, that is its message that there
  is none, see "Runtime View".

## Projects With 192x64 Original Frames

Some ROMs deliver 192x64 frames. The Win32 editor stores such a project with the 192x64 ROM frame
as its original plane and a half-size 96x32 plane as its extra plane. The frame status line of
such a project reads `Frame <n> | 192x64 | ...`.

The same rule as for 256x64 projects applies: everything happens on the ROM frame at its own
resolution, and a plane smaller than the ROM frame is not a plane to edit. So the project has one
plane in the editor, the 192x64 one. It is the ROM frame's own grid: plane pixel (x, y) belongs
to original pixel (x, y). The editor calls it `192x64`, never SD or HD.

### What is edited

Everything on the 192x64 plane, with coordinates 0..191 and 0..63:

- the original frames and the comparison masks (frame identification hashes the whole 192x64
  original with the 192x64 comparison mask),
- per frame: the colorized frame, the dynamic mask, dynamic colors and shadows, the color
  rotations, the background mask, trigger, duration, mask and shape settings,
- the background images,
- sprites: the sprite original, the sprite colors, the detection areas; per frame the sprite
  slots and their `Min X`/`Min Y`/`Max X`/`Max Y` bounds, which range over 0..191 and 0..63.

The canvas, the thumbnails, the frame list, playback, PNG export, image import (the image is
scaled to 192x64), `Frame -> BG`, `BG -> Frame`, background editing, the `Mask`, `Dynamic` and
`BG` modes, `Select Capture`, `Capture` and `Show Bounds` all work on that plane. A sprite is
looked for on the 192x64 original and drawn one to one where it was found. The sprite editor shows
the same 256x64 sprite cell as in every project.

On the canvas a 192x64 pixel is as large as a pixel of an HD frame or of a 256x64 original; the
frame is 3:1 instead of 4:1, and the thumbnails keep that shape.

On save a frame counts as colorized ("active") if its 192x64 frame differs from the plain
original colors or it has dynamic content on the 192x64 plane, or if it lasts 15 ms or longer.

### The 96x32 data is discarded

The file has room for a full set of 96x32 data: a colorized frame, dynamic mask, dynamic colors
and shadows, rotations and a background mask per frame, background images, sprite masks, sprite
colors and sprite dynamic data, and the flags that say which frames, backgrounds and sprites
have such data.

The editor drops all of it when the project is opened and holds no 96x32 data. On save the
blocks of the extra plane are written as "no extra data", in the same sizes as before, so the
file layout and the header (192x64 with 96x32) do not change and nothing is padded or converted:

| Extra-plane data | Written as |
| --- | --- |
| "has extra data" flag of every frame, background and sprite | 0 |
| colorized frames, background images | 0 |
| dynamic masks | 255 (not dynamic) |
| dynamic colors, dynamic shadow directions and colors | 0 |
| color rotations | 0 (no rotation) |
| background masks | 1 |
| sprite masks | 255 (not part of the sprite) |
| sprite colors | 0 |
| sprite dynamic masks | 255 (not dynamic) |
| sprite dynamic colors | 0 |

These are the values the Win32 editor gives a new frame, background or sprite that has no extra
resolution.

What this means:

- A project that never had 96x32 pictures loses nothing visible. Its file can still change on
  the first save, because the Win32 editor leaves other values in the unused blocks (for example
  dynamic colors it copied to both planes, or background masks of 0 in a project converted from
  an old file format).
- A project that had 96x32 pictures in use (frames, backgrounds or sprites flagged as having
  extra data) loses them with the first save from this editor. They cannot be brought back from
  the saved file; keep a copy of the original files if they matter. The 192x64 picture is not
  affected.
- Saving twice writes the same bytes: after the first save the extra plane is already "no extra
  data".
- Everything on the 192x64 plane, and the whole `.cRP`, is written back as it was loaded or
  edited.

### What is not there

- No `SD` and `HD` toolbar buttons.
- No `Create HD`, `Remove HD`, `Create HD Sprite` or `Remove HD Sprite` button, and therefore no
  `Create HD` dialog.
- No `HD` flag, badge, filter entry or counter, and no `(HD)` suffix in the sprite and
  background lists.
- Status texts and proposed file names say `192x64` where they say `SD` or `HD` in ordinary
  projects.

### Status

Verified with one real project saved by the Win32 editor 3.17.3 (`frankst1`: 11199 frames, no
sprites, no backgrounds, no 96x32 pictures in use), and with constructed projects in the
automated tests and `tools/ui_dump` that also have sprites, backgrounds and 96x32 data in use.
`frankst1` loads, shows and saves; a load and save changes two blocks of its extra plane, the
dynamic colors (the file has values there) and the background masks (the file has 0, the editor
writes 1), and nothing else.

Nothing is padded or converted to another geometry.

The editor's picture of the 192x64 plane is the same as libserum's, see "Preview Rendering";
the Runtime view shows libserum's own.

## Canvas

The central canvas shows:

- The active colorized frame region.
- The original/reference frame region below it. It always shows every pixel of the original
  frame and has the same on-screen size as the SD frame: 128x32 pixels, or 256x64 pixels of
  half the size in a project with 256x64 original frames, where the colorized region above it is
  always the 256x64 plane. In a project with 192x64 original frames both regions are 192x64
  pixels of that half size, so they are three quarters as wide.

The canvas does not draw text labels or white frame outlines over the image data.

Drawing previews are transient overlays. They do not modify project data until mouse release or click commit.

Use the `Mask` toolbar toggle to show the current frame's assigned comparison mask over the original/reference region.
When `Mask` is active, clicks on the original/reference region add pixels to the assigned comparison mask.

Use the `Dynamic` toolbar toggle to show and edit dynamic-mask pixels over the colorized frame.
Dynamic zones belong to the plane that has the size of the original frame; in an ordinary project
that is the SD plane, and the toggle is disabled in HD mode. See "Dynamic Masks".

Use the `BG` toolbar toggle to show and edit the current frame's background-mask pixels over the active colorized frame.
The background-mask overlay and edits follow the active SD/HD mode.

The left sidebar is the visual component browser:

- `Frames` shows frame thumbnails and the frame filter.
- `Masks` shows comparison-mask thumbnails; selecting one selects that mask in the Inspector `Mask`
  topic for assignment. Selecting does not change any frame, and the selection stays when another
  frame becomes current.
- `Backgrounds` shows background thumbnails; selecting one selects the same background in the Inspector for assignment or editing.
- `Sprites` shows sprite thumbnails; selecting one selects the same sprite in the Inspector.

Frame metadata, dynamic colors, and sprite controls live in selectable topics in the right-side Inspector:

- `Frame` contains frame/background assignment, trigger IDs, DMD render settings, and the HD frame
  actions (`Create 256x64 Frame` alone in a project with 256x64 original frames, none in one with
  192x64 original frames).
- `Mask` contains comparison-mask assignment, comparison mode, and a shortcut into mask editing.
- `Background` contains the selected background ID, assignment, and edit shortcut.
- `Dynamic` contains the dynamic set selector and dynamic color swatches. In HD mode of an
  ordinary project it shows the SD plane's colors and shadows, with a note saying so.
- `Rotation` contains color rotation slots for the current frame and active SD/HD mode.
- `Palette` contains the selected cRP palette name and all 64 palette colors.
- `Sprites` contains sprite selection, naming, assignment, bounds, detection areas, and sprite editing.

The `Frame` topic exposes:

- `Target` selects whether ordinary drawing tools edit the current frame or the selected background image.
  It is enabled whenever the project has a background to edit, that is, whenever a background is
  selected.
- `BG ID` assigns the shared background for all selected frames. Selected frames without background
  data are skipped.
- `Trigger ID` edits the per-frame legacy trigger ID for all selected frames. `None` means the frame
  triggers nothing and is stored as `0xFFFFFFFF`, the value the cROM uses for it; `0` is an ordinary
  trigger ID. New frames start with `None`.
- `Duration` edits the per-frame `.cRP` duration for all selected frames.
- `Switch mono` writes the legacy switch-to-monochrome trigger ID `65432` on all selected frames and
  disables manual trigger-ID entry while checked. Unchecking it sets `Trigger ID` to `None` on the
  selected frames that carry the monochrome trigger; selected frames with an ordinary trigger ID
  keep it. To get an earlier trigger ID back, use Undo.
- `BG ID`, `Trigger ID`, `Duration`, and `Switch mono` edits can be undone, see "Undo And Redo".
- `DMD Shape` selects square pixels or round dot pixels for the main canvas and PNG export.
- `DMD Size` sets the relative pixel fill size in 10% steps. Values below `100%` leave black spacing between pixels, which makes the DMD grid visible.
- `Show BG` renders the assigned background layer into the colorized frame preview and is enabled
  by default.
- `Import Image` replaces selected frame pixels from an image file in the active SD/HD mode.

`DMD Shape` and `DMD Size` are view/export settings only. They do not change project data or the cROM/cRP file format.

The `Background` topic exposes:

- Backgrounds are selected visually from the left sidebar.
- The tab shows the selected background ID.
- `Assign BG` assigns the selected background to all selected frames.
- `Edit BG` switches ordinary drawing tools to the selected background image. If the sprite editor
  is open, it is closed first (`Edit Sprite` is switched off).
- `Frame -> BG` replaces the selected background image with the current frame image in the active SD/HD mode.
- `BG -> Frame` copies the selected background image into all selected frames in the active SD/HD mode.
- `Import Image` replaces the selected background image from a PNG/BMP/JPEG, scaled to the active SD/HD DMD size.
  In HD mode, a background without HD data gets its HD image from the import; undoing the import
  removes that HD image again.
- `Export BG` writes the selected background image as a PNG using the current DMD dot/square render settings.

## Runtime View

`View -> Runtime View` (`Ctrl+R`) shows the current frame on the canvas as libserum renders it,
in place of the editable layers. The checkable `Runtime` button in the toolbar, right of the
playback controls, is the same switch and shows whether the view is on. The picture is libserum's own output, not the editor's
rendering: the editor writes a small cROM that holds this frame and what it uses (its comparison
mask, its background, the sprites in its sprite list), loads it into libserum from memory, feeds
it the frame's original and shows the plane that comes back. Nothing is written to disk.

While the view is on:

- A line above the picture says `Runtime view: libserum output, N rows. Editing is off.`
  (`N rows, playing` during playback), the frame status line reads
  `Frame N | Runtime view, N rows | editing off`, and the drawing tool buttons, `SD`, `HD`,
  `Mask`, `Dynamic` and `BG` are disabled.
- The canvas takes no clicks or strokes, and no overlay is drawn: no comparison mask, dynamic
  mask, background mask, sprite bounds, selection or tool preview. The original/reference region
  below the picture stays.
- The Inspector keeps working. A change of the document shows in the picture at once: an edit
  made in the Inspector, undo and redo.
- `Show BG` has no effect: libserum always renders the assigned background.
- Opening the sprite canvas (`Edit Sprite`) switches the view off.

Switching the view off returns to editing exactly as it was: the same mode, tool, overlays and
selection.

Which plane is shown:

- In an ordinary project `View -> Runtime Plane: 32 Rows` (`Ctrl+3`) and `Runtime Plane: 64 Rows`
  (`Ctrl+6`) choose between what a display with 32 rows and one with 64 rows gets. When the view
  is switched on it starts with the plane of the editing mode, 32 rows in SD and 64 rows in HD.
  The 64-row plane exists for every frame: for a frame without HD it is libserum's upscale of
  the 128x32 picture, which the editor cannot show in any other way.
- A project with 256x64 or 192x64 original frames has one plane and no choice; the two menu
  entries are hidden.

When libserum has no picture, the reason is shown in its place:

- `libserum identifies this frame and shows no picture for it.` In a project with 256x64
  original frames this is a frame without its 256x64 data, or with a background or sprite
  without it.
- `libserum identifies this frame and skips it: it is inactive (shorter than 15 ms and not
  colorized), so a machine keeps the picture it shows.` These are the frames the frame list marks
  `inactive`, see "Frame List". The frame status line ends in `| inactive` for them.
- `libserum does not identify this frame.` No frame of the project matches the original.
- `libserum: ...` with libserum's own error text, if the frame could not be loaded.

Playback with the view on is done by libserum. `Play` feeds the frames of the list to it in list
order, from the current one on, with the usual timings; libserum keeps its state from one frame
to the next as it does on a machine, and its color rotations run on libserum's own clock.
Switching the view or its plane stops playback.

**Frame identification depends on order.** libserum does not recognise a frame by its picture
alone. Every frame has a comparison mask and a shape mode, and its hash is taken from its
original under that mask. Masks are shared, so several frames can have the same hash: frames
that differ only where their mask covers them, or a frame of one mask that looks like a frame of
another under the other's mask. For such an original libserum's answer depends on what it
identified before:

- its search starts at the frame it identified last (the first frame of the project after
  loading) and walks through the frames in project order, wrapping around at the end;
- the first time it meets a mask and shape mode it hashes the incoming frame with them and looks
  for frames of that mask and mode with this hash; the first mask and mode that has one decides;
- of those frames it takes the first at or after the place where it met that mask and mode.

Playback in the Runtime view follows this exactly as a machine with the whole project loaded
does, starting like a machine that has just loaded the project. So of several frames that match
an original it shows the one a machine shows after the frames played before, which need not be
the frame selected in the list. Where the two differ the view says so: the frame status line
ends in `| identified as frame N`, the line above the picture adds
`Note: libserum identifies this ROM frame as frame N.`, and the tool tip of the status line
explains it. An author who sees this has two frames a machine cannot tell apart in this order,
which usually means one of them needs another comparison mask.

Outside playback the view shows the selected frame rendered on its own, whatever a machine would
make of its original. If a machine coming from the frame before it in the project (from the
first frame, for the first one) would identify the original as another frame, the status line
ends in `| after frame M: frame N` and the line above the picture adds
`Note: after frame M a machine identifies this ROM frame as frame N.` The picture stays that of
the selected frame. This hint is computed from the search rule above and costs no rendering.

How playback is loaded: the project is never loaded into libserum as a whole. The editor loads a
window: the next frames to play, at most 48, together with every other frame of the project that
takes part in identifying them. These are the frames that match the original of a played frame
(the ones the search can end at and start from) and, for each of them and each of their masks
and shape modes, the first frame with that mask and mode at or after it (the frames at which the
search meets the mask). With them the search runs in the window as it does in the whole project.
A window usually holds a few frames more than it plays; it stops growing at 160 frames, and
then plays fewer than 48. When playback goes past a window the next one is loaded and libserum's
identification state is carried over: the new window starts with the frame identified last, and
the original fed last is fed once more, unseen. Loading a window takes 15 to 100 ms on the real
projects measured (about 3 to 14 MB each); a whole project of 11199 frames plays in 234 windows.

Limits of that:

- What libserum keeps from frames before a window other than the identification, namely a
  monochrome switch set off by an earlier frame and the phase of running color rotations, starts
  again with the window.
- On the 64-row plane libserum leaves single pixels as an earlier frame painted them (seen at
  one or two pixels in 19 of 4430 played frames of `smb3` and 13 of 5211 of `trn_174h`). Which
  earlier frame that was differs between a window and a machine that has played the whole
  project. The 32-row plane was identical in every played frame.

What the view does not show:

- Scenes from a `.pup.csv` and the settings of a `scaling.txt`: only the cROM is loaded, so the
  64-row plane uses libserum's default upscaling (Scale2x) and shadow offset.
- Triggers, and what a trigger does on a machine. A frame with `Switch mono` is shown colorized.

What the editing canvas shows, by comparison: it is the editor's own rendering of the layers
being edited. On the plane that has the size of the original frame it follows libserum's rules
and gives the same pixels (see "Preview Rendering" for how that is checked). On the HD plane of
an ordinary project it follows them too, with pixels doubled where libserum upscales with
rounded edges; see the known differences there.

## Colors

The toolbar contains fixed swatches:

- White `0xffff`
- Red `0xf800`
- Green `0x07e0`
- Blue `0x001f`
- Black `0x0000`

The active draw color can also be set with the `Pick` tool. If the picked RGB565 color is not one of the fixed swatches, no swatch is checked, but the picked color is still used by later drawing tools.
The read-only color box next to the fixed swatches always shows the current active draw color.

## Drawing Tools

Drawing tools operate on the active SD/HD mode.

When Inspector `Target` is `Frame`, ordinary drawing tools edit the active colorized frame.

When Inspector `Target` is `Background`, ordinary drawing tools edit the selected shared background image. `Pick` samples from that background image, flood fill fills the background image, and the edits are recorded in the history of that background and resolution.

### Point

Click one pixel to paint it with the active draw color.

### Line

Press, drag, and release to draw a line.

The preview is shown while dragging. The whole line is committed on release as one undoable edit.

### Rect

Press, drag, and release to draw a rectangle outline bounded by the start and end pixels.

The preview is shown while dragging. The whole rectangle is committed on release as one undoable edit.

### Fill Rect

Press, drag, and release to draw a filled rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled rectangle is committed on release as one undoable edit.

### Circle

Press, drag, and release to draw a circle outline.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole circle is committed on release as one undoable edit.

### Fill Circle

Press, drag, and release to draw a filled circle.

The drag defines a square from the start pixel toward the cursor. The shorter drag axis constrains the circle size.
The preview is shown while dragging. The whole filled circle is committed on release as one undoable edit.

### Ellipse

Press, drag, and release to draw an ellipse outline inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole ellipse is committed on release as one undoable edit.

### Fill Ellipse

Press, drag, and release to draw a filled ellipse inside the rectangle bounded by the start and end pixels.

The preview is shown while dragging. The whole filled ellipse is committed on release as one undoable edit.

### Grad

Press, drag, and release to fill the selected Palette `Shape` with a directional palette gradient.

The filled pixels sample the actual palette entries from `From` through `To`; the canvas tool does
not recalculate colors between the endpoints. Palette color numbers are displayed as `1` through
`64`, while the saved cRP data remains unchanged internally.

The Palette tab separates gradient behavior into:

- `Shape`: the selected area to fill, currently rectangle, circle, or ellipse.
- `Pattern`: how colors are distributed inside the selected area, currently linear, circle, or ellipse.
- `Direction`: the color order used by the fill. `Free` is a two-step workflow: first draw the shape
  or selection area, then draw a virtual direction line that defines the gradient vector. The fixed
  linear directions ignore the mouse drag direction and use the selected area's bounds instead.
  Circle and ellipse patterns use the selected area's bounds for radial fills; `Edge -> Center`
  reverses radial fills.

This is currently implemented for colorized frame pixels and selected background image pixels. Each
gradient fill is committed as one undoable edit.

### Sel

Use `Sel` to create a persistent canvas selection before applying a palette gradient to that exact
area.

Create a selection:

1. Open the Inspector's `Palette` topic.
2. Choose the selection shape in `Shape`: rectangle, circle, or ellipse.
3. Select the toolbar tool `Sel`.
4. Press, drag, and release on the colorized canvas.
5. The selected pixels are shown as a translucent cyan overlay.

Fill a selection with a fixed-direction gradient:

1. Create a selection.
2. In the `Palette` topic, set `From` and `To`.
3. Set `Pattern` to `Linear`, `Circle`, or `Ellipse`.
4. Set `Direction` to a fixed direction such as `Left -> Right`, `Top -> Bottom`,
   `Center -> Edge`, or `Edge -> Center`.
5. Click `Grad Sel`.

Fill a selection with a free-direction gradient:

1. Create a selection.
2. In the `Palette` topic, set `From`, `To`, and `Pattern`.
3. Set `Direction` to `Free`.
4. Click `Set Dir`.
5. Press, drag, and release on the same canvas to draw the virtual gradient direction line.
6. The stored direction line is shown in the cyan selection overlay.
7. Click `Grad Sel`.

Clear or replace a selection:

1. Click `Clear Sel` to clear the current selection.
2. Or select `Sel` again and draw a new selection; the previous selection and stored direction line
   are replaced.

Selections are cleared when the frame, SD/HD mode, target, selected background, or tool context
changes. Selection gradient fills are undoable through the same frame/background edit histories as
ordinary pixel edits.

### Pick

Click a colorized frame pixel to set the active draw color to that pixel's RGB565 value.

When the `Dynamic` overlay is enabled, click a colorized frame pixel to select the dynamic set
stored at that pixel instead. This is useful before editing the set's dynamic colors or shadows.

This tool does not modify project data and does not create an undo entry.

### Flood

Click a colorized frame pixel to flood-fill the contiguous four-neighbor area that matches the clicked pixel's current color.

The fill uses the active draw color. The whole filled area is committed as one undoable edit.

TODO: consolidate frame, mask, background, and sprite drawing behind a shared editable-surface
pipeline so tools, previews, flood fill, undo/redo, and deferred dependent-view refresh are
implemented once and reused across canvas targets.

## Undo And Redo

Every edited object has its own undo/redo history. Undo and Redo act on the histories that belong
to the current editing context; a new edit clears redo only in the history it is recorded in.

Histories:

- Colorized frame pixels: per frame and per SD/HD resolution.
- Frame settings: per frame, for both resolutions together. This history holds the comparison mask
  ID, shape mode, background ID, trigger ID (including `Switch mono`), and duration edits of the
  frame, and its `Create HD` and `Remove HD`.
- Comparison masks: per mask. Frames that share a mask share its history.
- Dynamic masks, dynamic colors, and dynamic shadows: three separate histories per frame. They
  belong to the plane the dynamic data is edited on, which is one per project (see "Dynamic
  Masks"): an ordinary project has no histories for HD dynamic data, which cannot be edited.
- Background masks: per frame and per SD/HD resolution.
- Background images: per background and per SD/HD resolution.
- Sprite color images: per sprite and per SD/HD resolution.
- Sprite HD: per sprite, for both resolutions together. It holds `Create HD Sprite` and
  `Remove HD Sprite`.
- Palette colors: per palette.

Which histories Undo and Redo use, by context:

1. `Edit Sprite` active with a sprite selected: the selected sprite's color image history for the
   active resolution (its SD history when the sprite has no HD data) and its sprite HD history.
2. `Mask` active: the comparison mask assigned to the current frame and the current frame's
   settings.
3. `Dynamic` active: the current frame's dynamic-mask, dynamic-color, and dynamic-shadow
   histories.
4. `BG` active: the current frame's background-mask history for the active resolution and the
   current frame's settings.
5. Otherwise: the current frame's pixel history for the active resolution, the current frame's
   settings, the selected palette, the current frame's dynamic-color and dynamic-shadow histories
   (the same ones in SD and in HD mode), and the image history of the selected background for the
   active resolution. This is the same whether Inspector `Target` is `Frame` or `Background`, so a
   `Frame -> BG` copy or a background image import can be undone without switching the target.

Within a context, Undo reverts the edit that was made most recently among all of the context's
histories, whichever history it is in. For example, after a frame pixel edit followed by a palette
edit, Undo reverts the palette edit first and the pixel edit second. Redo reapplies the edit that
was undone most recently, so Undo followed by Redo changes nothing and repeated Redo replays the
edits in the order in which they were originally made. An edit that has just been redone counts as
the most recent edit again.

Only the histories of the current context take part. Edits made in other contexts, on other frames,
or in the other SD/HD mode stay in their own histories and are skipped; switch to that context to
undo them. Switching frame, SD/HD mode, edit mode, or selected background or palette updates
whether Undo and Redo are available.

An edit applied to several selected frames is recorded once per changed frame, in that frame's own
history. Undo therefore reverts it only on the current frame; select another frame to undo it
there. This applies to drawing, to dynamic shadow direction and color changes, and to all frame
settings.

Undoing `Create HD` removes the HD frame again and restores the HD data the dialog options had
overwritten: the frame's HD background mask and the HD image of the background that was assigned
at the time (or its absence). Undoing `Remove HD`
restores the HD pixels that were removed. Undo and Redo of these two switch the canvas back to SD
when the current frame is left without HD data; Redo of `Create HD` does not switch to HD by
itself. The same holds for `Create HD Sprite` and `Remove HD Sprite`, which restore the sprite's HD
mask and HD colors.

Histories are dropped when the data they refer to is replaced as a whole:

- Opening a project drops all histories.
- `Remove HD` drops the frame's HD pixel history.
- `Create HD` drops the frame's HD pixel history. Each related item that is upscaled in the dialog
  also drops its HD history: the frame's background mask and the image of the assigned
  background. Dynamic histories are not touched.
- `Create HD Sprite` and `Remove HD Sprite` drop the sprite's HD color image history.
- Undoing or redoing any of these four drops the same histories again, because the HD data is
  replaced as a whole in both directions. Pixel edits made on HD data in between can therefore not
  be undone separately afterwards.
- Sprite `Capture` drops the sprite's SD and HD color image histories and its sprite HD history.

All other histories stay available. Importing an image into a background and copying a frame to a
background are ordinary undoable edits and do not drop any history.

Not undoable: sprite slot assignment, removal and bounds, sprite detection areas, sprite and palette
names, sprite capture, and color rotation settings.

## Comparison Masks

The `Mask` toolbar toggle shows the assigned comparison mask as a semi-transparent red overlay on the original/reference region.

A comparison mask has one pixel per original pixel. In a project with 256x64 original frames
the masks are 256x64, and the overlay, the tools, flood fill, erasing and undo all work on those
256x64 pixels of the original/reference region. In a project with 192x64 original frames the
same holds for its 192x64 masks.

The Inspector exposes per-frame comparison metadata:

- The `Mask` topic shows the visually selected mask from the left sidebar as `Selected: <ID>`, with
  `(assigned)` when it is the mask of the current frame. Selecting a mask in the sidebar only
  selects it, in the same way as selecting a background or a sprite does; no frame is changed. The
  selection starts as the mask of the first frame and then stays until another mask is selected.
- `Assign Mask` assigns the selected mask to all selected frames.
- `Edit Mask` enables the mask overlay and returns the canvas to the frame preview. If the sprite
  editor is open, it is closed first (`Edit Sprite` is switched off). It edits the mask assigned to
  the current frame, which is not necessarily the mask selected in the sidebar.
- `Mask ID` shows the comparison mask assigned to the current frame and assigns the entered mask
  directly to all selected frames. `None` stores the legacy no-mask assignment.
- `Shape` selects `Full comparison` or `Shape mode` for all selected frames.
- Both fields are saved back into `.cROM`.
- Mask ID and shape mode edits can be undone, see "Undo And Redo".

Editing rules:

- Mask editing is active only while the `Mask` toggle is on.
- Colorized-frame drawing is paused while the `Mask` toggle is on.
- Click the original/reference region to set that mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill also edit the assigned mask from the original/reference region.
- Shape tools show a red live preview while drawing on the mask.
- Hold `Shift` while clicking, drawing, or flood filling the original/reference region to erase mask pixels back to `0`.
- Shift-erase shape tools show a dark live preview.
- Edits apply to the assigned comparison mask, not just the current frame.
- If the current frame has no valid comparison mask assignment, clicking the original/reference region does nothing and shows a short status message.
- Mask pixels are saved back into `.cROM`.

## Dynamic Masks

The `Dynamic` toolbar toggle shows the current frame's dynamic-mask pixels as a semi-transparent cyan overlay.

**Dynamic zones belong to one plane: the one with the size of the original frame.** That is the
128x32 (SD) plane in an ordinary project, the 256x64 plane in a project with 256x64 original
frames and the 192x64 plane in one with 192x64 original frames. It is the plane the runtime
takes them from. In an ordinary project libserum renders the 64-row picture in layers: the HD
frame and HD background as they are, and the dynamic content at 128x32, with the SD dynamic
mask and the SD dynamic colors, upscaled and put on top. It does not read the HD dynamic mask
and the HD dynamic colors at all (libserum, `Colorize_Framev2`: with a 64-row request a 128x32
colorization always runs in "layer mode", where `frameHasDynamicExtra` is forced false;
libserum's AGENTS.md, "Resolution scaling and the two-layer renderer": "HD dynamic masks are
ignored outright").

So in an ordinary project:

- Dynamic zones are shown and painted in SD mode. In HD mode the `Dynamic` button is disabled and
  its tool tip says `Dynamic zones are defined on the 128x32 plane: switch to SD to see and paint
  them`. Switching to HD while `Dynamic` is on switches it off, with a message in the status bar.
  The zones are not drawn over the HD frame: at twice the size they would look like something
  that can be painted there, and the HD picture on the canvas already shows what they do.
- The `Dyn` page works in both modes and always shows and edits the SD plane's dynamic colors
  and shadows, which are the ones both pictures use. In HD mode a note at the top of the page
  says so. An edit made there in HD mode shows in the HD picture at once and is undone from
  either mode.
- The editing canvas and `Frame -> BG` in HD mode render the HD plane as libserum does:
  dynamic pixels have the SD set's color for the original's shade, every SD pixel doubled.
  libserum rounds the edges of that upscale (Scale2x); the Runtime view with
  `Runtime Plane: 64 Rows` shows the exact picture.
- HD dynamic masks, colors and shadows a project already holds are kept: they are loaded,
  written back unchanged and never shown or edited. The Win32 editor writes them, as a
  line-doubled copy of the SD ones.
- One thing of the stored HD data does reach the picture: **shadows**. A dynamic set that has no
  shadow direction on the SD plane takes its direction and color from the HD shadow tables when
  libserum renders the 64-row plane (they are settings per set, not pictures). The editor's HD
  rendering does the same. The `Dyn` page shows and edits the SD shadow only: a shadow that
  comes from the HD tables can be replaced by setting an SD direction for the set, and cannot be
  switched off in this editor.
- The save rule that writes colorized pixels under a dynamic mask as 0 still uses the stored HD
  mask for the HD frame, see "Saving".

Rules:

- The `Dyn Set` field in the Inspector's `Dynamic` topic selects the dynamic set written by drawing tools.
- The Inspector shows editable dynamic color swatches for the active frame and the selected dynamic set.
- Click a dynamic color swatch to choose a replacement RGB565 color for that original color index.
- The dynamic mask is rendered over the colorized frame of its plane.
- `Mask` and `Dynamic` are mutually exclusive overlay modes.
- Click the colorized frame to set that dynamic-mask pixel to the selected `Dyn Set`.
- With the `Pick` tool, click the colorized frame to select the dynamic set stored under the cursor.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the dynamic mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase dynamic-mask pixels back to `255`.
- Dynamic-mask edits are undoable per frame.
- Dynamic color changes are undoable per frame.
- Dynamic shadow direction and color changes apply to all selected frames that have dynamic data
  and are undoable per frame.

Dynamic shadows are loaded from and saved back to the legacy `.cROM` dynamic-shadow blocks. The
canvas and thumbnails render shadows after dynamic-color replacement and before sprite
composition. Shadow pixels are written only into neighboring pixels that are not themselves dynamic,
matching the legacy renderer behavior. On the HD plane of an ordinary project a shadow is one HD
pixel wide around the doubled zone, as libserum generates it there.

The Inspector's `Dynamic` topic provides shadow controls for the active dynamic set:

- Direction buttons `TL`, `T`, `TR`, `L`, `R`, `BL`, `B`, and `BR` toggle where shadows are cast.
- `Shadow Color` opens a color picker for the active set's shadow color.
- Shadow edits apply to all selected frames that have dynamic data.

## Color Rotations

Modern `.cROM` projects store four color-rotation slots per frame and per SD/HD resolution. Each
slot contains a length, a delay in milliseconds, and up to 62 RGB565 colors.

The Inspector's `Rotation` topic edits the active frame's rotation slots for the current SD/HD mode:

- `Slot` selects one of the four rotation slots.
- `Length` sets the number of active colors in that slot. A length of `0` disables the slot.
- `Delay` sets the milliseconds between rotation steps.
- The color grid shows all colors in the selected slot at once. Active colors can be clicked to
  open a color picker; colors outside the active length remain visible but disabled.
- `Clear Slot` resets the selected slot to all zeros.

Length, delay, grid color edits, and clear operations apply to all selected frames. Rotation data is
loaded from and saved back to `.cROM`. The static canvas shows unrotated frame data; playback applies
an editor-side rotation preview.

## Palettes

Legacy `.cRP` files can store 256 palettes with 64 RGB565 colors each, plus 64-byte palette-name
slots. When that table exists, the Qt rewrite loads it into the document model and saves edits back
to the same `.cRP` offsets.

The Inspector's `Palette` topic provides a palette selector, the selected palette name, and a
64-color grid:

- Edit the name field to rename the selected palette. Names are capped at 63 characters so they fit
  the fixed cRP name slot.
- Click any palette color to make it the active drawing color.
- Drag any palette color onto one of the four toolbar favorite color slots to replace that favorite.
- Double-click any palette color to open a color picker for that RGB565 palette entry.
- Palette color cells and `From`/`To` controls are displayed as `1` through `64`.
- Use `From`, `To`, and `Apply Gradient` to fill a palette range by interpolating between the
  endpoint colors. Reversing `From` and `To` reverses the generated palette range. The red, green,
  and blue RGB565 components are interpolated separately and rounded to the nearest value; the two
  endpoint colors themselves are never changed.
- Use `Shape`, `Pattern`, and `Direction` to control the canvas `Grad` drawing tool.
- Palette color changes and gradient fills are undoable per palette.
- Palette color/name changes mark the project dirty and are saved with the `.cRP` file.

The current frame renderer does not yet use these palettes for color-rotation playback or gradient
fill previews outside the Palette editor. Palette editing is the storage/UI foundation for those
next steps.

## Sprites

Existing sprite data is loaded into the document model and saved back through the legacy `.cROM`
layout without changing record counts or binary structure.

Currently preserved sprite data includes:

- Sprite names from the `.cRP` sidecar, when available.
- Sprite original masks.
- SD and HD sprite color images.
- HD sprite masks.
- Sprite detection dwords, positions, and areas.
- Per-frame sprite assignments and bounding boxes.

The Inspector has a sprite browser and assignment controls:

- The sprite selector lists loaded sprite records and shows `.cRP` sprite names when available.
- The sprite name field edits the selected sprite name. Names are saved back to the fixed-size
  `.cRP` sprite-name table when that table exists.
- `Show Det Areas` overlays valid per-sprite detection areas on the original sprite in the sprite
  editor.
- The detection area selector chooses one of the sprite's detection areas. The `X`, `Y`, `W`, `H`
  fields edit an area that is set; they are disabled while the selected area is not set. A new
  area is created by dragging it in the sprite editor (see below).
- `Clear Area` disables the selected sprite detection area using the legacy `0xffff` marker.
- `Edit Sprite` switches the main canvas to a large sprite editor with colorized sprite on top and original sprite below.
- While `Edit Sprite` is active, `Create HD Sprite` creates HD sprite mask/color data by scaling the SD sprite data to 2x, and `Remove HD Sprite` clears the sprite HD-present flag. Both can be undone while the sprite editor is active; Undo restores the sprite's previous HD mask and HD colors.
- In the sprite editor canvas, drag on the colorized sprite to repaint existing non-black sprite pixels with the active draw color.
- Hold `Shift` while dragging on the colorized sprite to erase existing non-black sprite pixels back to black.
- The sprite editor supports the toolbar point, line, rectangle, filled rectangle, circle, filled circle, ellipse, and filled ellipse tools on the colorized sprite.
- The sprite editor also supports the toolbar pick and flood-fill tools on the colorized sprite.
- Sprite shape tools show a live preview and commit the full stroke on mouse release.
- Sprite pixel edits update the sprite editor immediately. Frame thumbnails and the composed frame
  preview refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Undo and redo for sprite color edits are scoped per sprite and per active SD/HD resolution, and
  are available while the sprite editor canvas is active.
- Drag on the lower original sprite canvas to set the selected detection area.
  Detection-area edits update the sprite editor immediately. Frame thumbnails and composed frame
  previews refresh only after `Apply Sprite`, leaving the sprite canvas, or saving.
- Sprite source/original pixels are not freehand editable.
- `Capture` copies original and colorized pixels from the current frame into a sprite, using the
  selected sprite slot bounds as the source rectangle. Captured pixels are placed at the sprite
  origin, black original pixels become transparent sprite pixels, detection area `0` is set to the
  captured rectangle, and the selected sprite is assigned to the current frame slot.
  Because the bounds of an empty slot cannot be edited, capturing a rectangle into an empty slot
  takes `Assign` first, then the bounds, then `Capture`; or use `Select Capture`, which needs no
  bounds.
- `Select Capture` enables a persistent shaped capture selection on the original frame. Use the
  normal point, line, rectangle, circle, ellipse, and filled-shape tools on the original frame to
  add selected pixels; hold `Shift` to erase selected pixels. `Clear Sel` removes the selection.
- When a capture selection exists, `Capture` uses the selected pixels instead of the slot rectangle.
  The selected pixels are copied into the sprite relative to the selection bounds, unselected pixels
  inside those bounds remain transparent, and selected black original pixels remain real sprite
  pixels.
- `Capture` currently replaces the selected sprite record after confirmation. The dialog shows the
  planned `Create New` path, but it stays disabled until sprite allocation updates all cROM and
  `.cRP` sprite tables together.
- Rectangle capture without a capture selection still treats black original pixels as transparent.
- The current frame's sprite assignment slots and bounding boxes are shown as text.
- `Assign` writes the selected sprite into the selected slot for all selected frames.
- `Remove` clears the selected slot's sprite assignment for all selected frames. Selected frames
  without sprite slots are skipped, and the status bar reports how many frames were changed and how
  many were skipped.
- `Min X`, `Min Y`, `Max X`, and `Max Y` edit the selected slot's bounding box for all selected
  frames in which that slot has a sprite. They are disabled while the slot of the current frame is
  empty. A minimum cannot be raised above its maximum and a maximum cannot be lowered below its
  minimum; the edited value stops at the other one.
- Removing a sprite assignment preserves the slot's bounding box, so a replacement sprite can reuse it.
- `Show Bounds` toggles a translucent filled overlay for the selected slot's bounding box on the original frame. It is disabled by default.
  The overlay also shows the bounds an emptied slot has kept.
- `Edit Sprite` and `Select Capture` are switched off when a project is opened. `Edit Mask` and
  `Edit BG` switch `Edit Sprite` off, since they return to the frame view.
- Canvas, thumbnails, and PNG export compose detected frame sprites over the resolved frame preview.
  Sprites are looked for and drawn by libserum's rules:
  - the sprite list of a frame ends at its first empty slot, so a sprite in a slot behind an
    empty one is not looked for;
  - a sprite assigned to two slots is looked for once, in the bounds of its first slot;
  - a detection area may end on `Max X` and `Max Y`: a sprite that fills its bounds is found;
  - a sprite in shape mode is compared by lit and unlit pixels, whatever the shades are;
  - a dynamic sprite pixel takes the color its dynamic set gives the original frame's shade
    at that place.

In projects with 256x64 original frames, sprites are detected, bounded, captured and drawn on
the 256x64 original, `Create HD Sprite` is `Create 256x64 Sprite` and there is no
`Remove HD Sprite`; see "Projects With 256x64 Original Frames". In projects with 192x64 original
frames all of this happens on the 192x64 original and there are no HD sprites; see "Projects
With 192x64 Original
Frames".

Sprite-name edits are limited to 31 characters so the legacy fixed 32-byte `.cRP` name slot
remains null-terminated.

## Backgrounds

The app loads and saves background data from modern `.cROM` projects.

Currently implemented:

- Shared SD and HD background image data is preserved.
- Per-frame background IDs are preserved.
- Per-frame SD and HD background masks are preserved.
- The project status line shows the loaded background count.
- `BG ID` assigns the shared background for all selected frames that have background data. `None` stores the legacy no-background assignment.
- `Show BG` renders the assigned background layer over original black pixels where the active SD/HD background mask allows it.
  The original pixel looked at is the one the plane pixel belongs to: (x, y) in SD, (x/2, y/2) in
  HD, and (x, y) on the single plane of a project with 256x64 or 192x64 original frames.
- Inspector `Target -> Background` displays the selected background image on the canvas and edits that shared background image with the ordinary drawing tools.
- The `BG` toolbar toggle shows the current frame's background mask as a semi-transparent yellow overlay on the colorized frame.
- `BG` and `Dynamic` are mutually exclusive edit modes because both write on the colorized frame.
- In SD mode, `BG` edits the SD background mask.
- In HD mode, `BG` edits the HD background mask.
- Click the colorized frame to set that background-mask pixel to `1`.
- Line, rectangle, filled rectangle, circle, filled circle, ellipse, filled ellipse, and flood fill edit the active SD/HD background mask.
- Hold `Shift` while clicking, drawing, or flood filling to erase background-mask pixels back to `0`.
- Background-mask edits are undoable per frame and per resolution.

Creating or deleting background records is not exposed in the UI yet.
Background import/copy operations replace existing background records only, because the first release
must not change the loaded cROM format.

## Missing Editing Features

Known feature gaps that affect first-release completeness:

- Palette import/export, color-set management, and reusable gradient definitions.
- Directional palette-gradient fills for masks, sprites, and dynamic masks.
- Filling with palette gradients while respecting outline-only tool shapes.
- Wand/free selections for arbitrary saved selections.

## Preview Rendering

libserum is the source of truth for every runtime-composed pixel. There are two renderers:

- The **Runtime view**, runtime playback and `Export Current Frame PNG...` show libserum's own
  output, see "Runtime View".
- The **editing canvas and the thumbnails** use the editor's own renderer, which draws the
  layers being edited and has to stay cheap. It follows libserum's
  rules and is checked against libserum by automated tests on constructed projects of all three
  layouts and by `colorizing_qt_v2_preview_diff` on real projects (see "Testing Checklist").

The rules the editor's renderer follows, in libserum's order:

- Frame pixels are resolved from background, static and dynamic data in one scan. The
  background shows where its mask is set and the original is 0; a dynamic zone takes its set's
  color for the original's shade.
- A lit dynamic pixel casts its set's shadow onto its neighbors in the set's directions. A
  shadow never covers lit dynamic content, and the first shadow on a pixel stays.
- Sprites are looked for on the original frame and drawn after the frame and its shadows, in
  the order they were found. The rules are listed under "Sprites".
- In a project with 256x64 original frames, a frame without its 256x64 data is black, and
  libserum's rule for the 256x64 dynamic data of sprites applies: sprite number N keeps it only
  if frame number N has 256x64 data.

On the plane that has the size of the original frame (128x32 in an ordinary project, 256x64 and
192x64 in the other two layouts) the editor's renderer and libserum give the same pixels, with
one intended exception: in a project with 256x64 original frames, a frame whose background or
sprite lacks its 256x64 data is still drawn without it, so that it can be edited, while libserum
shows nothing for the frame. Last
measured: `afm_113b` 3047 of 3047 frames, `trn_174h` 5211 of 5211, `hook_501` 3897 of 3897,
`smb3` 4430 of 4430, `frankst1` 303 of 303 sampled frames.

On the HD plane of an ordinary project the editor's renderer follows libserum's layers: the HD
frame and HD background as they are, and on top the dynamic zones of the SD plane with the SD
colors (the stored HD dynamic mask and colors are not used, see "Dynamic Masks"), an unlit zone
pixel under the SD background mask left to the HD picture, and shadows generated on the HD plane,
one pixel wide, from the SD shadow tables with the HD tables as a fallback per set. An automated
test compares the whole HD plane with libserum for a frame with HD statics, zones, shadows and a
background, built of rows so that libserum's upscale has nothing to round, and checks that other
stored HD dynamic masks and colors change nothing.

Known differences, all on the HD plane of an ordinary project. libserum upscales what it renders
at 128x32 once (Scale2x) before putting it on top. The editor doubles pixels instead.

- **Edges of dynamic zones.** Scale2x rounds corners: a pixel at the edge of a glyph gets the
  color of a neighboring 128x32 pixel, or is left to the HD frame below. This is all that is left
  of the dynamic differences: in `trn_174h` 113940 pixels in 4222 of 5211 frames, 27 per frame,
  every one of them such an edge pixel (before the editor used the SD zones: 151384). Shadows
  follow the rounded shape in libserum and the doubled one in the editor.
- **Edges of sprites and backgrounds.** libserum reads the original frame upscaled where it
  decides between background and frame, and rounds the edges of upscaled sprites. The editor
  doubles pixels: a sprite without HD is its 128x32 picture with every pixel doubled, and an HD
  sprite keeps the doubled 128x32 pixels where the sprite is dynamic. Single pixels differ along
  such edges, for example `trn_174h` frames 1455 and 697 (782 pixels in all).
- **Frames without HD** have no HD plane in the editor at all. libserum's upscaled 64-row
  picture of them is only in the Runtime view.
- **A frame with HD whose background has no HD image** is, by libserum's code
  (`HasHdStaticContent()`), rendered like a frame without HD, upscaled from 128x32. The editor
  shows the HD frame without the background. Not measured: none of the real projects has one.
- **HD pixels painted under a stored HD dynamic mask** are saved as 0, see "Saving". The canvas
  shows the painted color until the project is reopened.

For all of these the Runtime view with `Runtime Plane: 64 Rows` shows the real picture.

## Testing Checklist

For drawing changes, test at least:

- SD preview and commit.
- HD preview and commit.
- Undo and redo in the same frame/resolution.
- Switching SD/HD does not interleave undo histories.
- Save, reopen, and verify edits persist.
- With `Mask` active, add and Shift-erase pixels on the original/reference region, undo/redo the mask edits, save, reopen, and verify the mask overlay persists.

For changes that read the original frame (dynamic colors, shadows, backgrounds, sprites,
comparison masks, anything on the original/reference region), test a project with 128x32, one
with 256x64 and one with 192x64 original frames. The automated tests and `tools/ui_dump` contain a
constructed 256x64 project each, including frames, a background and sprites without HD data, and
a constructed 192x64 project each, whose snapshot has 96x32 data in use.

For changes to SD/HD handling, check in a project with 256x64 original frames that nothing shows,
edits or falls back to its SD data, and in a project with 192x64 original frames that no 96x32
data is shown or kept and that a save writes its extra plane as "no extra data", the same on a
second save. In both, check that the window says "HD" or "SD" nowhere: no toolbar buttons, no
badge, no filter entry, no counter, no `Create HD` / `Remove HD` (see the table in "SD And HD
Modes"), and that an ordinary project looks exactly as before.

For changes to the frame list, check with a large project that opening does not build one
thumbnail per frame, that rows show their picture when scrolled into view (also after jumping
with the scroll bar, `End`, a filter change and during playback), that a thumbnail in view
follows an edit, undo and redo at once, and that a row that was edited while out of view shows
the new picture when it is scrolled back to.

For changes to the editor's renderer (`app/FrameImageUtils`), to the runtime preview or to
libserum, build `colorizing_qt_v2_preview_diff` and run it against real projects. It renders
every frame with the editor's renderer and with libserum and compares the pictures; it writes
nothing:

```text
colorizing_qt_v2_preview_diff <project>.cROM|.cRZ <project>.cRP [first[-last]] [--step N]
                              [--examples N] [--pixels N] [--playback N [--no-whole]]
```

It compares the plane that has the original frame's size and, in an ordinary project, the HD
plane of frames that have HD. The upscaled 64-row plane of frames without HD is not compared.
Per plane it prints how many frames are identical and, for the others, the differing pixels by
class (inside a found sprite, inside a sprite slot's bounds, shadow, the edge rounding of
libserum's upscale in or beside a dynamic zone, anything else in a dynamic zone, background,
static) with example frames. `--pixels N` prints the first N differing pixels of each such frame,
`--step N` takes every Nth frame. The last line gives the mean cost of a runtime render. The
exit status is 0 only if every compared plane is identical.

Expected: `native plane ...: N frames compared, N identical` for every project. An ordinary
project with HD frames that have dynamic content or sprites reports differences on its HD plane,
see "Preview Rendering"; for `trn_174h` that is 4249 of 5211 frames and 114722 pixels, classes
`dynamic, upscale rounding` (113940), `sprite` (527) and `sprite area` (255), and none in the
class `dynamic`. A full run takes 2 to 25 seconds per project; `frankst1` needs about 3.2 GB
to load, so sample it with `--step`.

`--playback N` checks runtime playback instead of the pictures of single frames. It plays the
frames of the range in order through the runtime preview, in windows of at most N played frames
as the Runtime view does (the editor uses 48), and through libserum with the whole project
loaded as one cROM, and compares every step: the frame identified, whether libserum reported the
picture as unchanged or skipped the frame, and both planes. It also compares what libserum says
it identified with the editor's statement of libserum's search rule, and prints how many windows
were loaded and what they cost. The whole project needs its cROM in memory, about 1.7 to 3.7 GB
for the projects named here; `--no-whole` leaves that comparison out and reports the windows.
Expected for every project: `0 steps identified differently, 0 reported differently` and
`search rule against libserum: 0 differences`. Pictures may differ at single pixels of the
64-row plane, see "Runtime View". Last measured over all frames: `afm_113b` 3047 frames, 8
identified as another frame than the one fed, no difference; `hook_501` 3897, 26, none; `smb3`
4430, 3, pictures in 19 steps (19 pixels of the 64-row plane); `trn_174h` 5211, 48, pictures in
13 steps (26 pixels); `frankst1` 11199 frames with `--no-whole`, 4, search rule without
difference.

For changes to the Runtime view, also check by hand: `Ctrl+R`, the `Runtime` toolbar button and
`View -> Runtime View` on and off in an ordinary project in SD and in HD mode, both runtime planes
on a frame with and without HD, an Inspector edit and undo while the view is on, `Play` with the
view on over a frame with a color rotation and over frames that share an original (the status
line names the frame libserum identified), `File -> Export Current Frame PNG...` with the view on
and off and on an inactive frame, and the same in a project with 256x64 and one with 192x64
original frames.

For changes to dynamic data, check in an ordinary project that `Dynamic` is disabled in HD mode
with its tool tip, that the `Dyn` page there shows the SD colors with its note, that a color
changed there shows in the HD picture, and that a project with stored HD dynamic data still
round-trips byte-identical.

For load/save changes, build the `colorizing_qt_v2_roundtrip` tool and run it against real
projects. It loads a project, serializes it in memory, and reports per block where the result
differs from the files on disk; it writes nothing:

```text
colorizing_qt_v2_roundtrip <project>.cROM <project>.cRP
colorizing_qt_v2_roundtrip <project>.cRZ <project>.cRP
```

With a `.cRZ` the comparison is against the `.cROM` stored in the archive.

Projects last saved by the Win32 editor must come back `identical`, cROM and cRP.

A project with 192x64 original frames is the exception for the cROM: its 96x32 data is discarded
on load, so blocks of the extra plane (`extra ... flags` and every block named `HD ...`) may
differ. The tool marks each of them with `(expected: 96x32 data is discarded)`, prints
`identical except the discarded 96x32 extra plane` and still exits with 0. Only the blocks of
the original plane and the whole cRP are expected to be identical; a difference in any other
block is printed without that remark, the result is `DIFFERENT` and the exit status 1. For
`frankst1` the expected output is:

```text
cROM: 710862076 bytes on disk, 710862076 bytes serialized
  HD dynamic colors: 688808 of 2866944 bytes differ, first at offset 519056805 (expected: 96x32 data is discarded)
  HD background masks: 34403328 of 34403328 bytes differ, first at offset 674308540 (expected: 96x32 data is discarded)
  identical except the discarded 96x32 extra plane
cRP: 139931019 bytes on disk, 139931019 bytes serialized
  identical
```

For project-loading changes, test:

- Open via `.cROM` with matching `.cRP`.
- Open via `.cROM` and manually selected `.cRP`.
- Open via `.cRZ` with no `.cROM` next to it, save, and verify `.cROM`, `.cRP` and `.cRZ` are
  there, no `.saving` file is left, and the `.cRZ` unpacks to the bytes of the `.cROM`.
- Open via `.cRZ` while a different `.cROM` of the same name lies next to it: the archive's
  content is what loads.
- Open via `File -> Open Recent`.
