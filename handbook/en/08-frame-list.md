# Frame List

The frames run along the bottom of the window, as in the Win32 editor: one row, left to right,
scrolled sideways. Each frame shows its thumbnail (colorized above, original below) and under it
its number (with the name of a section it starts) and a line of compact metadata: duration,
`HD`, `Dyn`, `BG n`, sprites, `inactive` and the like. The section box, the trigger box and the
frame filter are at the left end of the strip; the comparison masks, backgrounds and sprites are
in the sections on the left of the window (see "Sections").

In the rest of this handbook "the frame list" is this strip.

## Sections and triggers

At the left end of the frame strip:

- The section box shows the section of the current frame: the one with the latest first frame
  at or before it (`- None -` before the first section). Choosing a section goes to its first
  frame. The row of a frame that starts a section shows the name, for example `3 [Game]`.
- `Name` starts a section at the current frame; on a frame that already starts one the button
  reads `Rename` and renames it. A name has 1 to 31 characters, is not `- None -` and is not
  the name of another section; otherwise nothing changes and the status bar says why. A
  project holds at most 512 sections.
- `Delete` deletes the section of the current frame, after a question. Its frames stay and
  belong to the section before it then.
- `Order` opens the list in its own order, which need not be the order of the first frames
  (as in CDMD): `Up` and `Down` move the selected section, `A-Z` sorts by name. Each step is
  applied at once.
- Every change of the sections is an undo step of its own (`Undo section change`), reachable
  from every view. Deleting frames moves the first frames (see "Deleting frames") and drops the
  section undo steps made before.
- The trigger box lists every frame with a trigger ID as `Fr: <frame> / TID: <id>`, sorted by
  ID and then frame, as CDMD's trigger list; it shows the current frame's entry when it has
  one. Choosing an entry goes to the frame. A frame hidden by the frame filter is not shown;
  the status bar says so.

Sections are editor data of the `.cRP`; the runtime does not use them.

Thumbnails are drawn when their frames come into view in the strip, not when the project is
opened: a frame that was never scrolled to shows an empty dark box of the thumbnail's size,
which is replaced by the picture as soon as the frame is in view (two frames left and right of
the visible part are prepared as well). Frames take the same room with and without a picture,
so scrolling, selecting, filtering and keyboard navigation do not depend on it. A thumbnail that was drawn is kept and redrawn when
its frame changes; nothing has to be done to refresh it. Opening a large project therefore no
longer waits for one thumbnail per frame.

Frame items show the project frame number, the loaded cRP duration, and compact flags such as
`HD`, `Dyn`, assigned `BG`, and sprite assignment count when present.
`Dyn` means that the frame has a dynamic zone the runtime renders: at least one pixel of its
dynamic mask on the plane libserum takes dynamic zones from (the 128x32 plane of an ordinary
project, the 256x64 or 192x64 plane of the other layouts). Every frame of a loaded project carries
dynamic data, mostly all "not dynamic", so the flag does not just say that the data exists. It
follows dynamic mask edits, their undo and redo, and so does the frame's thumbnail.
Selecting a frame updates the central canvas and the frame status line.
Whenever the editor picks the current frame itself (after deleting or adding frames, after undo
and redo of such a step, after a filter change, during playback, `Go to frame`), that frame
becomes the only selected one; an earlier multi-selection does not stay behind.

A frame with a reference the runtime cannot follow has the line `ref. problem`, and its tool tip
and the frame status line's tool tip say which: a comparison mask, background or sprite ID that
names no record, or an empty sprite slot with used slots behind it (see "Sprites"). Only loaded
projects can have these, since the Win32 editor writes what it holds; the editor loads and saves
them unchanged and never makes new ones. None of the five 3.17.3 test projects has one.

A frame the runtime skips has the line `inactive`, and its tool tip and the tool tip of the
frame status line say `inactive: shorter than 15 ms and not colorized — the runtime skips it`.
These are the frames a save flags as inactive (shorter than 15 ms, every pixel still the plain
color of the original, no dynamic zone; the rule of the serializer, which is the Win32 editor's)
that have no assigned background either: libserum identifies such a frame and renders nothing
for it, so a machine keeps the picture it shows. An inactive frame with a background is rendered
by libserum and is not marked. The marker follows edits: painting the frame, giving it a dynamic
zone or a background, or a duration of 15 ms or more removes it.

The filter of the frame strip can show all frames, only frames with HD data (`HD frames`),
frames with an assigned background, frames with a dynamic zone (the frames marked `Dyn`), frames
with sprite assignments, frames with a color rotation (`With rotation`: a rotation slot of a
length above 0 on a plane the frame has), the frames marked `inactive` (`Inactive`), in a
project with scenes the frames of all scenes (`Scene frames`), the others (`Without scene
frames`) or the frames of one scene (`Scene <ID>`; see "Scenes and Scaling"), or `Using the selected
asset`: the frames that use what is selected in the asset list used last on the left (opened,
or clicked in): the comparison mask (`Masks`), the assigned background (`Backgrounds`) or the
sprite in one of their sprite slots (`Sprites`); before any was used, the first open one. It
follows both: choosing another asset, or using another of the three lists, shows its frames.
Editing still targets the real project frame number shown in the item label, not the visible row
number.

In a project with 256x64 or 192x64 original frames there is no `HD` flag in the items, no blue
`HD` badge on the thumbnails and no `HD frames` filter entry, and the project status line above
the list leaves out the HD count (`<rom>: 5 frames, 2 backgrounds, 3 sprites`). The frame status
line names the plane by its size: `Frame <n> | 256x64 | <duration> ms`.
In a project with 256x64 original frames the filter has the entry `Not rendered` instead, and
frames the runtime shows nothing for are marked in the list; see "Projects With 256x64 Original
Frames".

The frame list supports native multi-selection (`Ctrl/Cmd`+click adds or removes a frame,
`Shift`+click a range). As in the Win32 editor, a plain click on a frame of a selection of
several frames keeps the selection and only makes that frame the current one, so the canvas
can show each of them in turn (the canvas selection stays, see "Selection"); a plain click on a
frame outside the selection selects that frame alone. Frame drawing, comparison-mask drawing,
dynamic-mask drawing, background-mask drawing, and all frame settings (comparison mask ID, shape
mode, background ID, trigger, duration) apply to all selected frames where
the operation is valid. Comparison-mask edits are grouped by assigned mask ID, so a shared mask is
edited once even if multiple selected frames use it. Undo/redo history is still stored per frame,
per resolution, or per shared mask as appropriate.

When an edit cannot apply to some of the selected frames, those frames are skipped and the others
are edited. The status bar then reports both counts, for example
`Assigned background 1 on 3 frame(s), 1 skipped (no background data)`. Frames that already have the
requested value are neither changed nor counted.

The selected frames are deleted with `Edit -> Delete Frames...`, with `Delete Frames...` in the
context menu of the frame list, or with the `Delete` or `Backspace` key while the frame list has
the focus. `Delete Frames Shorter Than...` (Edit menu and context menu) deletes those of the
selected frames that are shorter than a duration. See "Adding And Deleting Records".
