# Auto Copy And Scrolling Fill

## Auto copy

`Edit -> Auto Copy...` (Win32 `IDC_AUTOCOPY`) copies the current frame's colorization inside the
selection to every frame whose original frame shows exactly what the current frame's shows
there. The selection is turned into ROM frame pixels first (on the doubled HD plane of an
ordinary project a ROM pixel counts when any of its 2x2 pixels is selected). Candidates are
found by a CRC32 over the selected original pixels, as the Win32 editor does, and every
candidate is then compared pixel by pixel, so two different pictures with the same CRC are not
taken.

The dialog chooses `All frames` or `Selected frames only` and, before anything changes, says how
many frames show the same and how many of them would change (a frame that already has those
colors does not change), how many have no picture on the HD plane, and how many had the same CRC
but other pixels. `Apply` writes the colors and, on the dynamic plane, the dynamic mask of the
selected pixels; the dynamic set colors are not copied (Win32 `AutoCopy` copies the frame and the
mask only). One undoable step per changed frame. Unlike the Win32 editor (which asks nothing and
cannot undo it), the count comes first and every frame can be undone.

## Scrolling auto fill

`Edit -> Scrolling Auto Fill...` (Win32 `IDC_SCROLLFILL`, `AutoFillScrolling`) colorizes the
frames of a scrolling between two colorized frames. Select the frames of the scrolling in the
frame list, the current frame among them: the run of selected frames without a gap around the
current frame is used, its first and last frame are the references, and the frames strictly
between them are filled.

For each frame between, the editor looks for the smallest shift at which the frame's original
is the reference's moved up, down, left or right (in that order: "camera moving down" first;
the overlapping whole rows or columns have to be equal). The rows or columns the two share then
get the reference's colors and, on the dynamic plane, its dynamic mask; the uncovered ones stay.
This is done with the first frame, then with the last, which wins where both apply. On the HD
plane of an ordinary project the shift counts twice. Frames without a picture on the plane take
no part. The question before it runs says how many frames scroll from each reference and how
many change; one undoable step per changed frame.

The Win32 editor compares the rows and columns by CRC32; here they are compared exactly. A
single shared row is enough for a match, as in the Win32 editor, so rows that repeat (blank
rows) can match at another shift than the true one.
