# Projects With 256x64 Original Frames

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

## What is edited

Everything the runtime uses, all of it on the 256x64 grid:

- the original frames and the comparison masks (frame identification hashes the whole 256x64
  original with the 256x64 comparison mask),
- per frame: the HD colorized frame, the HD dynamic mask, dynamic colors and shadows, the HD color
  rotations, the HD background mask, trigger, duration, mask and shape settings,
- the HD background images,
- sprites: the sprite original (what is detected), the detection areas, the shape mode, the HD
  sprite mask (what is drawn) and the HD sprite colors,
- per frame: the sprite slots and their bounds.

The canvas, the thumbnails, the frame list, playback, PNG export, image import, `Frame -> Background`,
`Background -> Frame`, background editing, the `Dynamic` and `Background` mask modes and the rotation and dynamic
pages all work on that plane.

## What is ignored

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

## No SD mode

The `SD` and `HD` toolbar buttons are hidden: there is nothing to switch. Every place where an
ordinary project falls back to SD when HD data is missing instead reports the missing 256x64
data in the status bar and does nothing (PNG export of a frame or background, `Frame -> Background`,
`Background -> Frame`, `Edit Background`, sprite `Capture`). The messages name the data by its size, for example
`Frame 4 has no 256x64 frame: there is no picture to export`.

## The 256x64 data is required everywhere

libserum renders a frame of such a project only if the frame has HD data, its assigned
background (if any) has HD data and every sprite assigned to it has HD data. Otherwise it still
identifies the frame and reports its trigger, but produces no picture: the display keeps what it
showed before. There is nothing it could fall back to.

Therefore:

- There is no `Remove` button, neither for a frame, a sprite nor a background: the ordinary
  project's `Remove HD` and `Remove HD Sprite` are hidden.
- A project in which that data is missing still opens. Each affected frame is flagged:
  - a red `no picture` badge on its thumbnail,
  - `no picture`, `Background: no picture` and `Spr: no picture` lines in the frame list item, with the
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

- `Create 256x64 Frame` on the `Frame` section (the ordinary project's `Create HD`) is enabled for a
  frame that lacks something. For a frame without its 256x64 frame it creates that frame from the
  256x64 original, each pixel in the plain color of its original color index: the same picture an
  uncolorized frame has. It upscales nothing; the frame's dynamic mask,
  dynamic colors, background mask and rotations stay as they are stored.
- `Create 256x64 Frame` is also enabled when only the assigned background lacks its 256x64
  image. It then asks first, naming the backgrounds: `Give Them a Black Image` gives each a
  black 256x64 image, which every frame showing it shows until the background gets a picture;
  `Frames Only` (offered when frames lack their 256x64 frame too) creates just those; `Cancel`
  creates nothing. An existing background image is never replaced. `Create 256x64 Background` on the `Background` section gives the
  selected background a black 256x64 image as well, and its `Import Image` gives it a picture.
- `Create 256x64 Sprite` on the `Sprite` section creates the 256x64 sprite of the selected sprite
  from the sprite original: its mask gets the shape of the sprite original, its colors are the
  plain colors of its color indices.
- All three can be undone.
- A button is disabled when there is nothing to create.

A sprite added with `New` has its 256x64 data from the start (empty), and a background added
with `New` takes the 256x64 picture of the current frame, so neither makes a frame "not
rendered". `New` on the `Backgrounds` section needs a current frame with its 256x64 frame. See
"Adding And Deleting Records".

## Sprites

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

## On a 128x32 display

There is no 128x32 version of such a colorization. A host that asks libserum for 32-row output
only gets no picture (frames are still identified and triggers reported); a host that asks for
32-row output "with fallback", for 64-row output or for both gets the 256x64 picture. Whether and
how that is shown on a 128x32 panel is the host's business, not the colorization's.

## What the other parts of the editor do differently

- The original/reference region shows all 256x64 original pixels, in the same on-screen size as
  for a 128x32 original. One original pixel is exactly as large as one pixel of the plane above
  it.
- The comparison mask overlay and all comparison mask tools work on the 256x64 pixels of that
  region.

## Status

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
