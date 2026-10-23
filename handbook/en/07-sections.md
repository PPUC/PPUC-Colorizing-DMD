# Sections

The asset lists on the left (`Comparison Masks`, `Backgrounds`, `Sprites`) and the inspector on the
right (`Frame`, `Comparison Mask`, `Background`, `Sprite`, `Dynamic Sets`, `Palette`, `Color
Rotations`, `Color Sets`, and `Team` in a team project) are sections one below the other, each with
a header that opens and closes it (a click on the header). Any number can be open at a time: the
open sections share the height of their column (one alone takes all of it, two about half each at
first), each scrolling its own content where it does not fit, and the headers of the closed ones
stay visible. Below an open section with another open section further down there is a thin handle:
dragging it moves the border between the two (the others keep their size, and a section stays at
least 40 pixels high); a double click on a handle gives all open sections the same share again.
Which sections are open, and their shares, are kept for the next start; at first `Comparison Masks`
and `Frame` are open.

## Arranging the sections

Each column can be arranged to your needs, and the arrangement is kept for the next start:

- Drag a section's header up or down to move the section; a line shows where it goes. Dragging
  does not open or close it.
- Right-click a header for `Move Up` and `Move Down` (past the sections shown), `Hide ...` for an
  optional section, `Show ...` for every section you have hidden, and `Restore the Sections`
  (the first order, every section shown).
- Optional are the sections not every colorization uses: on the left `Scenes` and `Light
  Effects`, on the right `Color Rotations`, `Color Sets`, `Helper Lines`, `Speaker Lights` and
  `Light Effect`. A hidden section stays hidden whatever the canvas shows; its content is
  unchanged.

A header can show a bell with a number: something waits in that section (in the `Team` section,
the partners' shared changes to merge).

The editor also hides, on its own, the sections the canvas shown has no use for (see the canvas
modes); a section shows when neither you nor the editor hides it.

Both columns can be made wider or narrower by dragging their edge next to the canvas; the widths
are kept for the next start.
