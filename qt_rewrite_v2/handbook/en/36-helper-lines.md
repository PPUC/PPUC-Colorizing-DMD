# Helper Lines

Helper lines are named horizontal and vertical lines over the canvas, to line things up on
several frames and to measure: a score's baseline, the column a ball runs in, the edge of a
logo. They are the same on every frame and belong to the project: saved in its `.cRPX`, not
seen by libserum or a machine.

The `Helper Lines` section of the right sidebar lists them:

- `Horizontal` and `Vertical` add a line. It starts at the row or column the mouse last pointed
  at on the canvas (the middle of the frame before that), so pointing at a place and then
  adding a line puts it there; the window that opens takes its name, its direction and its
  position.
- The check box of a line shows or hides it on the canvas.
- `Edit` (or a double click) renames or moves the selected line, `Delete` removes it.

A line is given in the pixels of the plane that was active when it was added (SD or HD; in a
project with one plane, that plane) and lies on the border before that pixel: a horizontal line
at 8 is the top edge of row 8, one at 32 on a 32-row frame the bottom edge of the frame. It is
drawn dashed in light green at the same place on the original frame and on both planes, scaled
to each, and reaches a little past the frame. Its name is written outside the frame, so that
every pixel stays visible: left of a horizontal line, above a vertical one. While lines are
shown, the frame leaves room for their names beside it (it is drawn a little smaller); zoomed in,
where the frame fills the canvas, a name slides in and stays readable at the edge of the visible
part. The pixel under the mouse in the status bar (see "Canvas") gives the
numbers to use.

Adding, changing, showing, hiding and deleting lines can be undone, in a history of their own,
and mark the project as changed.
