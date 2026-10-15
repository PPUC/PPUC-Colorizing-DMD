# Runtime View

`View -> Runtime View` (`Ctrl/Cmd+R`) shows the current frame on the canvas as libserum renders it,
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

- In an ordinary project `View -> Runtime Plane: 32 Rows` (`Ctrl/Cmd+3`) and `Runtime Plane: 64 Rows`
  (`Ctrl/Cmd+6`) choose between what a display with 32 rows and one with 64 rows gets. When the view
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
to the next as it does on a machine, and its color rotations run on libserum's own clock: the
editor calls libserum's rotation step when libserum says the next one is due and shows the
planes libserum then holds. It does not rotate anything itself in this view. Outside playback
the view is a still; nothing rotates in it.
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
