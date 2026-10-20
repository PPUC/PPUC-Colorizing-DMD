# Projects With 192x64 Original Frames

Some ROMs deliver 192x64 frames. The Win32 editor stores such a project with the 192x64 ROM frame
as its original plane and a half-size 96x32 plane as its extra plane. The frame status line of
such a project reads `Frame <n> | 192x64 | ...`.

The same rule as for 256x64 projects applies: everything happens on the ROM frame at its own
resolution, and a plane smaller than the ROM frame is not a plane to edit. So the project has one
plane in the editor, the 192x64 one. It is the ROM frame's own grid: plane pixel (x, y) belongs
to original pixel (x, y). The editor calls it `192x64`, never SD or HD.

## What is edited

Everything on the 192x64 plane, with coordinates 0..191 and 0..63:

- the original frames and the comparison masks (frame identification hashes the whole 192x64
  original with the 192x64 comparison mask),
- per frame: the colorized frame, the dynamic mask, dynamic colors and shadows, the color
  rotations, the background mask, trigger, duration, mask and shape settings,
- the background images,
- sprites: the sprite original, the sprite colors, the detection areas; per frame the sprite
  slots and their `Min X`/`Min Y`/`Max X`/`Max Y` bounds, which range over 0..191 and 0..63.

The canvas, the thumbnails, the frame list, playback, PNG export, image import (the image is
scaled to 192x64), `Frame -> Background`, `Background -> Frame`, background editing, the `Mask`, `Dynamic` and
`Background` modes, `Select Capture`, `Capture` and `Show Bounds` all work on that plane. A sprite is
looked for on the 192x64 original and drawn one to one where it was found. The sprite editor shows
the same 256x64 sprite cell as in every project.

On the canvas a 192x64 pixel is as large as a pixel of an HD frame or of a 256x64 original; the
frame is 3:1 instead of 4:1, and the thumbnails keep that shape.

On save a frame counts as colorized ("active") if its 192x64 frame differs from the plain
original colors or it has dynamic content on the 192x64 plane, or if it lasts 15 ms or longer.

## The 96x32 data is discarded

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

A new sprite or background (`New` on the `Sprites` and `Backgrounds` sections) has 192x64 data only
and is written with "no extra data" like every other record.

## What is not there

- No `SD` and `HD` toolbar buttons.
- No `Create HD`, `Remove HD`, `Create HD Sprite` or `Remove HD Sprite` button, and therefore no
  `Create HD` dialog.
- No `HD` flag, badge, filter entry or counter, and no `(HD)` suffix in the sprite and
  background lists.
- Status texts and proposed file names say `192x64` where they say `SD` or `HD` in ordinary
  projects.

## Status

Verified with one real project saved by the Win32 editor 3.17.3 (`frankst1`: 11199 frames, no
sprites, no backgrounds, no 96x32 pictures in use), and with constructed projects in the
automated tests and `tools/ui_dump` that also have sprites, backgrounds and 96x32 data in use.
`frankst1` loads, shows and saves; a load and save changes two blocks of its extra plane, the
dynamic colors (the file has values there) and the background masks (the file has 0, the editor
writes 1), and nothing else.

Nothing is padded or converted to another geometry.

The editor's picture of the 192x64 plane is the same as libserum's, see "Preview Rendering";
the Runtime view shows libserum's own.
