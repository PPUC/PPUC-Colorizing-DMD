# SD And HD Modes

Use the `SD` and `HD` toolbar buttons to switch the active editing resolution, or
`View -> Switch SD/HD` (`Ctrl/Cmd+X`, as in the Win32 editor) to switch to the other one.

SD and HD exist in ordinary projects only (128x32 original frames with an optional 256x64 HD
plane). What the window shows for each kind of project:

| | Ordinary project | 256x64 original frames | 192x64 original frames |
| --- | --- | --- | --- |
| `SD` and `HD` toolbar buttons | shown | hidden | hidden |
| Frame list item | `HD` line for frames with HD | no `HD` line; `no picture`, `Background: no picture`, `Spr: no picture` for what is missing | no `HD` line |
| Thumbnail badge | blue `HD` | red `no picture` on frames that are not rendered | none |
| Frame filter | `HD frames` | `Not rendered` | neither |
| Project status line | `..., <n> HD, ...` | no HD count; `, <n> not rendered at runtime` | no HD count |
| Frame status line | `SD 128x32 \| HD 256x64` or `HD unavailable` | `256x64`, and `\| not rendered` | `192x64` |
| Frame page buttons | `Create HD`, `Remove HD` | `Create 256x64 Frame` | none |
| Sprite page buttons | `Create HD Sprite`, `Remove HD Sprite` | `Create 256x64 Sprite` | none |
| Background page buttons | `Create HD`, `Remove HD` | `Create 256x64 Background` | none |
| Sprite and background lists | `(HD)` and a blue `HD` badge on the thumbnail | `(no picture)` on records without 256x64 data | no suffix |
| Status texts, export file names | `SD`, `HD`, `_sd`, `_hd` | `256x64`, `_256x64` | `192x64`, `_192x64` |

The sprite and background thumbnails in the left sidebar always show the SD picture (the only
picture in a project with one plane), whichever plane is edited.

Buttons and entries that do not apply are hidden, not disabled, and take no room: the toolbar
starts with `Mask` in a project without SD and HD, and the `Frame` section ends after `Import Image`
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
- The Inspector `Create HD` button creates HD data for every selected frame that has none (the
  current frame when no other is selected), by scaling its SD colorized frame to 2x, as the
  Win32 editor's extra resolution switch acts on all selected frames.
- When creating HD, a dialog asks whether to also upscale the frames' background masks and their
  assigned background images. A background that several of the frames use is upscaled once.
- The dialog's `Filter`, as CDMD's filter box, says how the SD picture and the background image
  become twice as large (the masks are always doubled): `Nearest` doubles every pixel (as
  before), `Scale2x` keeps the colors and smooths diagonal edges (the way libserum upscales
  dynamic content and sprites at runtime), `Bilinear`, `Bicubic` and `Lanczos4` blend
  neighbouring colors as OpenCV does in CDMD (centres aligned, edges repeated), which adds
  colors the SD picture did not have. The last choice is offered again. The status bar names
  the filter used.
- `Create HD` does not create HD dynamic data and the dialog does not offer it: the runtime renders
  the dynamic zones of the SD plane into the HD picture and does not read an HD dynamic mask. The
  frame's stored HD dynamic mask, colors and shadows are written as they are: for a frame that
  never had any that is a mask of 255 everywhere (no zone) and colors and shadows of 0, which is
  what the Win32 editor gives a frame without HD.
- In HD mode the `Dynamic` toolbar button is disabled, and the `Dynamic Sets` section shows and edits the
  dynamic colors and shadows of the SD plane. See "Dynamic Masks".
- The Inspector `Remove HD` button removes HD data from every selected frame that has it and
  switches the canvas back to SD when needed.
- `Create HD` and `Remove HD` can be undone, on all frames they changed in one step (each
  frame's part is in the history of that frame). Undo puts back exactly what was there before: the
  frame's HD pixels (or their absence) and, for `Create HD`, every related item the dialog upscaled,
  including the previous HD image of the assigned background. See "Undo And Redo".

